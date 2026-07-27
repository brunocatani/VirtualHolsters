#include "hook.h"

#include "LegacyBridge.h"

#include <F4SE/F4SE.h>
#include <REL/Relocation.h>

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstring>
#include <span>

namespace MainLoopHook
{
	namespace
	{
		constexpr std::uintptr_t kFrameCallsiteRva = 0x00D8'405E;
		constexpr std::uintptr_t kVanillaTargetRva = 0x00D3'C820;

		constexpr std::array<std::uint8_t, 7> kCallsitePrefix{
			0x48, 0x8B, 0x0D, 0x8A, 0x1C, 0xD5, 0x04
		};
		constexpr std::array<std::uint8_t, 12> kCallsiteSuffix{
			0x48, 0x8B, 0x0D, 0xA6, 0xE2, 0xBA,
			0x04, 0xE8, 0xE1, 0x02, 0x41, 0xFF
		};
		constexpr std::array<std::uint8_t, 24> kVanillaTargetPrefix{
			0x0F, 0x0D, 0x89, 0xCC, 0x07, 0x00,
			0x00, 0x8B, 0x81, 0xCC, 0x07, 0x00,
			0x00, 0x0F, 0x1F, 0x00, 0x8B, 0xD0,
			0xF0, 0x0F, 0xB1, 0x91, 0xCC, 0x07
		};

		using OriginalFunction = bool (*)(void* state);

		struct CallsiteInspection
		{
			std::uintptr_t callsite{};
			std::uintptr_t predecessor{};
			bool accepted{};
			bool vanilla{};
		};

		std::atomic_bool s_gameplayReady{ false };
		std::atomic_bool s_installed{ false };
		std::atomic_bool s_runtimeFailureReported{ false };
		OriginalFunction s_original{};

