#pragma once

#include <cstdint>

// Optional ROCK 1.0 (provider API major 2) binding. VirtualHolsters works
// without ROCK; with it, ROCK owns which hand holds which weapon (akimbo),
// hand-directed equips and exact-slot unequips, and VirtualHolsters leases
// ROCK input for the hands working a holster.
//
// Thread model: every function runs on the game frame thread (VirtualHolsters'
// frame hook), which is ROCK's provider owner thread. No ROCK or engine type
// crosses this header; legacy F4SE units include it.
namespace Holsters::Rock
{
	inline constexpr std::uint32_t kNoWeaponSlot = 0xFFFF'FFFFu;

	// Engine storage order: rotate[row * 3 + column], as NiMatrix3 rows.
	struct WeaponWorld
	{
		float rotate[9]{};
		float translate[3]{};
		float scale{ 1.0F };
	};

	enum class HandRole : std::uint8_t
	{
		kNone,
		kFiring,
		kSupport
	};

	struct HandWeapon
	{
		HandRole role{ HandRole::kNone };
		std::uint32_t slot{ kNoWeaponSlot };
		std::uint32_t formId{};
	};

	enum class Result : std::uint8_t
	{
		kOk,
		kQueued,
		kBusy,
		kRefused,
		kUnavailable
	};

	enum class EquipProgress : std::uint8_t
	{
		kPending,
		kPresented,
		kSucceeded,
		kFailed
	};

	// Binds ROCK.dll once it is loaded and samples ROCK's lifecycle. Call once
	// per frame before any other function.
	void BeginFrame() noexcept;
	// Submits this frame's hand reservations and releases the ones that were
	// not renewed.
	void EndFrame() noexcept;
	// Drops every lease and pending reservation (load, menu, suspension).
	void Reset() noexcept;

	// Bound, ready, skeleton present and no ROCK-blocking menu this frame.
	[[nodiscard]] bool IsActive() noexcept;
	// Bound at all; hand ownership then belongs to ROCK even while not active.
	[[nodiscard]] bool IsBound() noexcept;

	[[nodiscard]] HandWeapon GetHandWeapon(bool isLeft) noexcept;
	[[nodiscard]] bool GetWeaponWorld(std::uint32_t slot, WeaponWorld& out) noexcept;

	[[nodiscard]] Result UnequipWeapon(std::uint32_t slot, std::uint32_t formId) noexcept;
	// Queues an equip of inventory stack `stackIndex` of `formId` into the
	// hand; ROCK returns the hand's current weapon to the inventory.
	[[nodiscard]] Result RequestEquip(
		bool isLeft,
		std::uint32_t formId,
		std::uint32_t stackIndex,
		std::uint64_t& outCommand) noexcept;
	[[nodiscard]] EquipProgress PollEquip(std::uint64_t command) noexcept;
	void CancelEquip(std::uint64_t command) noexcept;

	// Reserve one hand for this frame. reserveGrab keeps ROCK from grabbing
	// or releasing with that hand (holster button on the grab button);
	// holsterButtonMask reserves that OpenVR button on the hand's wand while
	// it is held; shoulder suppresses ROCK's shoulder stash for the hand.
	void ReserveHand(
		bool isLeft,
		bool reserveGrab,
		std::uint64_t holsterButtonMask,
		bool shoulder) noexcept;
}
