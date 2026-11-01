#include "RockProvider.h"

#include "LegacyBridge.h"

#include <ROCK/Client.h>
#include <ROCK/Hands.h>
#include <ROCK/Input.h>
#include <ROCK/Storage.h>
#include <ROCK/Weapon.h>

#include <Windows.h>

#include <array>
#include <cstdio>

namespace Holsters::Rock
{
	namespace
	{
		namespace api = rock::api;

		// Retry binding about every five seconds while ROCK.dll is loaded but
		// refuses this consumer, without spamming the log.
		constexpr std::uint32_t kBindRetryFrames = 450;
		constexpr std::uint32_t kLeaseFrames = 3;

		struct Reservation
		{
			bool requested{};
			bool reserveGrab{};
			bool exclusive{};
			std::uint64_t buttonMask{};
			bool shoulder{};
		};

		struct State
		{
			api::Client client{};
			const api::hands::Api* hands{};
			const api::weapon::Api* weapon{};
			const api::input::Api* input{};
			const api::storage::Api* storage{};
			bool bindFailureReported{};
			std::uint32_t retryCountdown{};
			bool active{};
			api::hands::Roles roles{};
			bool rolesValid{};
			std::array<Reservation, 2> pending{};   // [0] right, [1] left
			std::array<bool, 2> suppressed{};
			std::uint32_t routedMask{};
			std::uint64_t equipCommand{};
		};

		State s_state{};

		[[nodiscard]] api::Hand ToHand(const bool isLeft) noexcept
		{
			return isLeft ? api::Hand::Left : api::Hand::Right;
		}

		[[nodiscard]] std::size_t HandIndex(const bool isLeft) noexcept
		{
			return isLeft ? 1u : 0u;
		}

		void LogStatus(const char* what, const api::Status status) noexcept
		{
			char message[192]{};
			if (std::snprintf(
					message,
					sizeof(message),
					"VirtualHolsters: ROCK %s returned status %u.",
					what,
					static_cast<unsigned>(status)) > 0) {
				LegacyBridge::LogError(message);
			}
		}

		void Unbind() noexcept
		{
			if (s_state.client.owner()) {
				// Unregistering releases every lease this owner holds.
				(void)s_state.client.close();
			}
			s_state.hands = nullptr;
			s_state.weapon = nullptr;
			s_state.input = nullptr;
			s_state.storage = nullptr;
			s_state.active = false;
			s_state.rolesValid = false;
			s_state.suppressed = {};
			s_state.routedMask = 0;
			s_state.equipCommand = 0;
		}

		[[nodiscard]] bool Bind() noexcept
		{
			if (s_state.client.owner() && s_state.hands && s_state.weapon &&
				s_state.input && s_state.storage) {
				return true;
			}
			const HMODULE module = ::GetModuleHandleW(L"ROCK.dll");
			if (!module) {
				return false;
			}
			if (s_state.retryCountdown > 0) {
				--s_state.retryCountdown;
				return false;
			}

			const auto query = reinterpret_cast<api::QueryInterfaceV1>(
				::GetProcAddress(module, api::kQueryExportName));
			if (!query) {
				s_state.retryCountdown = kBindRetryFrames;
				if (!s_state.bindFailureReported) {
					s_state.bindFailureReported = true;
					LegacyBridge::LogError(
						"VirtualHolsters: ROCK.dll has no major-2 discovery "
						"export; holsters run without ROCK.");
				}
				return false;
			}

			using namespace rock::api;
			constexpr auto bits = [](auto... values) {
				return (static_cast<std::uint32_t>(values) | ...);
			};
			auto status = s_state.client.connect(query, "VirtualHolsters");
			const core::Api* core{};
			if (status == Status::Ok) {
				status = s_state.client.acquire(
					bits(core::Permission::Read), core);
			}
			if (status == Status::Ok) {
				status = s_state.client.acquire(
					bits(hands::Permission::Read), s_state.hands);
			}
			if (status == Status::Ok) {
				status = s_state.client.acquire(
					bits(weapon::Permission::Read, weapon::Permission::Write),
					s_state.weapon);
			}
			if (status == Status::Ok) {
				status = s_state.client.acquire(
					bits(input::Permission::Read, input::Permission::Write),
					s_state.input);
			}
			if (status == Status::Ok) {
				status = s_state.client.acquire(
					bits(storage::Permission::Write), s_state.storage);
			}
			if (status != Status::Ok) {
				Unbind();
				s_state.retryCountdown = kBindRetryFrames;
				if (!s_state.bindFailureReported) {
					s_state.bindFailureReported = true;
					LogStatus("binding (Core, Hands, Weapon, Input, Storage 2.0)", status);
				}
				return false;
			}
			s_state.bindFailureReported = false;
			LegacyBridge::LogInfo(
				"VirtualHolsters: bound ROCK Core, Hands, Weapon, Input and "
				"Storage 2.0; ROCK owns weapon hands.");
			return true;
		}

