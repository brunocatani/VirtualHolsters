#pragma once

#include <array>
#include <cstdint>

// Read-only views of the player's equipped weapons by native equipment index
// (0: the first weapon; 1: ROCK's second weapon). Frame thread only; nothing
// returned here may be retained past the current frame.
namespace Holsters::NativeEquipment
{
	struct Equipped
	{
		std::uint32_t formId{};
		// Comparison key for inventory stacks (ExtraInstanceData); never
		// dereferenced.
		std::uintptr_t instance{};
	};

	[[nodiscard]] std::array<Equipped, 2> ReadEquippedWeapons() noexcept;

	// The visible first-person model root of the weapon at `index`, named with
	// its "(formid)" token: index 0 beneath the skeleton's "Weapon" node,
	// index 1 elsewhere in the first-person skeleton. Null when it is absent,
	// hidden or ambiguous. Borrowed engine NiNode for this frame only.
	[[nodiscard]] void* FindEquippedModel(std::uint32_t index, std::uint32_t formId) noexcept;
}
