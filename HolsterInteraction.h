#pragma once

// The holster button gesture and zone selection, free of engine types so the
// per-hand rules are unit tested (tests/HolsterInteractionTests.cpp).

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
		kHeld,
		kConsumed
	};

	enum class GestureActionKind : std::uint8_t
	{
		kNone,
		kReleaseInZone,
		kPressDraw
	};

	struct GestureInput
	{
		bool inputAvailable{};
		bool gameplayEnabled{};
		bool buttonHeld{};
		Slot candidateSlot{};
		// The candidate slot can be drawn from by this hand right now.
		bool drawEligible{};
	};

	struct GestureDecision
	{
		Slot releasedSlot{};
		Slot drawSlot{};

		[[nodiscard]] bool HasRelease() const noexcept
		{
			return releasedSlot != kNoSlot;
		}

		[[nodiscard]] bool HasDraw() const noexcept
		{
			return drawSlot != kNoSlot;
		}

		[[nodiscard]] bool HasAction() const noexcept
		{
			return HasRelease() || HasDraw();
		}

		[[nodiscard]] GestureActionKind Kind() const noexcept
		{
			if (HasDraw()) {
				return GestureActionKind::kPressDraw;
			}
			return HasRelease() ?
				GestureActionKind::kReleaseInZone :
				GestureActionKind::kNone;
		}

		[[nodiscard]] Slot ActionSlot() const noexcept
		{
			return HasDraw() ? drawSlot : releasedSlot;
		}
	};

	// OpenVR can attach while a button is already down, so startup and every
	// interruption require observing an unpressed sample before another action.
	// A press inside a slot this hand can draw from draws at once, on the press
	// frame or as soon as that slot becomes drawable while the press is held
	// inside it (a one-frame ineligible sample must not defer it). Entering a
	// slot while already held never draws. Every other press acts on release,
	// in the release-frame zone (holster, swap).
	class HolsterGesture
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
					pressSlot_ = input.candidateSlot;
					return DrawIfEligible(input);
				}
				return {};

			case GesturePhase::kHeld:
				if (input.buttonHeld) {
					candidateSlot_ = input.candidateSlot;
					return DrawIfEligible(input);
				}

				// The release-frame zone is authoritative. This makes leaving
				// before release a cancellation and entering while held valid.
				phase_ = GesturePhase::kIdle;
				candidateSlot_ = kNoSlot;
				pressSlot_ = kNoSlot;
				return { input.candidateSlot, kNoSlot };

			case GesturePhase::kConsumed:
				if (!input.buttonHeld) {
					phase_ = GesturePhase::kIdle;
				}
				return {};
			}

			Reset();
			return {};
		}

		void Reset() noexcept
		{
			phase_ = GesturePhase::kNeedsRelease;
			candidateSlot_ = kNoSlot;
			pressSlot_ = kNoSlot;
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
		[[nodiscard]] GestureDecision DrawIfEligible(const GestureInput& input) noexcept
		{
			if (pressSlot_ == kNoSlot || !input.drawEligible || input.candidateSlot != pressSlot_) {
				return {};
			}
			const Slot slot = pressSlot_;
			phase_ = GesturePhase::kConsumed;
			candidateSlot_ = kNoSlot;
			pressSlot_ = kNoSlot;
			return { kNoSlot, slot };
		}

		GesturePhase phase_{ GesturePhase::kNeedsRelease };
		Slot candidateSlot_{ kNoSlot };
		Slot pressSlot_{ kNoSlot };
	};

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
