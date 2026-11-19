#include "HandHolsters.h"
#include "HolsterTrace.h"
#include "utils.h"
#include "Offsets.h"
#include "VRHook.h"
#include "NativeEquipment.h"
#include "MenuChecker.h"
#include "InvSorting.h"
#include "KabutoAPI.h"
#include "f4se/NiNodes.h"
#include "f4se/GameReferences.h"

namespace Holsters {
	struct GrabSettingSnapshot
	{
		float primaryDistance{};
		float secondaryDistance{};
		float floorMultiplier{};
		bool primaryCaptured{};
		bool secondaryCaptured{};
		bool floorCaptured{};
	};

	HandInteractionRuntime s_leftHand;

	HandInteractionRuntime s_rightHand;

	GrabSettingSnapshot s_grabSettingSnapshot;

	bool s_activationSuppressed = false;

	std::array<HandTransfer, 2> s_transfers{};   // [0] right, [1] left

	std::array<HandHandoff, 2> s_handoffs{};     // [0] right, [1] left

	std::array<HandDeposit, 2> s_deposits{};     // [0] right, [1] left

	std::array<ThrowableDraw, 2> s_throwableDraws{};  // [0] right, [1] left

	HandTransfer& TransferFor(const bool isLeft)
	{
		return s_transfers[isLeft ? 1 : 0];
	}

	HandDeposit& DepositFor(const bool isLeft)
	{
		return s_deposits[isLeft ? 1 : 0];
	}

	ThrowableDraw& ThrowableDrawFor(const bool isLeft)
	{
		return s_throwableDraws[isLeft ? 1 : 0];
	}

	void PulseHand(const bool isLeft)
	{
		if (ghapticsEnabled && VRHook::g_vrHook) {
			VRHook::g_vrHook->StartHaptics(isLeft ? 1 : 2, kHolsterHapticSeconds, c_hapticIntensity);
		}
	}

	const HandInteractionRuntime& GetHandRuntime(
		const bool isLeft)
	{
		return isLeft ? s_leftHand : s_rightHand;
	}

	void SendActivationEvent(const bool suppress)
	{
		SInt32 eventType = suppress ?
			HolsterEvent_DisableActivate :
			HolsterEvent_EnableActivate;
		if (g_HolsterEventRegs.m_data.size() > 0) {
			auto dispatch = [&eventType](
								const EventRegistration<NullParameters>& reg) {
				SendPapyrusEvent2<SInt32, UInt32>(
					reg.handle,
					reg.scriptName,
					HolsterEventName,
					eventType,
					gCurHolster);
			};
			g_HolsterEventRegs.ForEach(dispatch);
		}
	}

	void CaptureAndDisableGrabSettings()
	{
		s_grabSettingSnapshot = {};

		if (auto* setting =
				GetINISetting("fPrimaryWandMaxGrabDistance:VRWand")) {
			s_grabSettingSnapshot.primaryDistance = setting->data.f32;
			s_grabSettingSnapshot.primaryCaptured = true;
			setting->SetDouble(0.0);
		}
		if (auto* setting =
				GetINISetting("fSecondaryWandMaxGrabDistance:VRWand")) {
			s_grabSettingSnapshot.secondaryDistance = setting->data.f32;
			s_grabSettingSnapshot.secondaryCaptured = true;
			setting->SetDouble(0.0);
		}
		if (auto* setting = GetINISetting(
				"fDownwardGrabFloorDistanceMultiplier:VRWand")) {
			s_grabSettingSnapshot.floorMultiplier = setting->data.f32;
			s_grabSettingSnapshot.floorCaptured = true;
			setting->SetDouble(0.0);
		}
	}

	void RestoreGrabSettings()
	{
		if (s_grabSettingSnapshot.primaryCaptured) {
			if (auto* setting =
					GetINISetting("fPrimaryWandMaxGrabDistance:VRWand")) {
				setting->SetDouble(
					s_grabSettingSnapshot.primaryDistance);
			}
		}
		if (s_grabSettingSnapshot.secondaryCaptured) {
			if (auto* setting = GetINISetting(
					"fSecondaryWandMaxGrabDistance:VRWand")) {
				setting->SetDouble(
					s_grabSettingSnapshot.secondaryDistance);
			}
		}
		if (s_grabSettingSnapshot.floorCaptured) {
			if (auto* setting = GetINISetting(
					"fDownwardGrabFloorDistanceMultiplier:VRWand")) {
				setting->SetDouble(
					s_grabSettingSnapshot.floorMultiplier);
			}
		}
		s_grabSettingSnapshot = {};
	}

