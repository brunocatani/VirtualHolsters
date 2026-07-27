#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace Holsters::Interaction
{
	using Slot = std::uint32_t;

	constexpr Slot kNoSlot = 0;
	constexpr float kDefaultZoneMargin = 0.1F;
	constexpr float kNormalizedTieEpsilon = 0.0001F;

	enum class GesturePhase : std::uint8_t
	{
		kNeedsRelease,
		kIdle,
		kHeld
	};

	struct GestureInput
	{
		bool inputAvailable{};
		bool gameplayEnabled{};
		bool buttonHeld{};
		Slot candidateSlot{};
	};

	struct GestureDecision
	{
		Slot releasedSlot{};

		[[nodiscard]] bool HasRelease() const noexcept
		{
			return releasedSlot != kNoSlot;
		}
	};

	// A gesture is deliberately level-driven. OpenVR can attach while a button
	// is already down, so startup and every interruption require observing an
	// unpressed sample before another release can produce an action.
	class ReleaseGesture
	{
	public:
		[[nodiscard]] GestureDecision Update(
			const GestureInput& input) noexcept
		{
			if (!input.inputAvailable || !input.gameplayEnabled) {
				Reset();
				return {};
			}

			switch (phase_) {
			case GesturePhase::kNeedsRelease:
				if (!input.buttonHeld) {
					phase_ = GesturePhase::kIdle;
				}
				return {};

			case GesturePhase::kIdle:
				if (input.buttonHeld) {
					phase_ = GesturePhase::kHeld;
					candidateSlot_ = input.candidateSlot;
				}
				return {};

			case GesturePhase::kHeld:
				if (input.buttonHeld) {
					candidateSlot_ = input.candidateSlot;
					return {};
				}

				// The release-frame zone is authoritative. This makes leaving
				// before release a cancellation and entering while held valid.
				phase_ = GesturePhase::kIdle;
				candidateSlot_ = kNoSlot;
				return { input.candidateSlot };
			}

			Reset();
			return {};
		}

		void Reset() noexcept
		{
			phase_ = GesturePhase::kNeedsRelease;
			candidateSlot_ = kNoSlot;
		}

		[[nodiscard]] GesturePhase Phase() const noexcept
		{
			return phase_;
		}

		[[nodiscard]] Slot CandidateSlot() const noexcept
		{
			return candidateSlot_;
		}

		[[nodiscard]] bool IsHoldingCandidate() const noexcept
		{
			return phase_ == GesturePhase::kHeld &&
				candidateSlot_ != kNoSlot;
		}

	private:
		GesturePhase phase_{ GesturePhase::kNeedsRelease };
		Slot candidateSlot_{ kNoSlot };
	};

	struct HandRelease
	{
		bool hasRelease{};
		bool isLeft{};
		Slot slot{};
	};

	[[nodiscard]] inline HandRelease SelectRelease(
		const GestureDecision& left,
		const GestureDecision& right,
		const bool preferRight) noexcept
	{
		if (left.HasRelease() && right.HasRelease()) {
			return preferRight ?
				HandRelease{ true, false, right.releasedSlot } :
				HandRelease{ true, true, left.releasedSlot };
		}
		if (left.HasRelease()) {
			return { true, true, left.releasedSlot };
		}
		if (right.HasRelease()) {
			return { true, false, right.releasedSlot };
		}
		return {};
	}

	struct ZoneSample
	{
		Slot slot{};
		float distance{};
		float radius{};
	};

	[[nodiscard]] inline bool IsUsableZoneSample(
		const ZoneSample& sample) noexcept
	{
		return sample.slot != kNoSlot &&
			std::isfinite(sample.distance) &&
			std::isfinite(sample.radius) &&
			sample.distance >= 0.0F &&
			sample.radius > 0.0F;
	}

	// Retain the current slot through its exit margin. Once it is left, select
	// the closest normalized enter candidate. Equal candidates fail closed
	// instead of inheriting std::map iteration order.
	[[nodiscard]] inline Slot SelectStableZone(
		const Slot currentSlot,
		const ZoneSample* samples,
		const std::size_t sampleCount,
		const float enterMargin = kDefaultZoneMargin,
		const float exitMargin = kDefaultZoneMargin) noexcept
	{
		if (!samples || sampleCount == 0) {
			return kNoSlot;
		}

		if (currentSlot != kNoSlot) {
			for (std::size_t index = 0; index < sampleCount; ++index) {
				const auto& sample = samples[index];
				if (sample.slot == currentSlot &&
					IsUsableZoneSample(sample) &&
					sample.distance <=
						sample.radius + (std::max)(0.0F, exitMargin)) {
					return currentSlot;
				}
			}
		}

		Slot bestSlot = kNoSlot;
		float bestScore = std::numeric_limits<float>::infinity();
		bool ambiguous = false;
		for (std::size_t index = 0; index < sampleCount; ++index) {
			const auto& sample = samples[index];
			if (!IsUsableZoneSample(sample)) {
				continue;
			}

			const float enterRadius =
				(std::max)(
					0.0F,
					sample.radius - (std::max)(0.0F, enterMargin));
			if (sample.distance > enterRadius) {
				continue;
			}

			const float score = sample.distance / sample.radius;
			if (score + kNormalizedTieEpsilon < bestScore) {
				bestScore = score;
				bestSlot = sample.slot;
				ambiguous = false;
			} else if (
				std::fabs(score - bestScore) <= kNormalizedTieEpsilon) {
				ambiguous = true;
			}
		}

		return ambiguous ? kNoSlot : bestSlot;
	}
}
