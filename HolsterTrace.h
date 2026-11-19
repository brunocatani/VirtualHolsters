#pragma once

// Holster diagnostics written to VirtualHolsters.log. Nothing here changes
// what a holster does; every function only reads state and logs.

#include "HandHolsters.h"

#include <cstdio>

namespace Holsters {
	// Why the frame loop skipped.
	enum class FrameSkip : std::uint8_t { kNone, kNoPlayer, kNoRoot, kBoneTreeInit };

	// Local wall-clock stamp for the holster trace (the log has none).
	struct TraceStamp
	{
		char text[16]{};
		TraceStamp()
		{
			SYSTEMTIME now{};
			GetLocalTime(&now);
			std::snprintf(text, sizeof(text), "%02u:%02u:%02u.%03u",
				now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
		}
	};

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