		void SubmitReservation(const bool isLeft) noexcept
		{
			const auto index = HandIndex(isLeft);
			const auto& reservation = s_state.pending[index];
			using Flag = api::input::HandInputSuppressionFlag;
			std::uint32_t flags = 0;
			if (reservation.reserveGrab) {
				flags |= static_cast<std::uint32_t>(Flag::SuppressNormalGrabPress) |
					static_cast<std::uint32_t>(Flag::SuppressGrabRelease) |
					static_cast<std::uint32_t>(Flag::SuppressHeldWeaponTriggerEquip) |
					static_cast<std::uint32_t>(Flag::SuppressGameplayCandidates);
			}
			if (reservation.exclusive) {
				flags |= static_cast<std::uint32_t>(Flag::SuppressConfigModeChord) |
					static_cast<std::uint32_t>(Flag::SuppressOpenVrGameInput);
			}
			constexpr std::uint64_t kChordButtons =
				(1ull << 1) | (1ull << 2) | (1ull << 7) | (1ull << 32) | (1ull << 33);
			api::input::HandInputSuppressionRequest request{};
			if (reservation.buttonMask &&
				(reservation.buttonMask & ~kChordButtons) == 0) {
				flags |= static_cast<std::uint32_t>(Flag::ReserveButtonChord);
				request.chordButtons[isLeft ? 0 : 1] = reservation.buttonMask;
			}

			const auto owner = s_state.client.owner();
			if (flags == 0) {
				if (s_state.suppressed[index]) {
					(void)s_state.input->clearHandInputSuppression(owner, ToHand(isLeft));
					s_state.suppressed[index] = false;
				}
				return;
			}
			request.hand = ToHand(isLeft);
			request.flags = flags;
			request.leaseFrames = kLeaseFrames;
			const auto status = s_state.input->setHandInputSuppression(owner, &request);
			s_state.suppressed[index] = status == api::Status::Ok;
		}
	}

	void BeginFrame() noexcept
	{
		s_state.pending = {};
		s_state.active = false;
		s_state.rolesValid = false;
		if (!Bind()) {
			return;
		}

		std::uint32_t ready = 0;
		const auto* core = s_state.client.core();
		const auto owner = s_state.client.owner();
		api::core::Snapshot snapshot{};
		if (core->isProviderReady(owner, &ready) != api::Status::Ok || !ready ||
			core->getSnapshot(owner, &snapshot) != api::Status::Ok ||
			!snapshot.frikSkeletonReady || snapshot.menuBlocking ||
			snapshot.configBlocking) {
			return;
		}
		s_state.rolesValid =
			s_state.hands->getRoles(owner, &s_state.roles) == api::Status::Ok;
		s_state.active = s_state.rolesValid;
	}

	void EndFrame() noexcept
	{
		if (!s_state.client.owner() || !s_state.input || !s_state.storage) {
			return;
		}
		for (const bool isLeft : { false, true }) {
			SubmitReservation(isLeft);
		}

		std::uint32_t mask = 0;
		if (s_state.pending[0].requested && s_state.pending[0].shoulder) {
			mask |= 1u;
		}
		if (s_state.pending[1].requested && s_state.pending[1].shoulder) {
			mask |= 2u;
		}
		if (mask != 0 || s_state.routedMask != 0) {
			api::storage::ShoulderRouting routing{};
			routing.handMask = mask;
			routing.leaseFrames = kLeaseFrames;
			const auto status =
				s_state.storage->setShoulderRouting(s_state.client.owner(), &routing);
			s_state.routedMask = status == api::Status::Ok ? mask : 0;
		}
	}

