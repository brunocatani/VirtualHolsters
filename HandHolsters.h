#pragma once

// Per-hand holster interaction: each hand has its own holster zone, button
// press and draw, so both hands can holster and draw at the same time.
// HandHolsters.cpp decides what a press does, HolsterDraw.cpp carries a draw
// out over the following frames. docs/HowItWorks.md walks through one press.

#include "Holsters.h"
#include "HolsterInteraction.h"
#include "RockProvider.h"

#include <array>
#include <chrono>
#include <string>

namespace Holsters {
	constexpr std::size_t kMaximumHolsterSlots = 7;

	using Clock = std::chrono::steady_clock;

	// A holster draw waits this long for ROCK or the engine to put the
	// weapon in the hand before the slot keeps its weapon again.
	constexpr auto kDrawTimeout = std::chrono::milliseconds(2000);

	// A weapon put away from a hand eases into its holster pose.
	constexpr float kHolsterBlendSeconds = 0.18F;

	// A drawn stand-in eases into the learned grip pose this fast, and
	// gives way to the real weapon at the latest after this long.
	constexpr float kDrawBlendSeconds = 0.12F;

	constexpr auto kRevealWait = std::chrono::milliseconds(400);

	constexpr float kHolsterHapticSeconds = 0.03F;

	// A press that drew a throwable and stays in its holster this long
	// empties that holster.
	constexpr auto kThrowableClearHold = std::chrono::milliseconds(1500);

	// Frames a released loose weapon may take for ROCK to let go of it
	// before its holster deposit is abandoned.
	constexpr int kDepositCommitFrames = 45;

	// The second config-button press must follow the first this fast.
	constexpr auto kConfigDoublePress = std::chrono::milliseconds(300);

	struct HandInteractionRuntime
	{
		Interaction::HolsterGesture gesture{};
		UInt32 zone{};
		bool anchorAvailable{};
		// The current press started inside a zone; ROCK input stays
		// reserved for that hand until the button is released.
		bool pressFromZone{};
		// When the current press started (press diagnostics).
		Clock::time_point pressedAt{};
		// The other hand still held this hand's weapon when this hand let
		// go during the current press: the press never draws. A weapon held
		// by one hand, or a grabbed object, keeps drawing on let-go.
		bool pressTwoHanded{};
		bool twoHandedLastFrame{};
		// Input trace: the nearest enabled holster this frame and the
		// button state the trace last reported.
		UInt32 nearestSlot{ Interaction::kNoSlot };
		float nearestDistance{};
		float nearestRadius{};
		std::size_t zoneSamples{};
		bool heldLastFrame{};
		// Hand point the zone test used this frame (world), for the
		// press trace's position fields.
		NiPoint3 position{};
		// The current press drew a throwable: ROCK keeps the grab button,
		// so letting go throws it.
		bool pressDrewThrowable{};
		// That press's holster while the press stays there: held
		// kThrowableClearHold, it empties the holster.
		UInt32 throwableHoldSlot{};
		Clock::time_point throwableHoldSince{};
		// The current press emptied a throwable holster: its release
		// stores nothing.
		bool depositBlocked{};
	};

	// What a hand holds, from ROCK with ROCK bound, else the single
	// vanilla weapon in the configured weapon hand.
	struct HandWeaponFacts
	{
		bool known{};
		bool holds{};
		bool canReceive{};
		// The other hand supports the weapon this hand fires (two-handed).
		bool supported{};
		// This hand supports a weapon the other hand fires (two-handed).
		bool supports{};
		// This hand fires or supports an equipped weapon.
		bool grips{};
		UInt32 nativeIndex{};
		UInt32 formId{};
	};

	enum class HolsterAction : std::uint8_t
	{
		kNone,
		kHolster,
		kDraw,
		kSwap
	};

	// One running draw (or swap) per hand. The slot keeps its tag and
	// display until the weapon is in the hand, so a refused or failed equip
	// never empties a holster and the weapon never disappears in between.
	struct HandTransfer
	{
		bool active{};
		bool requested{};
		bool isLeft{};
		UInt32 slot{};
		UInt32 formId{};
		std::string name;
		std::uint64_t command{};
		Clock::time_point deadline{};
		// Swap: the weapon leaving the hand takes the slot on success. Its
		// clone stays hidden and follows the hand's model until the stored
		// weapon is presented, so the two never show at once.
		MyWeapon* outgoing{};
		std::string outgoingName;
		UInt16 outgoingNameLength{};
		bool outgoingHidden{};
		UInt32 outgoingIndex{};
		UInt32 outgoingFormId{};
		// The holster display, taken into the drawing hand on the frame of
		// the grab; it stands in for the weapon until the real one shows.
		MyWeapon* inHand{};
		// The slot ROCK equipped the weapon into, from its result.
		std::uint32_t weaponSlot{};
		bool weaponSlotKnown{};
	};