		[[nodiscard]] bool IsReadableAddress(
			const std::uintptr_t address,
			const std::size_t length) noexcept
		{
			if (!address || !length) {
				return false;
			}

			MEMORY_BASIC_INFORMATION info{};
			if (::VirtualQuery(
					reinterpret_cast<const void*>(address),
					&info,
					sizeof(info)) != sizeof(info)) {
				return false;
			}

			if (info.State != MEM_COMMIT ||
				(info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
				return false;
			}

			const auto regionEnd =
				reinterpret_cast<std::uintptr_t>(info.BaseAddress) +
				info.RegionSize;
			return address <= regionEnd && length <= regionEnd - address;
		}

		[[nodiscard]] bool IsExecutableAddress(
			const std::uintptr_t address) noexcept
		{
			MEMORY_BASIC_INFORMATION info{};
			if (!address ||
				::VirtualQuery(
					reinterpret_cast<const void*>(address),
					&info,
					sizeof(info)) != sizeof(info) ||
				info.State != MEM_COMMIT ||
				(info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
				return false;
			}

			const DWORD protection = info.Protect & 0xFF;
			return protection == PAGE_EXECUTE ||
				protection == PAGE_EXECUTE_READ ||
				protection == PAGE_EXECUTE_READWRITE ||
				protection == PAGE_EXECUTE_WRITECOPY;
		}

		[[nodiscard]] bool BytesEqual(
			const std::uintptr_t address,
			const std::span<const std::uint8_t> expected) noexcept
		{
			return IsReadableAddress(address, expected.size()) &&
				std::memcmp(
					reinterpret_cast<const void*>(address),
					expected.data(),
					expected.size()) == 0;
		}

		[[nodiscard]] std::uintptr_t DecodeRelativeCallTarget(
			const std::uintptr_t callsite) noexcept
		{
			if (!IsReadableAddress(callsite, 5) ||
				*reinterpret_cast<const std::uint8_t*>(callsite) != 0xE8) {
				return 0;
			}

			std::int32_t displacement{};
			std::memcpy(
				&displacement,
				reinterpret_cast<const void*>(callsite + 1),
				sizeof(displacement));
			return callsite + 5 + displacement;
		}

		[[nodiscard]] CallsiteInspection InspectCallsite() noexcept
		{
			const auto moduleBase = REL::Module::get().base();
			const auto callsite = moduleBase + kFrameCallsiteRva;
			const auto vanillaTarget = moduleBase + kVanillaTargetRva;

			if (!BytesEqual(
					callsite - kCallsitePrefix.size(),
					kCallsitePrefix) ||
				!BytesEqual(callsite + 5, kCallsiteSuffix)) {
				return { callsite, 0, false, false };
			}

			const auto predecessor = DecodeRelativeCallTarget(callsite);
			if (!predecessor || !IsExecutableAddress(predecessor)) {
				return { callsite, predecessor, false, false };
			}

			if (predecessor == vanillaTarget &&
				!BytesEqual(vanillaTarget, kVanillaTargetPrefix)) {
				return { callsite, predecessor, false, true };
			}

			return {
				callsite,
				predecessor,
				true,
				predecessor == vanillaTarget
			};
		}

		bool OnFrameUpdate(void* state) noexcept
		{
			const bool originalResult = s_original ?
				s_original(state) :
				false;

			if (!s_gameplayReady.load(std::memory_order_acquire)) {
				return originalResult;
			}

			try {
				LegacyBridge::RunFrame();
			} catch (const std::exception& error) {
				s_gameplayReady.store(false, std::memory_order_release);
				if (!s_runtimeFailureReported.exchange(
						true,
						std::memory_order_acq_rel)) {
					char message[512]{};
					std::snprintf(
						message,
						sizeof(message),
						"VirtualHolsters: gameplay loop disabled after "
						"an exception: %s",
						error.what());
					LegacyBridge::LogError(message);
				}
			} catch (...) {
				s_gameplayReady.store(false, std::memory_order_release);
				if (!s_runtimeFailureReported.exchange(
						true,
						std::memory_order_acq_rel)) {
					LegacyBridge::LogError(
						"VirtualHolsters: gameplay loop disabled after "
						"an unknown exception.");
				}
			}

			return originalResult;
		}
	}

	bool Install() noexcept
	{
		if (s_installed.load(std::memory_order_acquire)) {
			return true;
		}

		try {
			const auto inspection = InspectCallsite();
			if (!inspection.accepted) {
				LegacyBridge::LogError(
					"VirtualHolsters: frame-hook byte preflight rejected "
					"Fallout4VR.exe.");
				return false;
			}

			// Set the validated predecessor before mutation so even a write that
			// subsequently reports failure cannot swallow the engine call.
			s_original = reinterpret_cast<OriginalFunction>(
				inspection.predecessor);
			auto& trampoline = F4SE::GetTrampoline();
			const auto observed = trampoline.write_call<5>(
				inspection.callsite,
				OnFrameUpdate);
			if (observed != inspection.predecessor ||
				!IsExecutableAddress(observed)) {
				LegacyBridge::LogError(
					"VirtualHolsters: frame hook did not preserve its "
					"validated predecessor.");
				return false;
			}

			s_original = reinterpret_cast<OriginalFunction>(observed);
			s_installed.store(true, std::memory_order_release);
			LegacyBridge::LogInfo(
				inspection.vanilla ?
					"VirtualHolsters: verified vanilla frame hook installed." :
					"VirtualHolsters: verified chained frame hook installed.");
			return true;
		} catch (const std::exception& error) {
			char message[512]{};
			std::snprintf(
				message,
				sizeof(message),
				"VirtualHolsters: frame hook installation exception: %s",
				error.what());
			LegacyBridge::LogError(message);
			return false;
		} catch (...) {
			LegacyBridge::LogError(
				"VirtualHolsters: unknown frame hook installation exception.");
			return false;
		}
	}

	void SetGameplayReady(const bool ready) noexcept
	{
		s_gameplayReady.store(ready, std::memory_order_release);
		if (ready) {
			s_runtimeFailureReported.store(false, std::memory_order_release);
		}
	}
}