	void SetActivationSuppressed(const bool suppress)
	{
		if (suppress == s_activationSuppressed) {
			return;
		}

		if (suppress) {
			CaptureAndDisableGrabSettings();
		} else {
			RestoreGrabSettings();
		}
		SendActivationEvent(suppress);
		s_activationSuppressed = suppress;
	}

	// Whether the slot's tagged weapon is a throwable. The inventory is
	// read only when the slot's tag changes, never per frame.
	bool SlotHoldsThrowable(const UInt32 slot)
	{
		static std::array<std::string, kMaximumHolsterSlots + 1> s_names{};
		static std::array<bool, kMaximumHolsterSlots + 1> s_throwable{};
		if (slot < 1 || slot > kMaximumHolsterSlots) {
			return false;
		}
		if (s_names[slot] != holsteredWeapNames[slot]) {
			s_names[slot] = holsteredWeapNames[slot];
			const WeaponStack stack = FindStoredStack(s_names[slot]);
			s_throwable[slot] = stack.found && IsThrowableWeapon(stack.weapon);
		}
		return s_throwable[slot];
	}

	// KabutoVR owns the hand while it is at the head or holds or equips a
	// helmet; no holster draw then. Read on a draw only, never per frame.
	bool KabutoBlocksDraw()
	{
		KabutoAPI* kabuto = RequestKabutoAPI();
		if (!kabuto || kabuto->GetVersion() < 1) {
			return false;
		}
		const bool helmetZone = kabuto->IsHandHelmetZone();
		const bool holdingHelmet = kabuto->IsHoldingHelmetinPrimary();
		const bool equipping = kabuto->IsEquippingHelmet();
		if (helmetZone || holdingHelmet || equipping) {
			_MESSAGE("Holster draw withheld for KabutoVR: helmetZone=%d holdingHelmet=%d equipping=%d",
				helmetZone ? 1 : 0, holdingHelmet ? 1 : 0, equipping ? 1 : 0);
			return true;
		}
		return false;
	}

	HandWeaponFacts GetHandWeaponFacts(const bool isLeft)
	{
		HandWeaponFacts facts{};
		if (Rock::IsBound()) {
			// ROCK owns hand assignment; act only on its current publication.
			if (!Rock::IsActive()) {
				return facts;
			}
			const auto weapon = Rock::GetHandWeapon(isLeft);
			facts.known = true;
			facts.holds = weapon.role == Rock::HandRole::kFiring &&
				weapon.formId != 0 && weapon.slot <= 1;
			facts.canReceive = weapon.role != Rock::HandRole::kSupport &&
				!Rock::HandBusyWithObject(isLeft);
			facts.nativeIndex = facts.holds ? weapon.slot : 0;
			facts.formId = facts.holds ? weapon.formId : 0;
			const auto other = Rock::GetHandWeapon(!isLeft);
			facts.supported = facts.holds && other.role == Rock::HandRole::kSupport && other.slot == weapon.slot;
			// A support grip alone (the firing hand detached) is a one-hand hold.
			facts.supports = weapon.role == Rock::HandRole::kSupport &&
				other.role == Rock::HandRole::kFiring && other.slot == weapon.slot;
			facts.grips = facts.holds || weapon.role == Rock::HandRole::kSupport;
			return facts;
		}
		if (!*g_player || !*g_gameVM || !(*g_gameVM)->m_virtualMachine) {
			return facts;
		}
		// Vanilla FO4VR holds one weapon, in the configured weapon hand; a
		// draw into the other hand would replace it.
		facts.known = true;
		const bool weaponHand = isLeft == !gIsRightHanded;
		TESObjectWEAP* drawn = (*g_player)->actorState.IsWeaponDrawn() ?
			Offsets::GetEquippedWeapon((*g_gameVM)->m_virtualMachine, 0, *g_player, 0) :
			nullptr;
		facts.holds = weaponHand && drawn;
		facts.canReceive = weaponHand || !drawn;
		facts.formId = drawn ? drawn->formID : 0;
		return facts;
	}

	bool SlotHasTransfer(const UInt32 slot)
	{
		for (const auto& transfer : s_transfers) {
			if (transfer.active && transfer.slot == slot) {
				return true;
			}
		}
		for (const auto& draw : s_throwableDraws) {
			if (draw.active && draw.slot == slot) {
				return true;
			}
		}
		return false;
	}

	std::array<bool, 8> BusySlots()
	{
		std::array<bool, 8> busy{};
		for (const auto& transfer : s_transfers) {
			if (transfer.active && transfer.slot < busy.size()) {
				busy[transfer.slot] = true;
			}
		}
		for (const auto& draw : s_throwableDraws) {
			if (draw.active && draw.slot < busy.size()) {
				busy[draw.slot] = true;
			}
		}
		return busy;
	}

