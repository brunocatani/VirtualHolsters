#include "HandHolsters.h"
#include "HolsterMath.h"
#include "utils.h"
#include "Offsets.h"
#include "VRHook.h"
#include "VRSceneGraph.h"
#include "NativeEquipment.h"
#include "InvSorting.h"
#include "f4se/NiNodes.h"

namespace Holsters {
	// Stand-ins and learned grips live in the hand's controller frame,
	// where ROCK places the real weapon; the hand bone moves between an
	// open hand and a grip, so a pose kept against it lands off target.
	NiNode* HandNode(bool isLeft)
	{
		return static_cast<NiNode*>(NativeEquipment::WandNode(isLeft));
	}

	// Moves a display clone under another node without it jumping. The
	// clone is held across the detach, which drops the old parent's
	// reference.
	bool ReparentKeepingWorld(MyWeapon& record, NiNode* parent)
	{
		NiNode* mesh = record.WeapMesh;
		NiTransform local{};
		if (!mesh || !parent || !LocalFromWorld(parent->m_worldTransform, mesh->m_worldTransform, local)) {
			return false;
		}
		mesh->IncRef();
		if (mesh->m_parent) {
			VRSceneGraph::RemoveChild(mesh->m_parent, mesh);
		}
		VRSceneGraph::AttachChild(parent, mesh, true);
		mesh->DecRef();
		mesh->m_localTransform = local;
		record.blending = false;
		return true;
	}

	bool RealWeaponVisible(bool isLeft, UInt32 formId)
	{
		const HandWeaponFacts facts = GetHandWeaponFacts(isLeft);
		if (!facts.holds || facts.formId != formId) {
			return false;
		}
		if (Rock::IsBound()) {
			return Rock::WeaponShown(facts.nativeIndex, formId);
		}
		return NativeEquipment::FindEquippedModel(facts.nativeIndex, formId) != nullptr;
	}

	// On the grab the holster display leaves the hip in the hand. A
	// firearm eases into the pose ROCK seats firearms at in that hand;
	// melee, which ROCK reveals without a draw wait, stays as grabbed.
	void TakeDisplayIntoHand(HandTransfer& transfer)
	{
		const auto registered = WeaponRegisteredObjects.find(transfer.slot);
		NiNode* hand = HandNode(transfer.isLeft);
		if (registered == WeaponRegisteredObjects.end() || !registered->second ||
			!registered->second->WeapMesh || !hand) {
			return;
		}
		MyWeapon* record = registered->second;
		if (!ReparentKeepingWorld(*record, hand)) {
			return;
		}
		WeaponRegisteredObjects.erase(registered);
		// A new display of this slot must not take this one for stale.
		const std::string inHandName = HolsterNames[transfer.slot] + (transfer.isLeft ? "InLeftHand" : "InRightHand");
		record->WeapMesh->m_name = inHandName.c_str();
		record->followsAim = !record->isMelee;
		if (record->followsAim) {
			record->blendFrom = record->WeapMesh->m_localTransform;
			record->blendTo = record->blendFrom;
			record->blendElapsed = 0.0F;
			record->blendDuration = kDrawBlendSeconds;
			record->blending = true;
		}
		transfer.inHand = record;
	}

	// A draw that did not reach the hand puts the stand-in back.
	void ReturnDisplayToHolster(HandTransfer& transfer)
	{
		MyWeapon* record = transfer.inHand;
		transfer.inHand = nullptr;
		if (!record) {
			return;
		}
		record->followsAim = false;
		const UInt32 slot = transfer.slot;
		if (holsteredWeapNames[slot] == "Empty" || !record->wbone || SlotDisplay(slot) ||
			!ReparentKeepingWorld(*record, record->wbone)) {
			releaseWeaponClone(record);
			return;
		}
		record->WeapMesh->m_name = HolsterNames[slot].c_str();
		NiTransform pose{};
		if (ResolveHolsterPose(slot, *record, pose)) {
			record->blendFrom = record->WeapMesh->m_localTransform;
			record->blendTo = pose;
			record->blendElapsed = 0.0F;
			record->blendDuration = kHolsterBlendSeconds;
			record->blending = true;
		}
		WeaponRegisteredObjects[slot] = record;
	}

