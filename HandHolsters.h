#pragma once

// Per-hand holstering.
//
// Each hand has its own holster zone, its own button press and its own draw,
// so both hands can holster and draw in the same frame.
//
//   HandHolsters.cpp   reads the holster button and decides what a press does:
//                      holster, draw, swap, store a loose weapon, draw a grenade.
//   HolsterDraw.cpp    carries a draw out over the next frames and moves the
//                      weapon shown in the holster into the hand.
//   HolsterTrace.cpp   log output only.
//
// docs/HowItWorks.md follows one button press through these files.

#include "Holsters.h"
#include "HolsterInteraction.h"
#include "RockProvider.h"

#include <array>
#include <chrono>
#include <string>

namespace Holsters {
	const std::size_t kMaximumHolsterSlots = 7;

	typedef std::chrono::steady_clock Clock;

	// Timings.
	// How long a draw waits for the weapon to reach the hand before it gives up.
	const auto kDrawTimeout = std::chrono::milliseconds(2000);
	// How long a weapon takes to slide from the hand into its holster pose.
	const float kHolsterBlendSeconds = 0.18F;
	// How long a drawn weapon takes to slide from the holster into the grip.
	const float kDrawBlendSeconds = 0.12F;
	// The longest the holster's copy stays in the hand after the equip finished.
	const auto kRevealWait = std::chrono::milliseconds(400);
	const float kHolsterHapticSeconds = 0.03F;
	// Holding the button this long in a grenade holster after drawing empties it.
	const auto kThrowableClearHold = std::chrono::milliseconds(1500);
	// Frames ROCK gets to let go of a loose weapon released into a holster.
	const int kDepositCommitFrames = 45;

	// What a hand holds right now. Filled by GetHandWeaponFacts: from ROCK when
	// ROCK is loaded, otherwise from the one vanilla weapon in the weapon hand.
	struct HandWeaponFacts
	{
		bool known = false;        // false: no data this frame, do nothing
		bool holds = false;        // this hand holds a weapon it can holster
		bool canReceive = false;   // a draw may go into this hand
		bool supported = false;    // the other hand also holds this hand's weapon (two-handed)
		bool supports = false;     // this hand is the second hand on the other hand's weapon
		bool grips = false;        // holds or supports
		UInt32 nativeIndex = 0;    // the game's equip index of the held weapon (0 or 1)
		UInt32 formId = 0;         // the held weapon's form
	};

	enum HolsterAction
	{
		HolsterAction_None,
		HolsterAction_Holster,   // hand weapon -> empty holster
		HolsterAction_Draw,      // holster weapon -> empty hand
		HolsterAction_Swap       // hand weapon and holster weapon change places
	};

	// A draw or swap that is running. The holster keeps its weapon until the
	// weapon is really in the hand, so a failed equip never empties a holster.
	struct HandTransfer
	{
		bool active = false;
		bool requested = false;    // the equip was accepted (by ROCK or the game)
		bool isLeft = false;
		UInt32 slot = 0;           // the holster being drawn from
		UInt32 formId = 0;         // the weapon being drawn
		std::string name;          // its holster tag
		std::uint64_t command = 0; // ROCK's id for the equip
		Clock::time_point deadline;

		// Swap only: the weapon leaving the hand. Its copy stays hidden until
		// the drawn weapon shows, so the two are never visible together.
		MyWeapon* outgoing = nullptr;
		std::string outgoingName;
		UInt16 outgoingNameLength = 0;
		bool outgoingHidden = false;
		UInt32 outgoingIndex = 0;
		UInt32 outgoingFormId = 0;

		// The holster's copy of the weapon, moved into the hand when the draw
		// starts. It is shown until the real weapon appears.
		MyWeapon* inHand = nullptr;

		// The equip slot ROCK put the weapon into, from ROCK's result.
		std::uint32_t weaponSlot = 0;
		bool weaponSlotKnown = false;
	};

	// After the equip finished: the holster's copy stays in the hand until the
	// real weapon is visible there, then it is removed.
	struct HandHandoff
	{
		MyWeapon* clone = nullptr;
		UInt32 formId = 0;
		Clock::time_point deadline;
		std::uint64_t command = 0;       // ROCK's id for the equip
		std::uint32_t weaponSlot = 0;
		bool weaponSlotKnown = false;
	};

	// A loose weapon (picked up with ROCK, not equipped) on its way into an
	// empty holster. While the hand holds it inside the holster, ROCK reserves
	// it for us (lease). Letting go there sets committing; once ROCK has let
	// go, the weapon is taken into the inventory and the holster.
	struct HandDeposit
	{
		std::uint64_t lease = 0;              // ROCK's reservation, 0 = none
		std::uint32_t referenceFormId = 0;    // the loose weapon
		UInt32 slot = 0;
		bool committing = false;
		int framesLeft = 0;
	};