	void Reset() noexcept
	{
		s_state.pending = {};
		if (s_state.client.owner() && s_state.input) {
			(void)s_state.input->clearHandInputSuppression(
				s_state.client.owner(), api::Hand::None);
		}
		if (s_state.client.owner() && s_state.storage && s_state.routedMask) {
			api::storage::ShoulderRouting routing{};
			routing.leaseFrames = kLeaseFrames;
			(void)s_state.storage->setShoulderRouting(s_state.client.owner(), &routing);
		}
		if (s_state.equipCommand && s_state.weapon) {
			(void)s_state.weapon->cancelInventoryEquip(
				s_state.client.owner(), s_state.equipCommand);
		}
		s_state.suppressed = {};
		s_state.routedMask = 0;
		s_state.equipCommand = 0;
	}

	bool IsActive() noexcept
	{
		return s_state.active;
	}

	bool IsBound() noexcept
	{
		return s_state.client.owner() != 0 && s_state.weapon != nullptr;
	}

	HandWeapon GetHandWeapon(const bool isLeft) noexcept
	{
		if (!s_state.rolesValid) {
			return {};
		}
		const auto& role = isLeft ? s_state.roles.left : s_state.roles.right;
		HandWeapon result{};
		switch (role.role) {
		case api::hands::HandRole::Firing:
			result.role = HandRole::kFiring;
			break;
		case api::hands::HandRole::Support:
			result.role = HandRole::kSupport;
			break;
		default:
			return {};
		}
		if (role.weaponSlot >= api::kMaxWeaponSlots) {
			return {};
		}
		result.slot = role.weaponSlot;
		api::weapon::WeaponState state{};
		if (s_state.weapon->getWeaponState(
				s_state.client.owner(), role.weaponSlot, &state) == api::Status::Ok) {
			result.formId = state.weaponFormId;
		}
		return result;
	}

	bool GetWeaponWorld(const std::uint32_t slot, WeaponWorld& out) noexcept
	{
		if (!s_state.active || slot >= api::kMaxWeaponSlots) {
			return false;
		}
		api::weapon::Grip grip{};
		constexpr auto kWorldValid =
			static_cast<std::uint32_t>(api::weapon::GripFlag::WeaponWorldValid);
		if (s_state.weapon->getGrip(s_state.client.owner(), slot, &grip) !=
				api::Status::Ok ||
			(grip.flags & kWorldValid) == 0) {
			return false;
		}
		for (std::size_t i = 0; i < 9; ++i) {
			out.rotate[i] = grip.weaponWorld.rotate[i];
		}
		for (std::size_t i = 0; i < 3; ++i) {
			out.translate[i] = grip.weaponWorld.translate[i];
		}
		out.scale = grip.weaponWorld.scale;
		return true;
	}

	Result UnequipWeapon(const std::uint32_t slot, const std::uint32_t formId) noexcept
	{
		if (!s_state.active) {
			return Result::kUnavailable;
		}
		const auto status = s_state.weapon->unequipWeapon(
			s_state.client.owner(), slot, formId);
		switch (status) {
		case api::Status::Ok:
			return Result::kOk;
		case api::Status::Busy:
		case api::Status::NotReady:
			return Result::kBusy;
		default:
			LogStatus("unequipWeapon", status);
			return Result::kRefused;
		}
	}

	Result RequestEquip(
		const bool isLeft,
		const std::uint32_t formId,
		const std::uint32_t stackIndex,
		std::uint64_t& outCommand) noexcept
	{
		outCommand = 0;
		if (!s_state.active) {
			return Result::kUnavailable;
		}
		if (s_state.equipCommand) {
			return Result::kBusy;
		}
		const auto owner = s_state.client.owner();
		api::weapon::EquipRequest request{};
		request.hand = ToHand(isLeft);
		auto status = s_state.weapon->captureInventoryWeapon(
			owner, formId, stackIndex, &request.item);
		if (status == api::Status::Ok) {
			status = s_state.weapon->requestInventoryEquip(owner, &request, &outCommand);
		}
		switch (status) {
		case api::Status::RequestQueued:
			s_state.equipCommand = outCommand;
			return Result::kQueued;
		case api::Status::Busy:
		case api::Status::NotReady:
			return Result::kBusy;
		default:
			LogStatus("inventory equip", status);
			outCommand = 0;
			return Result::kRefused;
		}
	}