	void HandOffStandIn(HandTransfer& transfer)
	{
		if (!transfer.inHand) {
			return;
		}
		HandHandoff& handoff = GetHand(transfer.isLeft).handoff;
		releaseWeaponClone(handoff.clone);
		handoff.clone = transfer.inHand;
		handoff.formId = transfer.formId;
		handoff.deadline = Clock::now() + kRevealWait;
		handoff.command = transfer.command;
		handoff.weaponSlot = transfer.weaponSlot;
		handoff.weaponSlotKnown = transfer.weaponSlotKnown;
		transfer.inHand = nullptr;
	}

	static void ServiceHandoff(HandHandoff& handoff, bool isLeft, Clock::time_point now)
	{
		if (!handoff.clone) {
			return;
		}
		bool shown;
		if (handoff.weaponSlotKnown && Rock::IsBound()) {
			shown = Rock::WeaponShown(handoff.weaponSlot, handoff.formId);
		} else {
			shown = RealWeaponVisible(isLeft, handoff.formId);
		}
		if (shown) {
			releaseWeaponClone(handoff.clone);
		} else if (now > handoff.deadline) {
			_MESSAGE("Holster draw stand-in in the %s hand released before the real weapon showed",
				isLeft ? "left" : "right");
			releaseWeaponClone(handoff.clone);
		}
	}

	void ServiceHandoffs()
	{
		const auto now = Clock::now();
		ServiceHandoff(gRightHand.handoff, false, now);
		ServiceHandoff(gLeftHand.handoff, true, now);
	}

	void ReleaseHandoffs()
	{
		releaseWeaponClone(gRightHand.handoff.clone);
		releaseWeaponClone(gLeftHand.handoff.clone);
	}

	// ROCK keeps every holstered weapon's animations loaded, so a draw
	// never waits on the engine's first-use load. Sent after a load and
	// whenever a holster's contents change; the inventory is read only
	// then.
	void ServiceWarmWeapons()
	{
		static std::array<std::string, kMaximumHolsterSlots + 1> s_sentNames{};
		bool changed = false;
		for (UInt32 slot = 1; slot <= kMaximumHolsterSlots; ++slot) {
			changed = changed || s_sentNames[slot] != holsteredWeapNames[slot];
		}
		if (!Rock::WarmSendDue(changed)) {
			return;
		}
		std::array<Rock::WarmWeapon, kMaximumHolsterSlots> weapons{};
		std::size_t count = 0;
		for (UInt32 slot = 1; slot <= kMaximumHolsterSlots; ++slot) {
			if (holsteredWeapNames[slot] == "Empty") {
				continue;
			}
			const WeaponStack stack = FindStoredStack(holsteredWeapNames[slot]);
			// A throwable is drawn as a grab, never equipped.
			if (stack.found && stack.form && stack.stackID >= 0 && !IsThrowableWeapon(stack.weapon)) {
				weapons[count++] = { stack.form->formID, static_cast<std::uint32_t>(stack.stackID) };
			}
		}
		if (Rock::SetWarmWeapons(weapons.data(), count)) {
			for (UInt32 slot = 1; slot <= kMaximumHolsterSlots; ++slot) {
				s_sentNames[slot] = holsteredWeapNames[slot];
			}
		}
	}

	bool RequestDraw(HandTransfer& transfer)
	{
		const WeaponStack stored = FindStoredStack(transfer.name);
		if (!stored.found) {
			return false;
		}
		if (Rock::IsBound()) {
			std::uint64_t command = 0;
			const auto result = Rock::RequestEquip(
				transfer.isLeft, stored.form->formID, static_cast<std::uint32_t>(stored.stackID), command);
			if (result == Rock::Result::kBusy) {
				return true;  // retried next frame until the deadline
			}
			if (result != Rock::Result::kQueued) {
				return false;
			}
			transfer.command = command;
			transfer.requested = true;
			return true;
		}
		transfer.requested = NativeEquip(stored);
		return transfer.requested;
	}