	// A grenade or mine being drawn: ROCK puts one from the inventory into the
	// hand as a grabbed object. The holster keeps its tag while any are left.
	struct ThrowableDraw
	{
		bool active = false;
		UInt32 slot = 0;
		UInt32 formId = 0;
		std::string name;
		std::uint64_t command = 0;   // ROCK's id for the grab, 0 = not accepted yet
		Clock::time_point deadline;
	};

	// Values kept for the log only (HolsterTrace.cpp).
	struct HandTrace
	{
		UInt32 nearestSlot = Interaction::kNoSlot;
		float nearestDistance = 0.0F;
		float nearestRadius = 0.0F;
		std::size_t zoneSamples = 0;
		bool heldLastFrame = false;
		NiPoint3 position{};
		Clock::time_point pressedAt;
	};

	// Everything the mod keeps for one hand.
	struct HandState
	{
		// The holster zone the hand is in (0 = none), set by detectHolsterSphere.
		UInt32 zone = 0;
		// The hand's position is known this frame.
		bool anchorAvailable = false;

		// The holster button: tells presses and releases apart.
		Interaction::HolsterGesture gesture;
		// The current press started in a holster it can act on. ROCK does not
		// get the button until it is released.
		bool pressFromZone = false;
		// The current press let go of a two-handed weapon. It never draws.
		bool pressTwoHanded = false;
		bool twoHandedLastFrame = false;
		// The current press drew a grenade. ROCK keeps the button, so letting
		// go throws it.
		bool pressDrewThrowable = false;
		// The grenade holster of that press while the hand stays in it.
		UInt32 throwableHoldSlot = 0;
		Clock::time_point throwableHoldSince;
		// The current press emptied a grenade holster: its release stores nothing.
		bool depositBlocked = false;

		// Work in progress for this hand.
		HandTransfer transfer;
		HandHandoff handoff;
		HandDeposit deposit;
		ThrowableDraw throwableDraw;

		HandTrace trace;
	};

	extern HandState gLeftHand;
	extern HandState gRightHand;
	HandState& GetHand(bool isLeft);

	// What VRButtonsMain read from the controllers this frame.
	struct HolsterButtonInput
	{
		bool leftControllerAvailable = false;
		bool rightControllerAvailable = false;
		bool leftHolsterHeld = false;
		bool rightHolsterHeld = false;
		// The game's own read of the holster button (for the log only).
		bool leftGameHeld = false;
		bool rightGameHeld = false;
		bool gameStopped = false;
		bool scopeOpen = false;
	};

	// HandHolsters.cpp

	// Called by VRButtonsMain every frame, in this order.
	void UpdateHandGestures(const HolsterButtonInput& input);
	void RunHandActions();

	void PulseHand(bool isLeft);
	void SetActivationSuppressed(bool suppress);
	bool SlotHoldsThrowable(UInt32 slot);
	HandWeaponFacts GetHandWeaponFacts(bool isLeft);
	// A draw is running from this slot.
	bool SlotHasTransfer(UInt32 slot);
	std::array<bool, 8> BusySlots();
	// Where the hand's weapon model is on screen now.
	bool CaptureHeldWeapon(const HandWeaponFacts& facts, NiNode*& model, NiTransform& world);
	WeaponStack FindHandStack(const HandWeaponFacts& facts);
	// Takes a weapon lying in the world into the inventory and an empty holster.
	bool DepositReference(UInt32 slot, TESObjectREFR* reference);
	void ServiceDeposits();
	void ReleaseDeposits();
	void ServiceThrowableDraws();
	void CancelThrowableDraws();

	// HolsterDraw.cpp

	NiTransform ToNiTransform(const Rock::WeaponWorld& from);
	Rock::WeaponWorld ToWeaponWorld(const NiTransform& from);

	// Starts drawing the slot's weapon into the hand. With swap, the hand's
	// weapon goes into the slot once the drawn one is in the hand.
	bool BeginDraw(bool isLeft, UInt32 slot, const HandWeaponFacts& facts, bool swap);
	// Called by MainLoop every frame.
	void ServiceTransfers();
	void ServiceHandoffs();
	void ServiceWarmWeapons();
	void UpdateWeaponBlends();
	// Jumps every sliding holster weapon to its final pose.
	void FinishWeaponBlends();
	void CancelTransfers();
	void ReleaseHandoffs();
}