	HolsterAction DecideAction(
		const Interaction::GestureActionKind kind,
		const UInt32 slot,
		const bool isLeft,
		const HandWeaponFacts& facts)
	{
		if (!facts.known || !SlotAvailable(slot) ||
			TransferFor(isLeft).active || ThrowableDrawFor(isLeft).active || SlotHasTransfer(slot)) {
			return HolsterAction::kNone;
		}
		const bool occupied = holsteredWeapNames[slot] != "Empty";
		// A throwable comes out only through ROCK, into a free hand; a
		// held weapon is never swapped for it.
		if (occupied && SlotHoldsThrowable(slot)) {
			return kind != Interaction::GestureActionKind::kNone && Rock::IsActive() &&
					!facts.holds && facts.canReceive ?
				HolsterAction::kDraw :
				HolsterAction::kNone;
		}
		switch (kind) {
		case Interaction::GestureActionKind::kPressDraw:
			return occupied && !facts.holds && facts.canReceive ?
				HolsterAction::kDraw :
				HolsterAction::kNone;
		case Interaction::GestureActionKind::kReleaseInZone:
			if (facts.holds) {
				// A two-handed weapon is never stashed: the release is
				// ROCK's ordinary let-go.
				if (facts.supported) {
					return HolsterAction::kNone;
				}
				return occupied ? HolsterAction::kSwap : HolsterAction::kHolster;
			}
			return occupied && facts.canReceive ?
				HolsterAction::kDraw :
				HolsterAction::kNone;
		case Interaction::GestureActionKind::kNone:
			break;
		}
		return HolsterAction::kNone;
	}

	// Where the hand's weapon model is on screen now. The first weapon's
	// model is rendered where its node stands. ROCK re-places the second
	// weapon's model late in the frame after the engine returns it to its
	// biped carry, so its last placement comes from ROCK (getGrip(1) is
	// that model's own world).
	bool CaptureHeldWeapon(const HandWeaponFacts& facts, NiNode*& model, NiTransform& world)
	{
		model = static_cast<NiNode*>(
			NativeEquipment::FindEquippedModel(facts.nativeIndex, facts.formId));
		Rock::WeaponWorld placed{};
		if (model && facts.nativeIndex == 1 && Rock::IsActive() && Rock::GetWeaponWorld(1, placed)) {
			for (int row = 0; row < 3; ++row) {
				for (int column = 0; column < 3; ++column) {
					world.rot.data[row][column] = placed.rotate[row * 3 + column];
				}
				world.rot.data[row][3] = 0.0F;
			}
			world.pos = NiPoint3(placed.translate[0], placed.translate[1], placed.translate[2]);
			world.scale = placed.scale;
			return model != nullptr;
		}
		if (model) {
			world = model->m_worldTransform;
		}
		return model != nullptr;
	}

	WeaponStack FindHandStack(const HandWeaponFacts& facts)
	{
		if (!facts.holds || facts.nativeIndex > 1) {
			return {};
		}
		const auto equipped = NativeEquipment::ReadEquippedWeapons()[facts.nativeIndex];
		if (equipped.formId != facts.formId) {
			_WARNING("Holster refused: hand weapon %08X is not native index %u (%08X)",
				facts.formId, facts.nativeIndex, equipped.formId);
			return {};
		}
		return FindEquippedStack(facts.formId, equipped.instance);
	}

	bool UnequipHandWeapon(const HandWeaponFacts& facts, const WeaponStack& stack)
	{
		if (Rock::IsBound()) {
			return Rock::UnequipWeapon(facts.nativeIndex, facts.formId) == Rock::Result::kOk;
		}
		return NativeUnequip(stack);
	}

	// The hand's weapon goes into the empty slot. The clone appears where
	// the hand held the weapon in the frame the weapon leaves the hand, so
	// it never disappears, then eases into the holster.
	bool HolsterHandWeapon(const bool isLeft, const UInt32 slot, const HandWeaponFacts& facts)
	{
		const WeaponStack stack = FindHandStack(facts);
		if (!stack.found) {
			_WARNING("Holster %u refused: the %s hand's weapon stack was not found", slot, isLeft ? "left" : "right");
			return false;
		}
		if (!CanTagAnother(stack.name, true)) {
			_MESSAGE("Holster %u refused: every '%s' is already holstered", slot, stack.name.c_str());
			return false;
		}
		NiNode* model = nullptr;
		NiTransform world{};
		const bool captured = CaptureHeldWeapon(facts, model, world);
		nDestoryWeapon(slot, true, false);
		if (gShowWeapons && captured) {
			nRegisterWeapon(stack.form->GetFullName(), stack.form, model, slot, stack.melee, &world);
		}
		const auto registered = WeaponRegisteredObjects.find(slot);
		const bool presented = registered != WeaponRegisteredObjects.end() &&
			registered->second && registered->second->WeapMesh;
		if (!UnequipHandWeapon(facts, stack)) {
			_WARNING("Holster %u refused: the %s hand's weapon %08X could not be unequipped",
				slot, isLeft ? "left" : "right", facts.formId);
			nDestoryWeapon(slot, true, false);
			return false;
		}
		SetHolsterTag(slot, stack.name, stack.nameLength);
		if (gShowWeapons && !presented) {
			QueueStoredVisual(FindStoredStack(stack.name), slot);
		}
		PulseHand(isLeft);
		_MESSAGE("Holster %u stored '%s' from the %s hand (index %u)",
			slot, stack.name.c_str(), isLeft ? "left" : "right", facts.nativeIndex);
		return true;
	}