	// The stored weapon goes into the hand. With swap, the hand's weapon
	// takes the slot once the stored one is in the hand.
	bool BeginDraw(bool isLeft, UInt32 slot, const HandWeaponFacts& facts, bool swap)
	{
		auto& transfer = GetHand(isLeft).transfer;
		transfer = {};
		transfer.active = true;
		transfer.isLeft = isLeft;
		transfer.slot = slot;
		transfer.name = holsteredWeapNames[slot];
		transfer.deadline = Clock::now() + kDrawTimeout;
		const WeaponStack stored = FindStoredStack(transfer.name);
		if (!stored.found) {
			transfer = {};
			ReconcileHolsterTags(BusySlots());
			return false;
		}
		transfer.formId = stored.form->formID;

		if (swap) {
			const WeaponStack outgoing = FindHandStack(facts);
			if (!outgoing.found ||
				(outgoing.name != transfer.name && !CanTagAnother(outgoing.name, true))) {
				transfer = {};
				return false;
			}
			transfer.outgoingName = outgoing.name;
			transfer.outgoingNameLength = outgoing.nameLength;
			NiNode* model = nullptr;
			NiTransform world{};
			if (gShowWeapons && CaptureHeldWeapon(facts, model, world)) {
				auto* record = new MyWeapon(
					FindNode(gInPowerArmor ? PAWeaponBones[slot].c_str() : WeaponBones[slot].c_str()),
					HolsterNames[slot].c_str(), outgoing.melee, outgoing.form->GetFullName(), outgoing.form->formID);
				const std::string nodeName = HolsterNames[slot] + "Swap";
				if (record->wbone && attachWeaponClone(*record, model, slot, nodeName.c_str(), &world)) {
					record->WeapMesh->flags |= 0x1;
					record->blending = false;
					transfer.outgoing = record;
					transfer.outgoingHidden = true;
					transfer.outgoingIndex = facts.nativeIndex;
					transfer.outgoingFormId = facts.formId;
				} else {
					delete record;
				}
			}
		}

		if (!RequestDraw(transfer)) {
			releaseWeaponClone(transfer.outgoing);
			_WARNING("Holster %u draw refused for '%s'", slot, transfer.name.c_str());
			transfer = {};
			return false;
		}
		if (gShowWeapons) {
			TakeDisplayIntoHand(transfer);
		}
		return true;
	}

	void AbortTransfer(HandTransfer& transfer, const char* reason)
	{
		_WARNING("Holster %u draw of '%s' into the %s hand ended without the weapon: %s",
			transfer.slot, transfer.name.c_str(), transfer.isLeft ? "left" : "right", reason);
		if (transfer.command) {
			Rock::CancelEquip(transfer.command);
		}
		releaseWeaponClone(transfer.outgoing);
		ReturnDisplayToHolster(transfer);
		const UInt32 slot = transfer.slot;
		transfer = {};
		// The weapon stays holstered; restore a display hidden early.
		if (gShowWeapons && holsteredWeapNames[slot] != "Empty" && !SlotDisplay(slot)) {
			QueueStoredVisual(FindStoredStack(holsteredWeapNames[slot]), slot);
		}
	}

	// While the outgoing weapon is still in the hand, its hidden clone
	// starts from wherever the hand holds it this frame.
	void TrackOutgoing(HandTransfer& transfer)
	{
		if (!transfer.outgoing || !transfer.outgoingHidden || !transfer.outgoing->WeapMesh) {
			return;
		}
		HandWeaponFacts facts{};
		facts.known = true;
		facts.holds = true;
		facts.nativeIndex = transfer.outgoingIndex;
		facts.formId = transfer.outgoingFormId;
		NiNode* model = nullptr;
		NiTransform world{};
		NiTransform start{};
		if (CaptureHeldWeapon(facts, model, world) &&
			LocalFromWorld(transfer.outgoing->wbone->m_worldTransform, world, start)) {
			transfer.outgoing->blendFrom = start;
			transfer.outgoing->WeapMesh->m_localTransform = start;
		}
	}

	void RevealOutgoing(HandTransfer& transfer)
	{
		if (!transfer.outgoing || !transfer.outgoingHidden || !transfer.outgoing->WeapMesh) {
			return;
		}
		transfer.outgoingHidden = false;
		transfer.outgoing->WeapMesh->flags &= ~0x1;
		transfer.outgoing->blendElapsed = 0.0F;
		transfer.outgoing->blendDuration = kHolsterBlendSeconds;
		transfer.outgoing->blending = true;
	}

