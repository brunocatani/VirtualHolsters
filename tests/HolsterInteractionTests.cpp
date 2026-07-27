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
		Holsters::Interaction::ReleaseGesture& gesture,
		const bool available,
		const bool enabled,
		const bool held,
		const Holsters::Interaction::Slot slot)
	{
		return gesture.Update({ available, enabled, held, slot });
	}

	void TestStartupRequiresRelease()
	{
		using namespace Holsters::Interaction;

		ReleaseGesture gesture;
		Check(
			!Update(gesture, true, true, true, 1).HasRelease(),
			"startup while held must not act");
		Check(
			!Update(gesture, true, true, false, 1).HasRelease(),
			"the startup release must only arm the gesture");
		Check(
			gesture.Phase() == GesturePhase::kIdle,
			"startup release must leave the gesture armed");
		Check(
			!Update(gesture, true, true, true, 1).HasRelease(),
			"pressing inside a zone must not act");
		Check(
			Update(gesture, true, true, false, 1).releasedSlot == 1,
			"a later release inside the zone must act");
	}

	void TestReleaseOnlyGesture()
	{
		using namespace Holsters::Interaction;

		ReleaseGesture gesture;
		Update(gesture, true, true, false, kNoSlot);
		Check(
			!Update(gesture, true, true, true, 3).HasRelease(),
			"press inside must not act");
		Check(
			!Update(gesture, true, true, true, 3).HasRelease(),
			"hold inside must not act");
		Check(
			Update(gesture, true, true, false, 3).releasedSlot == 3,
			"release inside must act exactly once");
		Check(
			!Update(gesture, true, true, false, 3).HasRelease(),
			"an idle frame after release must not repeat the action");
	}

	void TestEnterAndExitWhileHeld()
	{
		using namespace Holsters::Interaction;

		ReleaseGesture gesture;
		Update(gesture, true, true, false, kNoSlot);
		Update(gesture, true, true, true, kNoSlot);
		Check(
			!Update(gesture, true, true, true, 4).HasRelease(),
			"entering a zone while held must not act early");
		Check(
			Update(gesture, true, true, false, 4).releasedSlot == 4,
			"entering while held then releasing in-zone must act");

		Update(gesture, true, true, true, 4);
		Update(gesture, true, true, true, kNoSlot);
		Check(
			!Update(gesture, true, true, false, kNoSlot).HasRelease(),
			"leaving the zone before release must cancel");
	}

	void TestInterruptionAndTrackingLossRearm()
	{
		using namespace Holsters::Interaction;

		ReleaseGesture gesture;
		Update(gesture, true, true, false, kNoSlot);
		Update(gesture, true, true, true, 2);
		Update(gesture, true, false, true, 2);
		Check(
			gesture.Phase() == GesturePhase::kNeedsRelease,
			"gameplay interruption must cancel and disarm");
		Check(
			!Update(gesture, true, true, false, 2).HasRelease(),
			"release after an interruption must only rearm");

		Update(gesture, true, true, true, 2);
		Update(gesture, false, true, true, 2);
		Check(
			gesture.Phase() == GesturePhase::kNeedsRelease,
			"tracking loss must cancel and disarm");
		Check(
			!Update(gesture, true, true, false, 2).HasRelease(),
			"release after tracking recovery must only rearm");

		Update(gesture, true, true, true, 2);
		Check(
			Update(gesture, true, true, false, 2).releasedSlot == 2,
			"a fresh cycle after recovery must work");
	}

	void TestIndependentHandsAndSimultaneousPriority()
	{
		using namespace Holsters::Interaction;

		ReleaseGesture left;
		ReleaseGesture right;
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
			SelectRelease({ 3 }, { 4 }, true);
		Check(
			preferRight.hasRelease &&
				!preferRight.isLeft &&
				preferRight.slot == 4,
			"simultaneous releases must prefer configured right hand");

		const auto preferLeft =
			SelectRelease({ 3 }, { 4 }, false);
		Check(
			preferLeft.hasRelease &&
				preferLeft.isLeft &&
				preferLeft.slot == 3,
			"simultaneous releases must prefer configured left hand");
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
	TestReleaseOnlyGesture();
	TestEnterAndExitWhileHeld();
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