	TESObjectREFR* LookupReference(const std::uint32_t formId)
	{
		TESForm* form = formId ? LookupFormByID(formId) : nullptr;
		return form ? DYNAMIC_CAST(form, TESForm, TESObjectREFR) : nullptr;
	}

	bool IsWeaponReference(const std::uint32_t formId)
	{
		const TESObjectREFR* reference = LookupReference(formId);
		return reference && reference->baseForm && reference->baseForm->formType == kFormType_WEAP;
	}

	// The reference's weapon goes into the empty slot: the display starts
	// where the reference is shown, then the item is taken into the
	// inventory and the slot is tagged with its inventory name.
	bool DepositReference(const UInt32 slot, TESObjectREFR* reference)
	{
		if (!reference || !SlotAvailable(slot) || holsteredWeapNames[slot] != "Empty" || SlotHasTransfer(slot)) {
			return false;
		}
		TESObjectWEAP* weapon = DYNAMIC_CAST(reference->baseForm, TESForm, TESObjectWEAP);
		if (!weapon) {
			return false;
		}
		if (IsThrowableWeapon(weapon) && !Rock::IsActive()) {
			_MESSAGE("Holster %u refused throwable %08X: only ROCK can draw it again", slot, weapon->formID);
			return false;
		}
		std::string name;
		UInt16 nameLength = 0;
		ReferenceTagName(reference, name, nameLength);
		if (name.empty() || !CanTagAnother(name, true)) {
			_MESSAGE("Holster %u refused reference %08X: every '%s' is already holstered",
				slot, reference->formID, name.c_str());
			return false;
		}
		NiAVObject* root = reference->unkF0 ? reference->unkF0->rootNode : nullptr;
		NiNode* model = root ? root->GetAsNiNode() : nullptr;
		nDestoryWeapon(slot, true, false);
		if (gShowWeapons && model) {
			const NiTransform world = model->m_worldTransform;
			nRegisterWeapon(weapon->GetFullName(), weapon, model, slot, IsMeleeWeapon(weapon), &world);
		}
		const MyWeapon* display = SlotDisplay(slot);
		const bool presented = display && display->WeapMesh;
		const std::uint32_t referenceId = reference->formID;
		if (!NativeEquipment::PickUpReference(referenceId)) {
			_WARNING("Holster %u refused reference %08X: the game did not take it into the inventory", slot, referenceId);
			nDestoryWeapon(slot, true, false);
			return false;
		}
		SetHolsterTag(slot, name, nameLength);
		if (gShowWeapons && !presented) {
			QueueStoredVisual(FindStoredStack(name), slot);
		}
		_MESSAGE("Holster %u stored '%s' from world reference %08X", slot, name.c_str(), referenceId);
		return true;
	}

	void ReleaseDeposit(HandDeposit& deposit)
	{
		Rock::ReleaseClaim(deposit.lease);
		deposit = {};
	}

	// Keeps the hand's claim on the loose weapon it holds while it is in
	// an empty holster. A claim outlives ROCK's let-go by ROCK's claim
	// reservation, which covers toggle grip letting go on the press.
	void UpdateDepositClaim(const bool isLeft, const HandInteractionRuntime& hand, const bool held)
	{
		auto& deposit = DepositFor(isLeft);
		if (deposit.committing) {
			return;
		}
		Rock::LooseHold hold{};
		const bool holding = Rock::GetLooseHold(isLeft, hold) && IsWeaponReference(hold.referenceFormId);
		const UInt32 slot = hand.zone;
		const bool slotFree = slot != Interaction::kNoSlot && SlotAvailable(slot) &&
			holsteredWeapNames[slot] == "Empty" && !SlotHasTransfer(slot);
		if (holding && slotFree && !hand.depositBlocked) {
			if (deposit.lease && (deposit.referenceFormId != hold.referenceFormId || deposit.slot != slot)) {
				ReleaseDeposit(deposit);
			}
			std::uint64_t lease = 0;
			const auto result = Rock::ClaimHeld(isLeft, hold, lease);
			if (result == Rock::Result::kOk) {
				deposit.lease = lease;
				deposit.referenceFormId = hold.referenceFormId;
				deposit.slot = slot;
			} else if (result != Rock::Result::kBusy) {
				ReleaseDeposit(deposit);
			}
			return;
		}
		// ROCK let go first: keep the claim while the press is held (toggle
		// grip lets go on the press) and on the frame it is released.
		const bool pressStillInSlot = !holding && slotFree && slot == deposit.slot &&
			(held || hand.gesture.Phase() == Interaction::GesturePhase::kHeld);
		if (deposit.lease && !pressStillInSlot) {
			ReleaseDeposit(deposit);
		}
	}

