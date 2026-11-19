#include "HolsterTrace.h"
#include "HolsterMath.h"
#include "utils.h"
#include "VRHook.h"

namespace Holsters {
	// Input gates the trace last reported (bit set built in VRButtonsMain).
	std::uint32_t s_tracedInputGates{ 0xFFFFFFFFu };

	FrameSkip s_tracedFrameSkip{ FrameSkip_None };

	constexpr float kHeartbeatSeconds = 10.0F;

	float s_heartbeatElapsed{};

	std::uint32_t s_heartbeatFrames{};

	std::uint32_t s_heartbeatZoneFrames[2]{};

	std::uint32_t s_tracedButtonSource[2]{ 0xFFFFFFFFu, 0xFFFFFFFFu };

	const char* FrameSkipName(FrameSkip skip)
	{
		switch (skip) {
		case FrameSkip_None: return "none";
		case FrameSkip_NoPlayer: return "no-player";
		case FrameSkip_NoRoot: return "no-third-person-root";
		case FrameSkip_BoneTreeInit: return "bone-tree-init-failed";
		}
		return "?";
	}

	const char* TraceTime()
	{
		static char text[16];
		SYSTEMTIME now{};
		GetLocalTime(&now);
		std::snprintf(text, sizeof(text), "%02u:%02u:%02u.%03u",
			now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
		return text;
	}

	void TraceNearestHolster(HandState& hand, const Interaction::ZoneSample* samples, std::size_t count)
	{
		hand.trace.nearestSlot = Interaction::kNoSlot;
		hand.trace.zoneSamples = count;
		float bestMargin = std::numeric_limits<float>::infinity();
		for (std::size_t index = 0; index < count; ++index) {
			const float margin = samples[index].distance - samples[index].radius;
			if (margin < bestMargin) {
				bestMargin = margin;
				hand.trace.nearestSlot = samples[index].slot;
				hand.trace.nearestDistance = samples[index].distance;
				hand.trace.nearestRadius = samples[index].radius;
			}
		}
	}

	void TraceFrameSkip(FrameSkip skip)
	{
		if (skip != s_tracedFrameSkip) {
			_MESSAGE("[%s] Holster frame %s: reason=%s", TraceTime(),
				skip == FrameSkip_None ? "resumed" : "skipped", FrameSkipName(skip));
			s_tracedFrameSkip = skip;
		}
	}

	const char* GesturePhaseName(Interaction::GesturePhase phase)
	{
		switch (phase) {
		case Interaction::GesturePhase::kNeedsRelease:
			return "needs-release";
		case Interaction::GesturePhase::kIdle:
			return "idle";
		case Interaction::GesturePhase::kHeld:
			return "held";
		case Interaction::GesturePhase::kConsumed:
			return "consumed";
		}
		return "unknown";
	}

	const char* GestureActionName(Interaction::GestureActionKind kind)
	{
		switch (kind) {
		case Interaction::GestureActionKind::kNone:
			return "none";
		case Interaction::GestureActionKind::kReleaseInZone:
			return "release-in-zone";
		case Interaction::GestureActionKind::kPressDraw:
			return "press-draw";
		}
		return "unknown";
	}

	// Press trace position fields for the hand's nearest holster. A press
	// that misses every zone has two distinct causes: the zone moved
	// against the body (centerInBody changes between presses at the same
	// holster) or the hand went for the displayed weapon away from the
	// zone centre (handInBody stays near the shown weapon, displayOffset
	// large). Event-only: called on button edges, never per frame.
	void FormatNearestPlacement(UInt32 slot, const NiPoint3& handWorld,
		char* buffer, std::size_t size)
	{
		buffer[0] = '\0';
		const auto holsterIt = HolsterRegisteredObjects.find(slot);
		if (holsterIt == HolsterRegisteredObjects.end() || !holsterIt->second ||
			!holsterIt->second->bone) {
			return;
		}
		const MyHolster& holster = *holsterIt->second;
		const NiTransform& boneWorld = holster.bone->m_worldTransform;
		const NiPoint3 center = LocalPointToWorld(boneWorld, holster.offset);
		const NiPoint3 handInBone = WorldPointToLocal(boneWorld, handWorld);

		NiAVObject* bodyRoot = (*g_player) && (*g_player)->unkF0 ? (*g_player)->unkF0->rootNode : nullptr;
		NiPoint3 centerInBody{};
		NiPoint3 handInBody{};
		if (bodyRoot) {
			centerInBody = WorldPointToLocal(bodyRoot->m_worldTransform, center);
			handInBody = WorldPointToLocal(bodyRoot->m_worldTransform, handWorld);
		}

		float displayOffset = -1.0F;
		float handToDisplay = -1.0F;
		const auto weaponIt = WeaponRegisteredObjects.find(slot);
		if (weaponIt != WeaponRegisteredObjects.end() && weaponIt->second &&
			weaponIt->second->WeapMesh) {
			const NiPoint3 display = weaponIt->second->WeapMesh->m_worldTransform.pos;
			displayOffset = vec3_len(display - center);
			handToDisplay = vec3_len(handWorld - display);
		}

		std::snprintf(buffer, size,
			" bone=%s boneScale=%.2f handInBone=(%.1f,%.1f,%.1f) offset=(%.1f,%.1f,%.1f) centerInBody=(%.1f,%.1f,%.1f) handInBody=(%.1f,%.1f,%.1f) body=%d displayOffset=%.1f handToDisplay=%.1f",
			holster.bone->m_name.c_str() ? holster.bone->m_name.c_str() : "?", boneWorld.scale,
			handInBone.x, handInBone.y, handInBone.z,
			holster.offset.x, holster.offset.y, holster.offset.z,
			centerInBody.x, centerInBody.y, centerInBody.z,
			handInBody.x, handInBody.y, handInBody.z, bodyRoot ? 1 : 0,
			displayOffset, handToDisplay);
	}

	// Press diagnostics: a press on an occupied slot that does not draw at
	// once names what blocked it.
	static void TracePress(HandState& hand, bool isLeft,
		Interaction::GesturePhase before, const HandWeaponFacts& facts)
	{
		if (before != Interaction::GesturePhase::kIdle || hand.gesture.Phase() == Interaction::GesturePhase::kIdle) {
			return;
		}
		hand.trace.pressedAt = Clock::now();
		if (hand.gesture.Phase() == Interaction::GesturePhase::kHeld) {
			const UInt32 slot = hand.zone;
			if (slot != Interaction::kNoSlot && holsteredWeapNames[slot] != "Empty" && !facts.holds) {
				_MESSAGE("Holster %u press by the %s hand did not arm a draw: known=%d holds=%d twoHandedPress=%d canReceive=%d busyObject=%d handTransfer=%d slotTransfer=%d slotAvailable=%d",
					slot, isLeft ? "left" : "right", facts.known, facts.holds, hand.pressTwoHanded ? 1 : 0, facts.canReceive,
					Rock::HandBusyWithObject(isLeft) ? 1 : 0, GetHand(isLeft).transfer.active ? 1 : 0,
					SlotHasTransfer(slot) ? 1 : 0, SlotAvailable(slot) ? 1 : 0);
			}
		}
	}

	// One line per press and per release of the holster button.
	static void TraceButtonEdge(HandState& hand, bool isLeft, bool held,
		Interaction::GesturePhase before, const Interaction::GestureDecision& decision,
		const HandWeaponFacts& facts)
	{
		const auto after = hand.gesture.Phase();
		if (held != hand.trace.heldLastFrame) {
			char placement[384];
			FormatNearestPlacement(hand.trace.nearestSlot, hand.trace.position, placement, sizeof(placement));
			_MESSAGE("[%s] Holster input %s by the %s hand: zone=%u nearest=%u dist=%.1f radius=%.1f samples=%u phase=%s->%s action=%s holds=%d grips=%d twoHandedPress=%d canReceive=%d transfer=%d%s",
				TraceTime(), held ? "press" : "release", isLeft ? "left" : "right", hand.zone, hand.trace.nearestSlot,
				hand.trace.nearestDistance, hand.trace.nearestRadius, static_cast<unsigned>(hand.trace.zoneSamples),
				GesturePhaseName(before), GesturePhaseName(after), GestureActionName(decision.Kind()),
				facts.holds, facts.grips, hand.pressTwoHanded ? 1 : 0, facts.canReceive,
				GetHand(isLeft).transfer.active ? 1 : 0, placement);
		} else if (held && after == Interaction::GesturePhase::kNeedsRelease &&
			(before == Interaction::GesturePhase::kHeld || before == Interaction::GesturePhase::kConsumed)) {
			_MESSAGE("Holster gesture for the %s hand reset during a press (phase %s)",
				isLeft ? "left" : "right", GesturePhaseName(before));
		}
		hand.trace.heldLastFrame = held;
	}

	void TraceHandFrame(
		const HolsterButtonInput& input,
		Interaction::GesturePhase leftPhaseBefore,
		Interaction::GesturePhase rightPhaseBefore,
		const Interaction::GestureDecision& leftDecision,
		const Interaction::GestureDecision& rightDecision,
		const HandWeaponFacts& leftFacts,
		const HandWeaponFacts& rightFacts)
	{
		TracePress(gLeftHand, true, leftPhaseBefore, leftFacts);
		TracePress(gRightHand, false, rightPhaseBefore, rightFacts);

		// Input trace, on gate changes and per press and release, never
		// per frame. Each cause that can stop every holster at once has
		// its own signature: a closed gate, a press outside every zone
		// (nearest distance and sample count), a gesture reset mid-press,
		// or a press that never releases.
		const std::uint32_t inputGates =
			(_isConfigModeActive ? 1u : 0u) | (input.gameStopped ? 2u : 0u) | (_WeaponHolsterInProgress ? 4u : 0u) |
			(input.leftControllerAvailable ? 8u : 0u) | (input.rightControllerAvailable ? 16u : 0u) |
			(gLeftHand.anchorAvailable ? 32u : 0u) | (gRightHand.anchorAvailable ? 64u : 0u) |
			(input.scopeOpen ? 128u : 0u);
		if (inputGates != s_tracedInputGates) {
			_MESSAGE("Holster input gates: config=%d gameStopped=%d holsterInProgress=%d controllers L=%d R=%d anchors L=%d R=%d scope=%d",
				(inputGates & 1u) != 0, (inputGates & 2u) != 0, (inputGates & 4u) != 0, (inputGates & 8u) != 0,
				(inputGates & 16u) != 0, (inputGates & 32u) != 0, (inputGates & 64u) != 0, (inputGates & 128u) != 0);
			s_tracedInputGates = inputGates;
		}
		TraceButtonEdge(gLeftHand, true, input.leftHolsterHeld, leftPhaseBefore, leftDecision, leftFacts);
		TraceButtonEdge(gRightHand, false, input.rightHolsterHeld, rightPhaseBefore, rightDecision, rightFacts);

		// Where each hand's holster button state comes from: ROCK's raw
		// read, or the game's controller read (which ROCK blanks for a
		// button it owns) when the raw read fails. Logged on change.
		Rock::RawButtonRead buttonReads[2]{};
		if (Rock::IsBound()) {
			for (int index = 0; index < 2; ++index) {
				const bool isLeft = index == 0;
				const auto read = Rock::ReadRawButtonDetail(isLeft, static_cast<std::uint32_t>(c_holsterButtonID));
				buttonReads[index] = read;
				const std::uint32_t source = !read.owner ? 0x100u :
					(static_cast<std::uint32_t>(read.status & 0xFF) << 4) | (read.available ? 0x8u : 0u) | (read.reason & 0x7u);
				if (source != s_tracedButtonSource[index]) {
					_MESSAGE("[%s] Holster button read for the %s hand: source=%s owner=%d status=%d available=%d reason=%u rawHeld=%d gameHeld=%d",
						TraceTime(), isLeft ? "left" : "right",
						read.owner && read.status == 0 && read.available ? "rock-raw" : "game",
						read.owner ? 1 : 0, read.status, read.available ? 1 : 0, read.reason, read.held ? 1 : 0,
						(isLeft ? input.leftGameHeld : input.rightGameHeld) ? 1 : 0);
					s_tracedButtonSource[index] = source;
				}
			}
		}

		// Heartbeat: proves the loop runs and shows, every few seconds,
		// what a dead holster would hide (zones never entered, the button
		// never seen, ROCK inactive, the skeleton the zones follow).
		++s_heartbeatFrames;
		if (gLeftHand.zone != Interaction::kNoSlot) {
			++s_heartbeatZoneFrames[0];
		}
		if (gRightHand.zone != Interaction::kNoSlot) {
			++s_heartbeatZoneFrames[1];
		}
		s_heartbeatElapsed += gFrameSeconds;
		if (s_heartbeatElapsed >= kHeartbeatSeconds) {
			const void* currentRoot = (*g_player)->unkF0 ? (*g_player)->unkF0->rootNode : nullptr;
			_MESSAGE("[%s] Holster heartbeat: frames=%u zoneFrames L=%u R=%u rock bound=%d active=%d gates=0x%02X root=%p resolvedRoot=%p tree=%p transforms=%d holsters=%u L[anchor=%d nearest=%u dist=%.1f radius=%.1f held=%d raw=%d/%d/%u phase=%s holds=%d canReceive=%d] R[anchor=%d nearest=%u dist=%.1f radius=%.1f held=%d raw=%d/%d/%u phase=%s holds=%d canReceive=%d]",
				TraceTime(), s_heartbeatFrames, s_heartbeatZoneFrames[0], s_heartbeatZoneFrames[1],
				Rock::IsBound() ? 1 : 0, Rock::IsActive() ? 1 : 0, inputGates, currentRoot, gResolvedRoot,
				gResolvedBoneTree, gResolvedTransforms, static_cast<unsigned>(HolsterRegisteredObjects.size()),
				gLeftHand.anchorAvailable ? 1 : 0, gLeftHand.trace.nearestSlot, gLeftHand.trace.nearestDistance, gLeftHand.trace.nearestRadius,
				input.leftHolsterHeld ? 1 : 0, buttonReads[0].status, buttonReads[0].available ? 1 : 0, buttonReads[0].reason,
				GesturePhaseName(gLeftHand.gesture.Phase()), leftFacts.holds ? 1 : 0, leftFacts.canReceive ? 1 : 0,
				gRightHand.anchorAvailable ? 1 : 0, gRightHand.trace.nearestSlot, gRightHand.trace.nearestDistance, gRightHand.trace.nearestRadius,
				input.rightHolsterHeld ? 1 : 0, buttonReads[1].status, buttonReads[1].available ? 1 : 0, buttonReads[1].reason,
				GesturePhaseName(gRightHand.gesture.Phase()), rightFacts.holds ? 1 : 0, rightFacts.canReceive ? 1 : 0);
			s_heartbeatElapsed = 0.0F;
			s_heartbeatFrames = 0;
			s_heartbeatZoneFrames[0] = s_heartbeatZoneFrames[1] = 0;
		}
	}
}
