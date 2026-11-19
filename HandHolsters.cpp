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
	// ---- Hands, haptics and the game's own grab ----

	// The game's own grab distances, saved while they are switched off.
	struct GrabSettingSnapshot
	{
		float primaryDistance = 0.0F;
		float secondaryDistance = 0.0F;
		float floorMultiplier = 0.0F;
		bool primaryCaptured = false;
		bool secondaryCaptured = false;
		bool floorCaptured = false;
	};

	HandState gLeftHand;
	HandState gRightHand;

	HandState& GetHand(bool isLeft)
	{
		return isLeft ? gLeftHand : gRightHand;
	}

	static GrabSettingSnapshot s_grabSettingSnapshot;
	static bool s_activationSuppressed = false;

	void PulseHand(bool isLeft)
	{
		if (ghapticsEnabled && VRHook::g_vrHook) {
			VRHook::g_vrHook->StartHaptics(isLeft ? 1 : 2, kHolsterHapticSeconds, c_hapticIntensity);
		}
	}

	void SendActivationEvent(bool suppress)
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

	// With c_ActivationDisabled, the game's grab and activate are switched off
	// while a hand works a holster and switched back on afterwards.
	void SetActivationSuppressed(bool suppress)
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

	// ---- What is in a slot and in a hand ----

	// True when the slot holds a grenade or mine. The inventory is only
	// looked up when the slot's weapon changes, not every frame.
	bool SlotHoldsThrowable(UInt32 slot)
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

	// KabutoVR: no draw while the hand is at the head, holds a helmet or puts
	// one on. Only asked when a draw is about to start.
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

	HandWeaponFacts GetHandWeaponFacts(bool isLeft)
	{
		HandWeaponFacts facts{};
		if (Rock::IsBound()) {
			// With ROCK loaded, ROCK says which hand holds what. Do nothing
			// while ROCK is not running (menus, loading).
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
		// Without ROCK the game has one weapon, in the weapon hand. Drawing
		// with the other hand would replace it.
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

	// ---- Deciding what a press does ----

	static bool HandWorksSlot(const HandState& hand, UInt32 slot)
	{
		return (hand.transfer.active && hand.transfer.slot == slot) ||
			(hand.throwableDraw.active && hand.throwableDraw.slot == slot);
	}

	bool SlotHasTransfer(UInt32 slot)
	{
		return HandWorksSlot(gRightHand, slot) || HandWorksSlot(gLeftHand, slot);
	}

	std::array<bool, 8> BusySlots()
	{
		std::array<bool, 8> busy{};
		for (UInt32 slot = 0; slot < busy.size(); slot++) {
			busy[slot] = SlotHasTransfer(slot);
		}
		return busy;
	}

	HolsterAction DecideAction(Interaction::GestureActionKind kind, UInt32 slot, bool isLeft, const HandWeaponFacts& facts)
	{
		const HandState& hand = GetHand(isLeft);
		if (!facts.known || !SlotAvailable(slot) ||
			hand.transfer.active || hand.throwableDraw.active || SlotHasTransfer(slot)) {
			return HolsterAction_None;
		}
		const bool occupied = holsteredWeapNames[slot] != "Empty";
		const bool handFree = !facts.holds && facts.canReceive;

		// A grenade comes out only through ROCK and only into a free hand.
		// A held weapon is never swapped for it.
		if (occupied && SlotHoldsThrowable(slot)) {
			if (kind != Interaction::GestureActionKind::kNone && Rock::IsActive() && handFree) {
				return HolsterAction_Draw;
			}
			return HolsterAction_None;
		}

		if (kind == Interaction::GestureActionKind::kPressDraw) {
			if (occupied && handFree) {
				return HolsterAction_Draw;
			}
			return HolsterAction_None;
		}

		if (kind == Interaction::GestureActionKind::kReleaseInZone) {
			if (facts.holds) {
				// A weapon held in both hands is never holstered: the
				// release only lets go of it.
				if (facts.supported) {
					return HolsterAction_None;
				}
				if (occupied) {
					return HolsterAction_Swap;
				}
				return HolsterAction_Holster;
			}
			if (occupied && facts.canReceive) {
				return HolsterAction_Draw;
			}
		}
		return HolsterAction_None;
	}

	// ---- Holstering the hand's weapon ----

	// Finds the hand's weapon model and where it is on screen. The second
	// (off hand) weapon is placed by ROCK late in the frame, so its position
	// is asked from ROCK.
	bool CaptureHeldWeapon(const HandWeaponFacts& facts, NiNode*& model, NiTransform& world)
	{
		model = static_cast<NiNode*>(
			NativeEquipment::FindEquippedModel(facts.nativeIndex, facts.formId));
		Rock::WeaponWorld placed{};
		if (model && facts.nativeIndex == 1 && Rock::IsActive() && Rock::GetWeaponWorld(1, placed)) {
			world = ToNiTransform(placed);
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

	// Puts the hand's weapon into the empty slot. The holster's copy starts
	// where the hand held the weapon and slides into the holster, so the
	// weapon never vanishes in between.
	bool HolsterHandWeapon(bool isLeft, UInt32 slot, const HandWeaponFacts& facts)
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

	// ---- Loose weapons (picked up with ROCK) into a holster ----

	TESObjectREFR* LookupReference(std::uint32_t formId)
	{
		TESForm* form = formId ? LookupFormByID(formId) : nullptr;
		return form ? DYNAMIC_CAST(form, TESForm, TESObjectREFR) : nullptr;
	}

	bool IsWeaponReference(std::uint32_t formId)
	{
		const TESObjectREFR* reference = LookupReference(formId);
		return reference && reference->baseForm && reference->baseForm->formType == kFormType_WEAP;
	}

	// Takes a weapon lying in the world into the inventory and the empty
	// slot. The holster's copy starts where the weapon was.
	bool DepositReference(UInt32 slot, TESObjectREFR* reference)
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

	// Runs every frame. While the hand holds a loose weapon inside an empty
	// holster, asks ROCK to reserve that weapon for us, so ROCK's own
	// shoulder stash or another mod cannot take it first.
	void UpdateDepositClaim(bool isLeft, const HandState& hand, bool held)
	{
		auto& deposit = GetHand(isLeft).deposit;
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
		// ROCK already let go (toggle grip lets go when the button is pressed):
		// keep the reservation while the button is still held in the slot.
		const bool pressStillInSlot = !holding && slotFree && slot == deposit.slot &&
			(held || hand.gesture.Phase() == Interaction::GesturePhase::kHeld);
		if (deposit.lease && !pressStillInSlot) {
			ReleaseDeposit(deposit);
		}
	}

	// The button was released in the reserved slot: store the loose weapon.
	// ServiceDeposits finishes it once ROCK has let go.
	bool StartDeposit(bool isLeft, UInt32 slot)
	{
		auto& deposit = GetHand(isLeft).deposit;
		if (!deposit.lease || deposit.committing || deposit.slot != slot) {
			return false;
		}
		deposit.committing = true;
		deposit.framesLeft = kDepositCommitFrames;
		return true;
	}

	static void ServiceDeposit(HandDeposit& deposit, bool isLeft)
	{
		if (!deposit.committing) {
			return;
		}
		const auto result = Rock::CommitClaim(deposit.lease);
		if (result == Rock::Result::kBusy && --deposit.framesLeft > 0) {
			return;  // ROCK is still letting go; try again next frame
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

	void ServiceDeposits()
	{
		ServiceDeposit(gRightHand.deposit, false);
		ServiceDeposit(gLeftHand.deposit, true);
	}

	void ReleaseDeposits()
	{
		ReleaseDeposit(gRightHand.deposit);
		ReleaseDeposit(gLeftHand.deposit);
	}

	// ---- Grenades and mines ----

	// Asks ROCK to put the grenade into the hand. If ROCK is busy, the
	// request is repeated next frame.
	bool RequestThrowable(ThrowableDraw& draw, bool isLeft)
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

	bool BeginThrowableDraw(bool isLeft, UInt32 slot)
	{
		const WeaponStack stored = FindStoredStack(holsteredWeapNames[slot]);
		if (!stored.found || !IsThrowableWeapon(stored.weapon)) {
			ReconcileHolsterTags(BusySlots());
			return false;
		}
		auto& draw = GetHand(isLeft).throwableDraw;
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

	// Waits for ROCK to finish the grenade grab. The slot keeps its tag
	// until none are left (ReconcileHolsterTags).
	static void ServiceThrowableDraw(ThrowableDraw& draw, bool isLeft, Clock::time_point now)
	{
		if (!draw.active) {
			return;
		}
		if (!draw.command) {
			if (now > draw.deadline || !RequestThrowable(draw, isLeft)) {
				_WARNING("Holster %u throwable draw of '%s' into the %s hand was never admitted",
					draw.slot, draw.name.c_str(), isLeft ? "left" : "right");
				draw = {};
			}
			return;
		}
		const Rock::EquipProgress progress = Rock::PollItemGrab(draw.command);
		if (progress == Rock::EquipProgress::kSucceeded) {
			PulseHand(isLeft);
			_MESSAGE("Holster %u drew '%s' into the %s hand", draw.slot, draw.name.c_str(), isLeft ? "left" : "right");
			draw = {};
			ReconcileHolsterTags(BusySlots());
		} else if (progress == Rock::EquipProgress::kFailed) {
			_WARNING("Holster %u throwable draw of '%s' into the %s hand failed in ROCK",
				draw.slot, draw.name.c_str(), isLeft ? "left" : "right");
			draw = {};
		}
	}

	void ServiceThrowableDraws()
	{
		const auto now = Clock::now();
		ServiceThrowableDraw(gRightHand.throwableDraw, false, now);
		ServiceThrowableDraw(gLeftHand.throwableDraw, true, now);
	}

	void CancelThrowableDraws()
	{
		Rock::CancelItemGrab(gRightHand.throwableDraw.command);
		gRightHand.throwableDraw = {};
		Rock::CancelItemGrab(gLeftHand.throwableDraw.command);
		gLeftHand.throwableDraw = {};
	}

	// After drawing a grenade: keeping the button held inside its holster
	// for kThrowableClearHold empties the holster.
	void ServiceThrowableHold(HandState& hand, bool isLeft, bool held)
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

	// ---- Keeping ROCK off the holster button ----

	// Tells ROCK to leave the hand alone while it works a holster: no grab,
	// no drop, no shoulder stash, and the holster button is ours.
	void ReserveRockInput(bool isLeft, const HandState& hand, bool actionPossible)
	{
		if (!Rock::IsBound()) {
			return;
		}
		// A loose weapon going into a shoulder holster must not go into
		// ROCK's own shoulder stash.
		const auto& deposit = GetHand(isLeft).deposit;
		if (deposit.lease && (deposit.slot == 1 || deposit.slot == 2)) {
			Rock::ReserveHand(isLeft, false, 0, true);
		}
		// A drawn grenade is ROCK's grabbed object: ROCK keeps the button, so
		// letting go throws it.
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
		// While the button is reserved, ROCK also blocks that hand's trigger.
		// After a draw the button is given back (only ROCK's grab stays off),
		// so the drawn weapon can fire with the grip still held.
		const bool drawTookPress = grip && phase == Interaction::GesturePhase::kConsumed;
		Rock::ReserveHand(
			isLeft,
			grip,
			drawTookPress ? 0 : mask,
			hand.zone == 1 || hand.zone == 2);
	}

	// ---- Buttons ----
	// VRButtonsMain calls UpdateHandGestures with what it read from the
	// controllers, then RunHandActions to do what the presses asked for.

	static bool s_gameplayEnabled = false;
	static HolsterButtonInput s_input;
	static HandWeaponFacts s_leftFacts;
	static HandWeaponFacts s_rightFacts;
	static Interaction::GestureDecision s_leftDecision;
	static Interaction::GestureDecision s_rightDecision;

	static bool DrawPossible(const HandState& hand, bool isLeft, const HandWeaponFacts& facts)
	{
		return DecideAction(Interaction::GestureActionKind::kPressDraw, hand.zone, isLeft, facts) != HolsterAction_None;
	}

	static bool ReleasePossible(const HandState& hand, bool isLeft, const HandWeaponFacts& facts)
	{
		return DecideAction(Interaction::GestureActionKind::kReleaseInZone, hand.zone, isLeft, facts) != HolsterAction_None;
	}

	// True when a press may draw right now.
	static bool CanDrawNow(const HandState& hand, bool isLeft,
		bool controllerAvailable, bool held, const HandWeaponFacts& facts)
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
	static void UpdatePressState(HandState& hand, bool isLeft, bool held, const HandWeaponFacts& facts)
	{
		const bool twoHandedNow = facts.supported || facts.supports;
		const bool twoHandedBefore = hand.twoHandedLastFrame;
		hand.twoHandedLastFrame = twoHandedNow;
		// pressTwoHanded: this press let go of a weapon held in both hands.
		// It is updated while the hand still grips and kept after it lets go,
		// until the next press.
		if (!held) {
			hand.pressFromZone = false;
			hand.pressDrewThrowable = false;
			hand.depositBlocked = false;
			return;
		}
		if (hand.gesture.Phase() == Interaction::GesturePhase::kIdle) {
			// With ROCK's toggle grip the hand is free one frame after the
			// press. A hand that was two-handed in the frame before counts
			// too, so the result does not depend on who reads the button first.
			hand.pressTwoHanded = twoHandedNow || (!facts.grips && twoHandedBefore);
			// Only a press that can do something in its holster keeps the
			// button away from ROCK. Any other press (letting go of a
			// two-handed weapon) stays ROCK's.
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
		const bool leftCanDraw = CanDrawNow(gLeftHand, true, input.leftControllerAvailable, input.leftHolsterHeld, s_leftFacts);
		const bool rightCanDraw = CanDrawNow(gRightHand, false, input.rightControllerAvailable, input.rightHolsterHeld, s_rightFacts);
		const auto leftPhaseBefore = gLeftHand.gesture.Phase();
		const auto rightPhaseBefore = gRightHand.gesture.Phase();
		UpdatePressState(gLeftHand, true, input.leftHolsterHeld, s_leftFacts);
		UpdatePressState(gRightHand, false, input.rightHolsterHeld, s_rightFacts);
		if (s_gameplayEnabled) {
			UpdateDepositClaim(true, gLeftHand, input.leftHolsterHeld);
			UpdateDepositClaim(false, gRightHand, input.rightHolsterHeld);
		} else {
			if (!gRightHand.deposit.committing) {
				ReleaseDeposit(gRightHand.deposit);
			}
			if (!gLeftHand.deposit.committing) {
				ReleaseDeposit(gLeftHand.deposit);
			}
		}
		s_leftDecision = gLeftHand.gesture.Update({
			input.leftControllerAvailable && gLeftHand.anchorAvailable,
			s_gameplayEnabled,
			input.leftHolsterHeld,
			gLeftHand.zone,
			leftCanDraw && !gLeftHand.pressTwoHanded
		});
		s_rightDecision = gRightHand.gesture.Update({
			input.rightControllerAvailable && gRightHand.anchorAvailable,
			s_gameplayEnabled,
			input.rightHolsterHeld,
			gRightHand.zone,
			rightCanDraw && !gRightHand.pressTwoHanded
		});

		TraceHandFrame(input, leftPhaseBefore, rightPhaseBefore, s_leftDecision, s_rightDecision, s_leftFacts, s_rightFacts);

		SetActivationSuppressed(
			c_ActivationDisabled &&
			s_gameplayEnabled &&
			(gLeftHand.gesture.IsHoldingCandidate() ||
				gRightHand.gesture.IsHoldingCandidate()));
		if (s_gameplayEnabled) {
			ReserveRockInput(true, gLeftHand,
				gLeftHand.zone != Interaction::kNoSlot &&
					(ReleasePossible(gLeftHand, true, s_leftFacts) || DrawPossible(gLeftHand, true, s_leftFacts)));
			ReserveRockInput(false, gRightHand,
				gRightHand.zone != Interaction::kNoSlot &&
					(ReleasePossible(gRightHand, false, s_rightFacts) || DrawPossible(gRightHand, false, s_rightFacts)));
		}
	}

	// Carries out what one hand's gesture decided this frame.
	static void RunHandAction(HandState& hand, bool isLeft, const Interaction::GestureDecision& decision)
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
		// Letting go of a two-handed weapon inside an occupied holster does
		// not draw.
		const bool twoHandedLetGo = decision.Kind() == Interaction::GestureActionKind::kReleaseInZone &&
			hand.pressTwoHanded;
		if (action == HolsterAction_Draw && twoHandedLetGo) {
			action = HolsterAction_None;
		}
		bool acted = false;
		switch (action) {
		case HolsterAction_Holster:
			acted = HolsterHandWeapon(isLeft, slot, facts);
			break;
		case HolsterAction_Draw: {
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
						Clock::now() - hand.trace.pressedAt).count()));
			}
			break;
		}
		case HolsterAction_Swap:
			if (!KabutoBlocksDraw()) {
				acted = BeginDraw(isLeft, slot, facts, true);
			}
			break;
		case HolsterAction_None:
			if (slot != Interaction::kNoSlot) {
				_MESSAGE("Holster %u %s by the %s hand did nothing: occupied=%d known=%d holds=%d twoHanded=%d twoHandedPress=%d canReceive=%d handBusy=%d slotBusy=%d",
					slot, decision.Kind() == Interaction::GestureActionKind::kPressDraw ? "press" : "release",
					isLeft ? "left" : "right", holsteredWeapNames[slot] != "Empty" ? 1 : 0,
					facts.known, facts.holds, facts.supported, twoHandedLetGo, facts.canReceive,
					GetHand(isLeft).transfer.active ? 1 : 0, SlotHasTransfer(slot) ? 1 : 0);
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
		RunHandAction(gLeftHand, true, s_leftDecision);
		RunHandAction(gRightHand, false, s_rightDecision);
		ServiceThrowableHold(gLeftHand, true, s_input.leftHolsterHeld);
		ServiceThrowableHold(gRightHand, false, s_input.rightHolsterHeld);
	}

	static void ResetPress(HandState& hand)
	{
		hand.gesture.Reset();
		hand.pressFromZone = false;
		hand.pressTwoHanded = false;
		hand.twoHandedLastFrame = false;
		hand.pressDrewThrowable = false;
		hand.throwableHoldSlot = 0;
		hand.depositBlocked = false;
	}

	void resetInteractionState() {
		ResetPress(gLeftHand);
		ResetPress(gRightHand);
		CancelTransfers();
		ReleaseHandoffs();
		ReleaseDeposits();
		CancelThrowableDraws();
		Rock::Reset();
		ClearHandContacts();
		SetActivationSuppressed(false);
	}
}