	// A release in the claimed slot stores the loose weapon.
	bool StartDeposit(const bool isLeft, const UInt32 slot)
	{
		auto& deposit = DepositFor(isLeft);
		if (!deposit.lease || deposit.committing || deposit.slot != slot) {
			return false;
		}
		deposit.committing = true;
		deposit.framesLeft = kDepositCommitFrames;
		return true;
	}

	void ServiceDeposits()
	{
		for (const bool isLeft : { false, true }) {
			auto& deposit = DepositFor(isLeft);
			if (!deposit.committing) {
				continue;
			}
			const auto result = Rock::CommitClaim(deposit.lease);
			if (result == Rock::Result::kBusy && --deposit.framesLeft > 0) {
				continue;
			}
			if (result == Rock::Result::kOk) {
				if (DepositReference(deposit.slot, LookupReference(deposit.referenceFormId))) {
					gCurHolster = deposit.slot;
					PulseHand(isLeft);
				}
			} else {
				_WARNING("Holster %u deposit of reference %08X from the %s hand abandoned: ROCK %s",
					deposit.slot, deposit.referenceFormId, isLeft ? "left" : "right",
					result == Rock::Result::kBusy ? "kept holding it" : "refused the commit");
			}
			ReleaseDeposit(deposit);
		}
	}

	void ReleaseDeposits()
	{
		for (auto& deposit : s_deposits) {
			ReleaseDeposit(deposit);
		}
	}

	// A request ROCK reports busy is retried next frame until the deadline.
	bool RequestThrowable(ThrowableDraw& draw, const bool isLeft)
	{
		std::uint64_t command = 0;
		const auto result = Rock::RequestItemGrab(isLeft, draw.formId, command);
		if (result == Rock::Result::kBusy) {
			return true;
		}
		if (result != Rock::Result::kQueued) {
			return false;
		}
		draw.command = command;
		return true;
	}

	bool BeginThrowableDraw(const bool isLeft, const UInt32 slot)
	{
		const WeaponStack stored = FindStoredStack(holsteredWeapNames[slot]);
		if (!stored.found || !IsThrowableWeapon(stored.weapon)) {
			ReconcileHolsterTags(BusySlots());
			return false;
		}
		auto& draw = ThrowableDrawFor(isLeft);
		draw = {};
		draw.active = true;
		draw.slot = slot;
		draw.formId = stored.form->formID;
		draw.name = stored.name;
		draw.deadline = Clock::now() + kDrawTimeout;
		if (!RequestThrowable(draw, isLeft)) {
			_WARNING("Holster %u throwable draw refused for '%s'", slot, draw.name.c_str());
			draw = {};
			return false;
		}
		return true;
	}

	// ROCK's admitted grab ends with its own result. The slot keeps its
	// tag until no copy is left (ReconcileHolsterTags).
	void ServiceThrowableDraws()
	{
		const auto now = Clock::now();
		for (const bool isLeft : { false, true }) {
			auto& draw = ThrowableDrawFor(isLeft);
			if (!draw.active) {
				continue;
			}
			if (!draw.command) {
				if (now > draw.deadline || !RequestThrowable(draw, isLeft)) {
					_WARNING("Holster %u throwable draw of '%s' into the %s hand was never admitted",
						draw.slot, draw.name.c_str(), isLeft ? "left" : "right");
					draw = {};
				}
				continue;
			}
			switch (Rock::PollItemGrab(draw.command)) {
			case Rock::EquipProgress::kSucceeded:
				PulseHand(isLeft);
				_MESSAGE("Holster %u drew '%s' into the %s hand", draw.slot, draw.name.c_str(), isLeft ? "left" : "right");
				draw = {};
				ReconcileHolsterTags(BusySlots());
				break;
			case Rock::EquipProgress::kFailed:
				_WARNING("Holster %u throwable draw of '%s' into the %s hand failed in ROCK",
					draw.slot, draw.name.c_str(), isLeft ? "left" : "right");
				draw = {};
				break;
			default:
				break;
			}
		}
	}