	void CompleteTransfer(HandTransfer& transfer)
	{
		RevealOutgoing(transfer);
		const UInt32 slot = transfer.slot;
		const bool standIn = transfer.inHand != nullptr;
		const bool hadDisplay = standIn || SlotDisplay(slot) != nullptr;
		if (standIn) {
			playSwordSFX(*transfer.inHand);
		}
		HandOffStandIn(transfer);
		nDestoryWeapon(slot, true, hadDisplay && !standIn);
		if (transfer.outgoing || !transfer.outgoingName.empty()) {
			SetHolsterTag(slot, transfer.outgoingName, transfer.outgoingNameLength);
			if (transfer.outgoing && transfer.outgoing->WeapMesh) {
				transfer.outgoing->WeapMesh->m_name = HolsterNames[slot].c_str();
				WeaponRegisteredObjects[slot] = transfer.outgoing;
				VisualArt[slot] = transfer.outgoing->WeaponName.c_str();
				lastInHolster[slot] = transfer.outgoing->ThisForm;
				attachHolsterArt(*transfer.outgoing, slot);
				transfer.outgoing = nullptr;
			} else if (gShowWeapons) {
				releaseWeaponClone(transfer.outgoing);
				QueueStoredVisual(FindStoredStack(transfer.outgoingName), slot);
			}
		} else {
			resetHolsterName(slot);
		}
		PulseHand(transfer.isLeft);
		_MESSAGE("Holster %u drew '%s' into the %s hand", slot, transfer.name.c_str(), transfer.isLeft ? "left" : "right");
		transfer = {};
	}

	// The holster display hides on the frame the weapon is presented in
	// the hand; the tag clears only once the equip has succeeded.
	void HidePresentedDisplay(const HandTransfer& transfer)
	{
		if (transfer.inHand) {
			return;  // the stand-in gives way once the real weapon shows
		}
		const auto registered = WeaponRegisteredObjects.find(transfer.slot);
		if (registered != WeaponRegisteredObjects.end()) {
			nDestoryWeapon(transfer.slot, true, true);
		}
	}

	static void ServiceTransfer(HandTransfer& transfer, Clock::time_point now)
	{
		if (!transfer.active) {
			return;
		}
		// The equip was not accepted yet: ask again until the deadline.
		if (!transfer.requested) {
			if (now > transfer.deadline) {
				AbortTransfer(transfer, "the equip was never admitted");
			} else if (!RequestDraw(transfer)) {
				AbortTransfer(transfer, "the equip was refused");
			}
			return;
		}
		// Hide the holster's copy as soon as the real weapon shows in the hand.
		if (transfer.inHand && transfer.inHand->WeapMesh && RealWeaponVisible(transfer.isLeft, transfer.formId)) {
			transfer.inHand->WeapMesh->flags |= 0x1;
		}
		if (Rock::IsBound()) {
			std::uint32_t weaponSlot = 0;
			const Rock::EquipProgress progress = Rock::PollEquip(transfer.command, &weaponSlot);
			if (progress == Rock::EquipProgress::kPending) {
				TrackOutgoing(transfer);
			} else if (progress == Rock::EquipProgress::kPresented) {
				HidePresentedDisplay(transfer);
				RevealOutgoing(transfer);
			} else if (progress == Rock::EquipProgress::kSucceeded) {
				transfer.weaponSlot = weaponSlot;
				transfer.weaponSlotKnown = true;
				CompleteTransfer(transfer);
				return;
			} else if (progress == Rock::EquipProgress::kFailed) {
				transfer.command = 0;
				AbortTransfer(transfer, "ROCK reported the equip failed");
				return;
			}
		} else {
			TrackOutgoing(transfer);
			const auto equipped = NativeEquipment::ReadEquippedWeapons()[0];
			if (equipped.formId == transfer.formId) {
				CompleteTransfer(transfer);
				return;
			}
		}
		// An equip ROCK accepted ends with ROCK's own result (the game may
		// already have equipped it, so it cannot be cancelled). The deadline
		// only applies without ROCK.
		if (transfer.active && !Rock::IsBound() && now > transfer.deadline) {
			AbortTransfer(transfer, "timed out");
		}
	}

	void ServiceTransfers()
	{
		const auto now = Clock::now();
		ServiceTransfer(gRightHand.transfer, now);
		ServiceTransfer(gLeftHand.transfer, now);
	}

	static void CancelTransfer(HandTransfer& transfer)
	{
		if (!transfer.active) {
			return;
		}
		if (transfer.command) {
			Rock::CancelEquip(transfer.command);
		}
		releaseWeaponClone(transfer.outgoing);
		releaseWeaponClone(transfer.inHand);
		transfer = {};
	}

	void CancelTransfers()
	{
		CancelTransfer(gRightHand.transfer);
		CancelTransfer(gLeftHand.transfer);
	}