	// After the equip succeeds the stand-in stays in the hand until the
	// real weapon is visible there (ROCK reveals it once the native draw
	// pose is ready), so the hand is never empty.
	struct HandHandoff
	{
		MyWeapon* clone{};
		UInt32 formId{};
		Clock::time_point deadline{};
		// The draw's ROCK command: the stand-in's pose is reported under
		// it until ROCK reveals the weapon.
		std::uint64_t command{};
		// A second weapon is in a hand's role only once its grip holds
		// that hand, which can follow its presentation; its slot from the
		// equip result is checked directly.
		std::uint32_t weaponSlot{};
		bool weaponSlotKnown{};
	};

	// A loose weapon a hand holds through ROCK goes into the empty holster
	// the hand releases it in. While the hand holds it in that holster,
	// ROCK Storage keeps a claim on it (renewed each frame); the release
	// there commits the claim, and the item is taken once ROCK has let go.
	struct HandDeposit
	{
		std::uint64_t lease{};
		std::uint32_t referenceFormId{};
		UInt32 slot{};
		bool committing{};
		int framesLeft{};
	};

	// A holstered throwable drawn into a hand: ROCK puts one item of it
	// from the inventory into the hand as a grab. The holster keeps its
	// tag while any copy is left.
	struct ThrowableDraw
	{
		bool active{};
		UInt32 slot{};
		UInt32 formId{};
		std::string name;
		std::uint64_t command{};
		Clock::time_point deadline{};
	};

	// What VRButtonsMain read from the controllers this frame.
	struct HolsterButtonInput
	{
		bool leftControllerAvailable{};
		bool rightControllerAvailable{};
		bool leftHolsterHeld{};
		bool rightHolsterHeld{};
		// The game's own read of the holster button (button trace only).
		bool leftGameHeld{};
		bool rightGameHeld{};
		bool gameStopped{};
		bool scopeOpen{};
	};

	extern HandInteractionRuntime s_leftHand;
	extern HandInteractionRuntime s_rightHand;
	extern std::array<HandTransfer, 2> s_transfers;          // [0] right, [1] left
	extern std::array<HandHandoff, 2> s_handoffs;            // [0] right, [1] left
	extern std::array<HandDeposit, 2> s_deposits;            // [0] right, [1] left
	extern std::array<ThrowableDraw, 2> s_throwableDraws;    // [0] right, [1] left

	HandTransfer& TransferFor(bool isLeft);
	HandDeposit& DepositFor(bool isLeft);
	ThrowableDraw& ThrowableDrawFor(bool isLeft);
	const HandInteractionRuntime& GetHandRuntime(bool isLeft);

	// HandHolsters.cpp
	void UpdateHandGestures(const HolsterButtonInput& input);
	void RunHandActions();
	void PulseHand(bool isLeft);
	void SetActivationSuppressed(bool suppress);
	bool SlotHoldsThrowable(UInt32 slot);
	HandWeaponFacts GetHandWeaponFacts(bool isLeft);
	bool SlotHasTransfer(UInt32 slot);
	std::array<bool, 8> BusySlots();
	bool CaptureHeldWeapon(const HandWeaponFacts& facts, NiNode*& model, NiTransform& world);
	WeaponStack FindHandStack(const HandWeaponFacts& facts);
	bool DepositReference(UInt32 slot, TESObjectREFR* reference);
	void ServiceDeposits();
	void ReleaseDeposits();
	void ServiceThrowableDraws();
	void CancelThrowableDraws();

	// HolsterDraw.cpp
	bool BeginDraw(bool isLeft, UInt32 slot, const HandWeaponFacts& facts, bool swap);
	void ServiceTransfers();
	void CancelTransfers();
	void ServiceHandoffs();
	void ReleaseHandoffs();
	void ServiceWarmWeapons();
	void UpdateWeaponBlends();
	void FinishWeaponBlends();
}