	void CancelThrowableDraws()
	{
		for (auto& draw : s_throwableDraws) {
			Rock::CancelItemGrab(draw.command);
			draw = {};
		}
	}

	// A press that drew a throwable and stays in its holster empties it.
	void ServiceThrowableHold(HandInteractionRuntime& hand, const bool isLeft, const bool held)
	{
		if (!hand.throwableHoldSlot) {
			return;
		}
		if (!held || hand.zone != hand.throwableHoldSlot ||
			hand.gesture.Phase() != Interaction::GesturePhase::kConsumed) {
			hand.throwableHoldSlot = 0;
			return;
		}
		if (Clock::now() - hand.throwableHoldSince < kThrowableClearHold) {
			return;
		}
		const UInt32 slot = hand.throwableHoldSlot;
		hand.throwableHoldSlot = 0;
		if (holsteredWeapNames[slot] == "Empty" || !SlotHoldsThrowable(slot)) {
			return;
		}
		nDestoryWeapon(slot, true, false);
		resetHolsterName(slot);
		hand.depositBlocked = true;
		PulseHand(isLeft);
		_MESSAGE("Holster %u emptied by a held press of the %s hand", slot, isLeft ? "left" : "right");
	}

	// ROCK must not grab, drop, stash or act on the holster button with a
	// hand working a holster, including a press that has left the zone.
	void ReserveRockInput(const bool isLeft, const HandInteractionRuntime& hand, const bool actionPossible)
	{
		if (!Rock::IsBound()) {
			return;
		}
		// A loose weapon going into a shoulder holster must not go into
		// ROCK's own shoulder stash instead.
		const auto& deposit = DepositFor(isLeft);
		if (deposit.lease && (deposit.slot == 1 || deposit.slot == 2)) {
			Rock::ReserveHand(isLeft, false, 0, true);
		}
		// A drawn throwable is ROCK's grab: letting go of the button
		// throws it.
		if (hand.pressDrewThrowable) {
			return;
		}
		const auto phase = hand.gesture.Phase();
		const bool heldFromZone = hand.pressFromZone &&
			(phase == Interaction::GesturePhase::kHeld || phase == Interaction::GesturePhase::kConsumed);
		if (!actionPossible && !heldFromZone) {
			return;
		}
		const auto mask = vr::ButtonMaskFromId(static_cast<vr::EVRButtonId>(c_holsterButtonID));
		const bool grip = c_holsterButtonID == vr::EVRButtonId::k_EButton_Grip;
		// A held reserved chord withholds all of the hand's game input,
		// trigger included (ROCK effectiveHandInputSuppressionFlags). Once
		// a draw took the press, the grip stays off ROCK's grab but the
		// chord is dropped, so the drawn weapon fires with the grip held.
		const bool drawTookPress = grip && phase == Interaction::GesturePhase::kConsumed;
		Rock::ReserveHand(
			isLeft,
			grip,
			drawTookPress ? 0 : mask,
			hand.zone == 1 || hand.zone == 2);
	}

	// Buttons
	// VRButtonsMain calls UpdateHandGestures with what it read from the
	// controllers, then RunHandActions to act on what the gestures decided.

	static bool s_gameplayEnabled = false;
	static HolsterButtonInput s_input;
	static HandWeaponFacts s_leftFacts;
	static HandWeaponFacts s_rightFacts;
	static Interaction::GestureDecision s_leftDecision;
	static Interaction::GestureDecision s_rightDecision;

	static bool DrawPossible(const HandInteractionRuntime& hand, const bool isLeft, const HandWeaponFacts& facts)
	{
		return DecideAction(Interaction::GestureActionKind::kPressDraw, hand.zone, isLeft, facts) != HolsterAction::kNone;
	}

	static bool ReleasePossible(const HandInteractionRuntime& hand, const bool isLeft, const HandWeaponFacts& facts)
	{
		return DecideAction(Interaction::GestureActionKind::kReleaseInZone, hand.zone, isLeft, facts) != HolsterAction::kNone;
	}

	// A press draws on its frame or later in the same hold (the gesture
	// keeps it to the pressed slot).
	static bool CanDrawNow(const HandInteractionRuntime& hand, const bool isLeft,
		const bool controllerAvailable, const bool held, const HandWeaponFacts& facts)
	{
		const auto phase = hand.gesture.Phase();
		const bool drawPhase = phase == Interaction::GesturePhase::kIdle || phase == Interaction::GesturePhase::kHeld;
		return s_gameplayEnabled &&
			controllerAvailable &&
			hand.anchorAvailable &&
			held &&
			drawPhase &&
			DrawPossible(hand, isLeft, facts);
	}

