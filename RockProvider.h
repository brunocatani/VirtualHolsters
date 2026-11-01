#pragma once

#include <cstddef>
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

	// ROCK shows the weapon of that slot in the hand.
	[[nodiscard]] bool WeaponShown(std::uint32_t slot, std::uint32_t formId) noexcept;

	// The hand holds, pulls or catches an object, or is latched to a
	// surface, so ROCK would refuse to equip into it.
	[[nodiscard]] bool HandBusyWithObject(bool isLeft) noexcept;

	// An inventory weapon by base form and inventory stack index.
	struct WarmWeapon
	{
		std::uint32_t formId{};
		std::uint32_t stackIndex{};
	};

	// The holstered set must be sent to ROCK: it changed, was never sent
	// since binding, or ROCK dropped it (a lifecycle generation changed).
	// False while a failed send waits for its retry.
	[[nodiscard]] bool WarmSendDue(bool setChanged) noexcept;

	// ROCK keeps these inventory weapons' animations loaded so drawing one
	// does not wait on the engine's first-use load. False: retried later.
	bool SetWarmWeapons(const WarmWeapon* weapons, std::size_t count) noexcept;

	// Where ROCK seats a firearm held by that hand this frame (scale 1).
	[[nodiscard]] bool GetFiringAimWorld(bool isLeft, WeaponWorld& out) noexcept;

	[[nodiscard]] Result UnequipWeapon(std::uint32_t slot, std::uint32_t formId) noexcept;
	// Queues a draw of inventory stack `stackIndex` of `formId` into the
	// hand (ROCK requestInventoryDraw: the stand-in is at the firing aim);
	// ROCK returns the hand's current weapon to the inventory.
	[[nodiscard]] Result RequestEquip(
		bool isLeft,
		std::uint32_t formId,
		std::uint32_t stackIndex,
		std::uint64_t& outCommand) noexcept;
	// On success, outWeaponSlot names the slot ROCK equipped the weapon into.
	[[nodiscard]] EquipProgress PollEquip(std::uint64_t command, std::uint32_t* outWeaponSlot = nullptr) noexcept;
	void CancelEquip(std::uint64_t command) noexcept;

	// Physical state of one OpenVR button on a wand. ROCK filters the game's
	// controller reads, including a button this mod reserves, so with ROCK
	// bound the holster button is read here. False when ROCK has no sample.
	bool ReadRawButton(bool isLeft, std::uint32_t buttonId, bool& held) noexcept;

	// The OpenVR buttons this mod can reserve in ROCK (menu, grip, A/X,
	// stick/pad press, trigger).
	inline constexpr std::uint64_t kReservableButtons =
		(1ull << 1) | (1ull << 2) | (1ull << 7) | (1ull << 32) | (1ull << 33);

	// buttons with every reservable button replaced by its physical state
	// from ROCK, where ROCK has a sample for it.
	std::uint64_t OverlayRawButtons(bool isLeft, std::uint64_t buttons) noexcept;

	// Physical thumbstick of a wand; false when ROCK has no sample.
	bool ReadRawStick(bool isLeft, float& x, float& y) noexcept;

	// Reserve one hand completely for this frame: ROCK neither grabs nor
	// acts on its buttons, and the game does not see its input (config mode).
	void ReserveHandExclusive(bool isLeft) noexcept;

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
