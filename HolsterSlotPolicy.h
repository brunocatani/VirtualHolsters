#pragma once

#include <array>
#include <cstdint>

namespace Holsters::SlotPolicy
{
	using Slot = std::uint32_t;
	using SlotMask = std::uint8_t;

	constexpr Slot kFirstSlot = 1;
	constexpr Slot kLastSlot = 7;
	constexpr Slot kNoSlot = 0;
	constexpr SlotMask kNoSlotsMask = 0;
	constexpr SlotMask kAllSlotsMask = 0xFE;

	inline constexpr std::array<const char*, 8> kConfigKeys{
		"",
		"LeftShoulder",
		"RightShoulder",
		"LeftHip",
		"RightHip",
		"LowerBack",
		"LeftChest",
		"RightChest"
	};

	[[nodiscard]] constexpr bool IsValidSlot(const Slot slot) noexcept
	{
		return slot >= kFirstSlot && slot <= kLastSlot;
	}

	[[nodiscard]] constexpr SlotMask SlotBit(const Slot slot) noexcept
	{
		return IsValidSlot(slot) ?
			static_cast<SlotMask>(1u << slot) :
			kNoSlotsMask;
	}

	[[nodiscard]] constexpr bool IsEnabled(
		const SlotMask mask,
		const Slot slot) noexcept
	{
		return (mask & SlotBit(slot)) != 0;
	}

	[[nodiscard]] constexpr SlotMask SetEnabled(
		const SlotMask mask,
		const Slot slot,
		const bool enabled) noexcept
	{
		const SlotMask bit = SlotBit(slot);
		return enabled ?
			static_cast<SlotMask>(mask | bit) :
			static_cast<SlotMask>(mask & ~bit);
	}

	[[nodiscard]] constexpr Slot NextEnabled(
		const SlotMask mask,
		const Slot current) noexcept
	{
		const Slot start = IsValidSlot(current) ? current : kNoSlot;
		for (Slot offset = 1; offset <= kLastSlot; ++offset) {
			const Slot candidate =
				((start + offset - 1) % kLastSlot) + kFirstSlot;
			if (IsEnabled(mask, candidate)) {
				return candidate;
			}
		}
		return kNoSlot;
	}
}