	// What the current press of this hand's holster button is allowed to do.
	static void UpdatePressState(HandInteractionRuntime& hand, const bool isLeft, const bool held, const HandWeaponFacts& facts)
	{
		const bool twoHandedNow = facts.supported || facts.supports;
		const bool twoHandedBefore = hand.twoHandedLastFrame;
		hand.twoHandedLastFrame = twoHandedNow;
		// pressTwoHanded follows the hold while this hand grips and
		// freezes once it lets go: on the press with toggle grip, on
		// the release with hold grip. It outlives the release frame
		// (the release action reads it); each press sets it afresh.
		if (!held) {
			hand.pressFromZone = false;
			hand.pressDrewThrowable = false;
			hand.depositBlocked = false;
			return;
		}
		if (hand.gesture.Phase() == Interaction::GesturePhase::kIdle) {
			// ROCK's toggle grip lets go on the press and frees the hand
			// a frame later, which would make the slot drawable
			// mid-press. A hand already free on the press frame takes
			// the frame before, so this does not depend on whether
			// ROCK or this mod reads the press first.
			hand.pressTwoHanded = twoHandedNow || (!facts.grips && twoHandedBefore);
			// Only a press that can act in its zone keeps the button
			// from ROCK until release; any other press there (a
			// two-handed weapon's let-go) stays ROCK's.
			hand.pressFromZone = hand.zone != Interaction::kNoSlot &&
				(ReleasePossible(hand, isLeft, facts) ||
					(!hand.pressTwoHanded && DrawPossible(hand, isLeft, facts)));
		} else if (facts.grips) {
			hand.pressTwoHanded = twoHandedNow;
		}
	}

	void UpdateHandGestures(const HolsterButtonInput& input)
	{
		s_input = input;
		s_gameplayEnabled =
			!_isConfigModeActive &&
			!input.gameStopped &&
			!input.scopeOpen &&
			!_WeaponHolsterInProgress;
		s_leftFacts = GetHandWeaponFacts(true);
		s_rightFacts = GetHandWeaponFacts(false);
		const bool leftCanDraw = CanDrawNow(s_leftHand, true, input.leftControllerAvailable, input.leftHolsterHeld, s_leftFacts);
		const bool rightCanDraw = CanDrawNow(s_rightHand, false, input.rightControllerAvailable, input.rightHolsterHeld, s_rightFacts);
		const auto leftPhaseBefore = s_leftHand.gesture.Phase();
		const auto rightPhaseBefore = s_rightHand.gesture.Phase();
		UpdatePressState(s_leftHand, true, input.leftHolsterHeld, s_leftFacts);
		UpdatePressState(s_rightHand, false, input.rightHolsterHeld, s_rightFacts);
		if (s_gameplayEnabled) {
			UpdateDepositClaim(true, s_leftHand, input.leftHolsterHeld);
			UpdateDepositClaim(false, s_rightHand, input.rightHolsterHeld);
		} else {
			for (auto& deposit : s_deposits) {
				if (!deposit.committing) {
					ReleaseDeposit(deposit);
				}
			}
		}
		s_leftDecision = s_leftHand.gesture.Update({
			input.leftControllerAvailable && s_leftHand.anchorAvailable,
			s_gameplayEnabled,
			input.leftHolsterHeld,
			s_leftHand.zone,
			leftCanDraw && !s_leftHand.pressTwoHanded
		});
		s_rightDecision = s_rightHand.gesture.Update({
			input.rightControllerAvailable && s_rightHand.anchorAvailable,
			s_gameplayEnabled,
			input.rightHolsterHeld,
			s_rightHand.zone,
			rightCanDraw && !s_rightHand.pressTwoHanded
		});

		TraceHandFrame(input, leftPhaseBefore, rightPhaseBefore, s_leftDecision, s_rightDecision, s_leftFacts, s_rightFacts);

		SetActivationSuppressed(
			c_ActivationDisabled &&
			s_gameplayEnabled &&
			(s_leftHand.gesture.IsHoldingCandidate() ||
				s_rightHand.gesture.IsHoldingCandidate()));
		if (s_gameplayEnabled) {
			ReserveRockInput(true, s_leftHand,
				s_leftHand.zone != Interaction::kNoSlot &&
					(ReleasePossible(s_leftHand, true, s_leftFacts) || DrawPossible(s_leftHand, true, s_leftFacts)));
			ReserveRockInput(false, s_rightHand,
				s_rightHand.zone != Interaction::kNoSlot &&
					(ReleasePossible(s_rightHand, false, s_rightFacts) || DrawPossible(s_rightHand, false, s_rightFacts)));
		}
	}