	void AdvanceBlend(MyWeapon& weapon)
	{
		if (!weapon.blending || !weapon.WeapMesh) {
			weapon.blending = false;
			return;
		}
		weapon.blendElapsed += gFrameSeconds;
		const float linear = weapon.blendDuration > 0.0F ?
			(std::min)(1.0F, weapon.blendElapsed / weapon.blendDuration) :
			1.0F;
		const float eased = linear * linear * (3.0F - 2.0F * linear);
		weapon.WeapMesh->m_localTransform = linear >= 1.0F ?
			weapon.blendTo :
			BlendTransforms(weapon.blendFrom, weapon.blendTo, eased);
		if (linear >= 1.0F) {
			weapon.blending = false;
		}
	}

	// A drawn firearm stand-in eases from where it was grabbed into ROCK's
	// firing aim for the hand, then keeps following it. Without an aim it
	// holds still. ROCK usually reveals the real weapon before the ease
	// ends, so each frame tells ROCK where the stand-in is and the real
	// weapon continues from there instead of appearing at the aim.
	void AdvanceStandIn(MyWeapon& weapon, bool isLeft, std::uint64_t command)
	{
		NiNode* hand = HandNode(isLeft);
		Rock::WeaponWorld aim{};
		bool placed = false;
		if (weapon.followsAim && weapon.WeapMesh && hand && Rock::GetFiringAimWorld(isLeft, aim)) {
			NiTransform world{};
			for (int row = 0; row < 3; ++row) {
				for (int column = 0; column < 3; ++column) {
					world.rot.data[row][column] = aim.rotate[row * 3 + column];
				}
				world.rot.data[row][3] = 0.0F;
			}
			world.pos = NiPoint3(aim.translate[0], aim.translate[1], aim.translate[2]);
			// The aim carries no model scale; the clone keeps its own.
			world.scale = weapon.WeapMesh->m_worldTransform.scale;
			NiTransform local{};
			if (LocalFromWorld(hand->m_worldTransform, world, local)) {
				weapon.blendTo = local;
				if (!weapon.blending) {
					weapon.WeapMesh->m_localTransform = local;
					placed = true;
				}
			}
		}
		if (!placed) {
			AdvanceBlend(weapon);
		}
		if (command && weapon.followsAim && weapon.WeapMesh && hand && (weapon.WeapMesh->flags & 0x1) == 0) {
			const NiTransform world = WorldFromLocal(hand->m_worldTransform, weapon.WeapMesh->m_localTransform);
			Rock::WeaponWorld shown{};
			for (int row = 0; row < 3; ++row) {
				for (int column = 0; column < 3; ++column) {
					shown.rotate[row * 3 + column] = world.rot.data[row][column];
				}
			}
			shown.translate[0] = world.pos.x;
			shown.translate[1] = world.pos.y;
			shown.translate[2] = world.pos.z;
			shown.scale = world.scale;
			(void)Rock::ReportDrawPose(command, shown);
		}
	}

	static void AdvanceTransferDisplays(HandTransfer& transfer)
	{
		if (transfer.outgoing) {
			AdvanceBlend(*transfer.outgoing);
		}
		if (transfer.inHand) {
			AdvanceStandIn(*transfer.inHand, transfer.isLeft, transfer.requested ? transfer.command : 0);
		}
	}

	void UpdateWeaponBlends()
	{
		for (auto& [slot, weapon] : WeaponRegisteredObjects) {
			(void)slot;
			if (weapon) {
				AdvanceBlend(*weapon);
			}
		}
		AdvanceTransferDisplays(gRightHand.transfer);
		AdvanceTransferDisplays(gLeftHand.transfer);
		if (gRightHand.handoff.clone) {
			AdvanceStandIn(*gRightHand.handoff.clone, false, gRightHand.handoff.command);
		}
		if (gLeftHand.handoff.clone) {
			AdvanceStandIn(*gLeftHand.handoff.clone, true, gLeftHand.handoff.command);
		}
	}

	void FinishWeaponBlends()
	{
		for (auto& [slot, weapon] : WeaponRegisteredObjects) {
			(void)slot;
			if (weapon && weapon->blending && weapon->WeapMesh) {
				weapon->WeapMesh->m_localTransform = weapon->blendTo;
				weapon->blending = false;
			}
		}
	}
}
