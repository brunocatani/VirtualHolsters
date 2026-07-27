#include "HolsterInteraction.h"

#include <array>
#include <iostream>

namespace
{
	int s_failures = 0;

	void Check(const bool condition, const char* message)
	{
		if (!condition) {
			std::cerr << "FAILED: " << message << '\n';
			++s_failures;
		}
	}

	Holsters::Interaction::GestureDecision Update(
		Holsters::Interaction::HolsterGesture& gesture,
		const bool available,
		const bool enabled,
		const bool held,
		const Holsters::Interaction::Slot slot,
		const bool pullOutEligible = false)
	{
		return gesture.Update({
			available,
			enabled,
			held,
			slot,
			pullOutEligible
		});
	}

	void TestStartupRequiresRelease()
	{
		using namespace Holsters::Interaction;

		HolsterGesture gesture;
		Check(
			!Update(gesture, true, true, true, 1, true).HasAction(),
			"startup while held must not act");
		Check(
			!Update(gesture, true, true, false, 1, true).HasAction(),
			"the startup release must only arm the gesture");
		Check(
			gesture.Phase() == GesturePhase::kIdle,
			"startup release must leave the gesture armed");
		Check(
			!Update(gesture, true, true, true, 1, true).HasAction(),
			"pressing inside a zone must not act");
		Check(
			Update(gesture, true, true, false, 1, true).releasedSlot == 1,
			"a later release inside the zone must act");
	}

	void TestReleaseInZone()
	{
		using namespace Holsters::Interaction;

		HolsterGesture gesture;
		Update(gesture, true, true, false, kNoSlot);
		Check(
			!Update(gesture, true, true, true, 3).HasAction(),
			"press inside must not act");
		Check(
			!Update(gesture, true, true, true, 3).HasAction(),
			"hold inside must not act");
		Check(
			Update(gesture, true, true, false, 3).releasedSlot == 3,
			"release inside must act exactly once");
		Check(
			!Update(gesture, true, true, false, 3).HasAction(),
			"an idle frame after release must not repeat the action");
	}

	void TestEnterAndExitWhileHeld()
	{
		using namespace Holsters::Interaction;

		HolsterGesture gesture;
		Update(gesture, true, true, false, kNoSlot);
		Update(gesture, true, true, true, kNoSlot);
		Check(
			!Update(gesture, true, true, true, 4).HasAction(),
			"entering a zone while held must not act early");
		Check(
			Update(gesture, true, true, false, 4).releasedSlot == 4,
			"entering while held then releasing in-zone must act");

		Update(gesture, true, true, true, 4);
		Check(
			!Update(
				gesture,
				true,
				true,
				true,
				kNoSlot).HasAction(),
			"an ineligible held exit must not act");
		Check(
			!Update(
				gesture,
				true,
				true,
				false,
				kNoSlot).HasAction(),
			"an ineligible exit followed by release must cancel");
	}

	void TestPullOutGesture()
	{
		using namespace Holsters::Interaction;

		HolsterGesture gesture;
		Update(gesture, true, true, false, kNoSlot);
		Check(
			!Update(gesture, true, true, true, 3, true).HasAction(),
			"pressing an occupied slot must only arm pull-out");
		Check(
			!Update(gesture, true, true, true, 3, true).HasAction(),
			"holding inside the armed slot must not equip early");

		const auto pullOut =
			Update(gesture, true, true, true, kNoSlot);
		Check(
			pullOut.HasPullOut() &&
				!pullOut.HasRelease() &&
				pullOut.pulledSlot == 3,
			"leaving the armed slot while held must request equip");
		Check(
			gesture.Phase() == GesturePhase::kConsumed,
			"pull-out must consume the current press");
		Check(
			!Update(gesture, true, true, true, 3, true).HasAction(),
			"returning inside during a consumed press must not repeat");
		Check(
			!Update(gesture, true, true, false, 3, true).HasAction(),
			"release after pull-out must not also trigger release-in-zone");
		Check(
			gesture.Phase() == GesturePhase::kIdle,
			"release after pull-out must rearm the next press");
	}

	void TestPullOutRequiresEligiblePressInside()
	{
		using namespace Holsters::Interaction;

		HolsterGesture gesture;
		Update(gesture, true, true, false, kNoSlot);
		Update(gesture, true, true, true, kNoSlot);
		Update(gesture, true, true, true, 4, true);
		Check(
			!Update(
				gesture,
				true,
				true,
				true,
				kNoSlot,
				true).HasAction(),
			"entering while already held must not arm pull-out");
		Update(gesture, true, true, false, kNoSlot);

		Update(gesture, true, true, true, 4, false);
		Check(
			!Update(
				gesture,
				true,
				true,
				true,
				kNoSlot,
				true).HasAction(),
			"an ineligible slot must not arm pull-out");
		Update(gesture, true, true, false, kNoSlot);

		Update(gesture, true, true, true, 4, true);
		const auto release =
			Update(gesture, true, true, false, 4, true);
		Check(
			release.HasRelease() && !release.HasPullOut(),
			"release inside must remain distinct from pull-out");
	}