	// Carries out what one hand's gesture decided this frame.
	static void RunHandAction(HandInteractionRuntime& hand, const bool isLeft, const Interaction::GestureDecision& decision)
	{
		if (!decision.HasAction()) {
			return;
		}
		// The left hand acts first and may have changed what the right hand
		// can reach this frame (a holster or swap), so the right hand reads
		// its weapon again.
		const HandWeaponFacts facts = isLeft ? s_leftFacts : GetHandWeaponFacts(false);
		const UInt32 slot = decision.ActionSlot();
		if (decision.Kind() == Interaction::GestureActionKind::kReleaseInZone && StartDeposit(isLeft, slot)) {
			return;
		}
		HolsterAction action = DecideAction(decision.Kind(), slot, isLeft, facts);
		// A two-handed let-go press that ends in an occupied zone
		// does not draw either: the hand is free by its release.
		const bool twoHandedLetGo = decision.Kind() == Interaction::GestureActionKind::kReleaseInZone &&
			hand.pressTwoHanded;
		if (action == HolsterAction::kDraw && twoHandedLetGo) {
			action = HolsterAction::kNone;
		}
		bool acted = false;
		switch (action) {
		case HolsterAction::kHolster:
			acted = HolsterHandWeapon(isLeft, slot, facts);
			break;
		case HolsterAction::kDraw: {
			if (KabutoBlocksDraw()) {
				break;
			}
			const bool onPress = decision.Kind() == Interaction::GestureActionKind::kPressDraw;
			if (SlotHoldsThrowable(slot)) {
				acted = BeginThrowableDraw(isLeft, slot);
				if (acted && onPress) {
					hand.pressDrewThrowable = true;
					hand.throwableHoldSlot = slot;
					hand.throwableHoldSince = Clock::now();
				}
			} else {
				acted = BeginDraw(isLeft, slot, facts, false);
			}
			if (acted) {
				_MESSAGE("Holster %u draw by the %s hand started on %s, %lld ms after the press",
					slot, isLeft ? "left" : "right", onPress ? "press" : "release",
					static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
						Clock::now() - hand.pressedAt).count()));
			}
			break;
		}
		case HolsterAction::kSwap:
			if (!KabutoBlocksDraw()) {
				acted = BeginDraw(isLeft, slot, facts, true);
			}
			break;
		case HolsterAction::kNone:
			if (slot != Interaction::kNoSlot) {
				_MESSAGE("Holster %u %s by the %s hand did nothing: occupied=%d known=%d holds=%d twoHanded=%d twoHandedPress=%d canReceive=%d handBusy=%d slotBusy=%d",
					slot, decision.Kind() == Interaction::GestureActionKind::kPressDraw ? "press" : "release",
					isLeft ? "left" : "right", holsteredWeapNames[slot] != "Empty" ? 1 : 0,
					facts.known, facts.holds, facts.supported, twoHandedLetGo, facts.canReceive,
					TransferFor(isLeft).active ? 1 : 0, SlotHasTransfer(slot) ? 1 : 0);
			}
			break;
		}
		if (acted) {
			gCurHolster = slot;
			if (c_ActivationDisabled) {
				StartMenuCheck();
			}
		}
	}

	void RunHandActions()
	{
		if (!s_gameplayEnabled) {
			return;
		}
		// Each hand acts on its own holster in the same frame; a
		// hand-directed draw leaves the other hand's weapon in place.
		RunHandAction(s_leftHand, true, s_leftDecision);
		RunHandAction(s_rightHand, false, s_rightDecision);
		ServiceThrowableHold(s_leftHand, true, s_input.leftHolsterHeld);
		ServiceThrowableHold(s_rightHand, false, s_input.rightHolsterHeld);
	}

	void resetInteractionState() {
		s_leftHand.gesture.Reset();
		s_rightHand.gesture.Reset();
		s_leftHand.pressFromZone = false;
		s_rightHand.pressFromZone = false;
		s_leftHand.pressTwoHanded = s_rightHand.pressTwoHanded = false;
		s_leftHand.twoHandedLastFrame = s_rightHand.twoHandedLastFrame = false;
		for (auto* hand : { &s_leftHand, &s_rightHand }) {
			hand->pressDrewThrowable = false;
			hand->throwableHoldSlot = 0;
			hand->depositBlocked = false;
		}
		CancelTransfers();
		ReleaseHandoffs();
		ReleaseDeposits();
		CancelThrowableDraws();
		Rock::Reset();
		ClearHandContacts();
		SetActivationSuppressed(false);
	}
}