	EquipProgress PollEquip(const std::uint64_t command) noexcept
	{
		if (!command || !s_state.weapon || !s_state.client.owner()) {
			return EquipProgress::kFailed;
		}
		api::weapon::EquipResult result{};
		const auto status = s_state.weapon->getInventoryEquipResult(
			s_state.client.owner(), command, &result);
		if (status != api::Status::Ok) {
			if (command == s_state.equipCommand) {
				s_state.equipCommand = 0;
			}
			return EquipProgress::kFailed;
		}
		using State = api::weapon::EquipState;
		if (result.state == State::Succeeded) {
			if (command == s_state.equipCommand) {
				s_state.equipCommand = 0;
			}
			return EquipProgress::kSucceeded;
		}
		if (result.state == State::Failed || result.state == State::Cancelled) {
			if (command == s_state.equipCommand) {
				s_state.equipCommand = 0;
			}
			char message[192]{};
			if (std::snprintf(
					message,
					sizeof(message),
					"VirtualHolsters: ROCK equip command %llu ended state=%u "
					"failure=%u.",
					static_cast<unsigned long long>(command),
					static_cast<unsigned>(result.state),
					static_cast<unsigned>(result.failure)) > 0) {
				LegacyBridge::LogError(message);
			}
			return EquipProgress::kFailed;
		}
		constexpr auto kPresented = static_cast<std::uint32_t>(
			api::weapon::EquipResultFlag::IncomingPresented);
		return (result.flags & kPresented) != 0 ?
			EquipProgress::kPresented :
			EquipProgress::kPending;
	}

	void CancelEquip(const std::uint64_t command) noexcept
	{
		if (!command || !s_state.weapon || !s_state.client.owner()) {
			return;
		}
		(void)s_state.weapon->cancelInventoryEquip(s_state.client.owner(), command);
		if (command == s_state.equipCommand) {
			s_state.equipCommand = 0;
		}
	}

	bool ReadRawButton(const bool isLeft, const std::uint32_t buttonId, bool& held) noexcept
	{
		held = false;
		if (!s_state.client.owner() || !s_state.input) {
			return false;
		}
		api::input::RawWandButtonState state{};
		if (s_state.input->getRawWandButtonState(
				s_state.client.owner(), ToHand(isLeft), buttonId, &state) != api::Status::Ok ||
			!state.available) {
			return false;
		}
		held = state.held != 0;
		return true;
	}

	bool ReadRawStick(const bool isLeft, float& x, float& y) noexcept
	{
		x = 0.0F;
		y = 0.0F;
		return s_state.client.owner() && s_state.input &&
			s_state.input->getRawWandThumbstick(s_state.client.owner(), ToHand(isLeft), &x, &y) == api::Status::Ok;
	}

	void ReserveHandExclusive(const bool isLeft) noexcept
	{
		auto& reservation = s_state.pending[HandIndex(isLeft)];
		reservation.requested = true;
		reservation.exclusive = true;
	}

	std::uint64_t OverlayRawButtons(const bool isLeft, std::uint64_t buttons) noexcept
	{
		for (std::uint32_t id = 0; id < 64; ++id) {
			const std::uint64_t bit = 1ull << id;
			bool held = false;
			if ((kReservableButtons & bit) != 0 && ReadRawButton(isLeft, id, held)) {
				buttons = held ? (buttons | bit) : (buttons & ~bit);
			}
		}
		return buttons;
	}

	void ReserveHand(
		const bool isLeft,
		const bool reserveGrab,
		const std::uint64_t holsterButtonMask,
		const bool shoulder) noexcept
	{
		auto& reservation = s_state.pending[HandIndex(isLeft)];
		reservation.requested = true;
		reservation.reserveGrab |= reserveGrab;
		reservation.buttonMask |= holsterButtonMask;
		reservation.shoulder |= shoulder;
	}
}