	void TestInterruptionAndTrackingLossRearm()
	{
		using namespace Holsters::Interaction;

		HolsterGesture gesture;
		Update(gesture, true, true, false, kNoSlot);
		Update(gesture, true, true, true, 2, true);
		const auto interrupted =
			Update(gesture, true, false, true, kNoSlot, true);
		Check(
			!interrupted.HasAction() &&
				gesture.Phase() == GesturePhase::kNeedsRelease,
			"gameplay interruption must cancel and disarm");
		Check(
			!Update(gesture, true, true, false, 2).HasAction(),
			"release after an interruption must only rearm");

		Update(gesture, true, true, true, 2, true);
		const auto trackingLost =
			Update(gesture, false, true, true, kNoSlot, true);
		Check(
			!trackingLost.HasAction() &&
				gesture.Phase() == GesturePhase::kNeedsRelease,
			"tracking loss must cancel and disarm");
		Check(
			!Update(gesture, true, true, false, 2).HasAction(),
			"release after tracking recovery must only rearm");

		Update(gesture, true, true, true, 2);
		Check(
			Update(gesture, true, true, false, 2).releasedSlot == 2,
			"a fresh cycle after recovery must work");
	}

	void TestIndependentHandsAndSimultaneousPriority()
	{
		using namespace Holsters::Interaction;

		HolsterGesture left;
		HolsterGesture right;
		Update(left, true, true, false, kNoSlot);
		Update(right, true, true, false, kNoSlot);
		Update(left, true, true, true, 1);
		Update(right, true, true, true, 7);

		const auto leftRelease = Update(left, true, true, false, 1);
		Check(
			leftRelease.releasedSlot == 1,
			"left-hand state must release independently");
		Check(
			right.Phase() == GesturePhase::kHeld,
			"left release must not alter right-hand state");

		const auto rightRelease = Update(right, true, true, false, 7);
		Check(
			rightRelease.releasedSlot == 7,
			"right-hand state must release independently");

		const auto preferRight =
			SelectAction({ 3, kNoSlot }, { kNoSlot, 4 }, true);
		Check(
			preferRight.HasAction() &&
				preferRight.kind == GestureActionKind::kPullOut &&
				!preferRight.isLeft &&
				preferRight.slot == 4,
			"simultaneous actions must prefer configured right hand");

		const auto preferLeft =
			SelectAction({ 3, kNoSlot }, { kNoSlot, 4 }, false);
		Check(
			preferLeft.HasAction() &&
				preferLeft.kind ==
					GestureActionKind::kReleaseInZone &&
				preferLeft.isLeft &&
				preferLeft.slot == 3,
			"simultaneous actions must prefer configured left hand");
	}

	void TestStableZoneSelection()
	{
		using namespace Holsters::Interaction;

		std::array<ZoneSample, 3> samples{
			ZoneSample{ 1, 2.0F, 5.0F },
			ZoneSample{ 2, 1.0F, 2.0F },
			ZoneSample{ 3, 8.0F, 5.0F }
		};
		Check(
			SelectStableZone(kNoSlot, samples.data(), samples.size()) == 1,
			"new selection must use closest normalized eligible zone");

		samples[0].distance = 5.05F;
		samples[1].distance = 0.1F;
		Check(
			SelectStableZone(1, samples.data(), samples.size()) == 1,
			"current zone must remain sticky through its exit margin");

		samples[0].distance = 5.2F;
		Check(
			SelectStableZone(1, samples.data(), samples.size()) == 2,
			"leaving the exit margin must arbitrate a new zone");

		samples[0] = { 1, 1.0F, 2.0F };
		samples[1] = { 2, 2.0F, 4.0F };
		Check(
			SelectStableZone(kNoSlot, samples.data(), 2) == kNoSlot,
			"equal normalized overlaps must fail closed");
	}
}

int main()
{
	TestStartupRequiresRelease();
	TestReleaseInZone();
	TestEnterAndExitWhileHeld();
	TestPullOutGesture();
	TestPullOutRequiresEligiblePressInside();
	TestInterruptionAndTrackingLossRearm();
	TestIndependentHandsAndSimultaneousPriority();
	TestStableZoneSelection();

	if (s_failures != 0) {
		std::cerr << s_failures << " interaction test(s) failed.\n";
		return 1;
	}

	std::cout << "All VirtualHolsters interaction tests passed.\n";
	return 0;
}
