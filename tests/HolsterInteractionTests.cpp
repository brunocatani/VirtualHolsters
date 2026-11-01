#include "HolsterInteraction.h"
#include "HolsterSlotPolicy.h"

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
		const bool drawEligible = false)
	{
		return gesture.Update({
			available,
			enabled,
			held,
			slot,
			drawEligible
		});
	}

	void TestStartupRequiresRelease()
	{
		using namespace Holsters::Interaction;

		HolsterGesture gesture;
		Check(
			!Update(gesture, true, true, true, 1).HasAction(),
			"startup while held must not act");
		Check(
			!Update(gesture, true, true, false, 1).HasAction(),
			"the startup release must only arm the gesture");
		Check(
			gesture.Phase() == GesturePhase::kIdle,
			"startup release must leave the gesture armed");
		Check(
			!Update(gesture, true, true, true, 1).HasAction(),
			"pressing inside a zone must not act");
		Check(
			Update(gesture, true, true, false, 1).releasedSlot == 1,
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

	void TestPressDraw()
	{
		using namespace Holsters::Interaction;

		HolsterGesture gesture;
		Update(gesture, true, true, false, kNoSlot);
		const auto draw = Update(gesture, true, true, true, 3, true);
		Check(draw.HasDraw() && !draw.HasRelease() && draw.drawSlot == 3,
			"pressing inside a drawable slot must draw on the press frame");
		Check(gesture.Phase() == GesturePhase::kConsumed, "the draw must consume the press");
		Check(!Update(gesture, true, true, true, 3, true).HasAction(),
			"holding after the draw must not draw again");
		Check(!Update(gesture, true, true, false, 3, true).HasAction(),
			"release after the draw must not also act in the zone");
		Check(gesture.Phase() == GesturePhase::kIdle, "release after the draw must rearm the next press");
	}

	void TestPressDrawRequiresPressInside()
	{
		using namespace Holsters::Interaction;

		HolsterGesture gesture;
		Update(gesture, true, true, false, kNoSlot);
		Update(gesture, true, true, true, kNoSlot);
		Check(!Update(gesture, true, true, true, 4, true).HasAction(),
			"entering a drawable slot while already held must not draw");
		Update(gesture, true, true, false, kNoSlot);

		Check(!Update(gesture, true, true, true, 4, false).HasAction(),
			"pressing an undrawable slot must not draw");
		const auto release = Update(gesture, true, true, false, 4, false);
		Check(release.HasRelease() && !release.HasDraw(),
			"an undrawable press must act on release in the zone");
	}

	void TestPressDrawWhenThePressedSlotBecomesDrawable()
	{
		using namespace Holsters::Interaction;

		HolsterGesture gesture;
		Update(gesture, true, true, false, kNoSlot);
		Check(!Update(gesture, true, true, true, 3, false).HasAction(), "an undrawable press frame must not draw");
		const auto late = Update(gesture, true, true, true, 3, true);
		Check(late.HasDraw() && late.drawSlot == 3,
			"the pressed slot becoming drawable during the hold must draw then");
		Update(gesture, true, true, false, kNoSlot);

		Update(gesture, true, true, true, 3, false);
		Check(!Update(gesture, true, true, true, 4, true).HasAction(),
			"another slot becoming drawable must not draw the pressed one");
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

	void TestIndependentHands()
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

	void TestHolsterSlotPolicy()
	{
		using namespace Holsters::SlotPolicy;

		Check(
			!IsEnabled(kAllSlotsMask, kNoSlot) &&
				!IsEnabled(kAllSlotsMask, kLastSlot + 1),
			"invalid slot numbers must never be enabled");
		for (Slot slot = kFirstSlot; slot <= kLastSlot; ++slot) {
			Check(
				IsEnabled(kAllSlotsMask, slot),
				"the default mask must enable every physical holster");
		}

		auto mask = SetEnabled(kAllSlotsMask, 2, false);
		mask = SetEnabled(mask, 4, false);
		mask = SetEnabled(mask, 7, false);
		Check(
			!IsEnabled(mask, 2) &&
				!IsEnabled(mask, 4) &&
				!IsEnabled(mask, 7),
			"disabled slots must be removed from the enable mask");
		Check(
			IsEnabled(mask, 1) &&
				IsEnabled(mask, 3) &&
				IsEnabled(mask, 5) &&
				IsEnabled(mask, 6),
			"disabling selected slots must preserve all other slots");
		Check(
			NextEnabled(mask, 1) == 3,
			"cycling must skip a disabled interior slot");
		Check(
			NextEnabled(mask, 6) == 1,
			"cycling must skip disabled slots while wrapping");
		Check(
			NextEnabled(mask, kNoSlot) == 1,
			"cycling without a current slot must choose the first enabled slot");
		Check(
			NextEnabled(kNoSlotsMask, 3) == kNoSlot,
			"cycling with every slot disabled must fail closed");
	}
}

int main()
{
	TestStartupRequiresRelease();
	TestReleaseInZone();
	TestEnterAndExitWhileHeld();
	TestPressDraw();
	TestPressDrawRequiresPressInside();
	TestPressDrawWhenThePressedSlotBecomesDrawable();
	TestInterruptionAndTrackingLossRearm();
	TestIndependentHands();
	TestStableZoneSelection();
	TestHolsterSlotPolicy();

	if (s_failures != 0) {
		std::cerr << s_failures << " interaction test(s) failed.\n";
		return 1;
	}

	std::cout << "All VirtualHolsters interaction tests passed.\n";
	return 0;
}
