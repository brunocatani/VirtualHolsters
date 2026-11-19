#pragma once

// Holster diagnostics written to VirtualHolsters.log. Nothing here changes
// what a holster does; every function only reads state and logs.

#include "HandHolsters.h"

#include <cstdio>

namespace Holsters {
	// Why the frame loop skipped.
	enum FrameSkip
	{
		FrameSkip_None,
		FrameSkip_NoPlayer,
		FrameSkip_NoRoot,
		FrameSkip_BoneTreeInit
	};

	// The local time as text, for log lines (the log has no time stamps).
	// Valid until the next call.
	const char* TraceTime();

	// Remembers the holster nearest to the hand, for the press log line.
	void TraceNearestHolster(HandState& hand, const Interaction::ZoneSample* samples, std::size_t count);

	// Logs when the frame loop starts or stops skipping.
	void TraceFrameSkip(FrameSkip skip);

	// Logs this frame's button presses and releases, changes to the input
	// gates and the button source, and the heartbeat. Called once per frame
	// after both hands' gestures were updated.
	void TraceHandFrame(
		const HolsterButtonInput& input,
		Interaction::GesturePhase leftPhaseBefore,
		Interaction::GesturePhase rightPhaseBefore,
		const Interaction::GestureDecision& leftDecision,
		const Interaction::GestureDecision& rightDecision,
		const HandWeaponFacts& leftFacts,
		const HandWeaponFacts& rightFacts);
}
