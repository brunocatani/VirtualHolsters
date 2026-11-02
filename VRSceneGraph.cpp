#include "VRSceneGraph.h"

#include "LegacyBridge.h"

#include <F4SE/F4SE.h>
#include <RE/NetImmerse/NiNode.h>
#include <REL/Relocation.h>

#include <Windows.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <span>

namespace VRSceneGraph
{
	namespace
	{
		// Fallout 4 VR inserts three NiAVObject virtuals before the NiNode
		// mutation surface. Legacy F4SEVR declares the flat slots and must not
		// dispatch these methods directly.
		constexpr std::size_t kAttachChildSlot = 0x3D;
		constexpr std::size_t kRemoveChildSlot = 0x40;
		constexpr std::uintptr_t kAttachChildRva = 0x01C1'7E20;
		constexpr std::uintptr_t kRemoveChildRva = 0x01C1'7F70;

		constexpr std::array<std::uint8_t, 24> kAttachChildPrefix{
			0x48, 0x85, 0xD2, 0x0F, 0x84, 0xBF, 0x00, 0x00,
			0x00, 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89,
			0x74, 0x24, 0x18, 0x57, 0x48, 0x83, 0xEC, 0x30
		};
		constexpr std::array<std::uint8_t, 24> kRemoveChildPrefix{
			0x48, 0x83, 0xEC, 0x28, 0x48, 0x8B, 0x01, 0x4C,
			0x8D, 0x44, 0x24, 0x40, 0x48, 0xC7, 0x44, 0x24,
			0x40, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x90, 0xF8
		};

		using AttachChildFunction = void (*)(void*, void*, bool);
		using RemoveChildFunction = void (*)(void*, void*);

		std::atomic_bool s_ready{ false };
		std::atomic_bool s_dispatchFailureReported{ false };

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
					sizeof(info)) != sizeof(info) ||
				info.State != MEM_COMMIT ||
				(info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
				return false;
			}

			const auto regionStart =
				reinterpret_cast<std::uintptr_t>(info.BaseAddress);
			const auto regionEnd = regionStart + info.RegionSize;
			return regionEnd >= regionStart &&
				address >= regionStart &&
				address <= regionEnd &&
				length <= regionEnd - address;
		}

		[[nodiscard]] bool IsExecutableGameTextAddress(
			const std::uintptr_t address) noexcept
		{
			const auto text =
				REL::Module::get().segment(REL::Segment::text);
			const auto textEnd = text.address() + text.size();
			if (!text.address() ||
				textEnd < text.address() ||
				address < text.address() ||
				address >= textEnd) {
				return false;
			}

			MEMORY_BASIC_INFORMATION info{};
			if (::VirtualQuery(
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

		[[nodiscard]] bool ReadPointer(
			const std::uintptr_t address,
			std::uintptr_t& value) noexcept
		{
			if (!IsReadableAddress(address, sizeof(value))) {
				value = 0;
				return false;
			}

			std::memcpy(
				&value,
				reinterpret_cast<const void*>(address),
				sizeof(value));
			return value != 0;
		}

		[[nodiscard]] bool VerifyBaseVtableEntry(
			const std::uintptr_t vtable,
			const std::size_t slot,
			const std::uintptr_t expectedTarget) noexcept
		{
			if (slot >
				(UINTPTR_MAX - vtable) / sizeof(std::uintptr_t)) {
				return false;
			}

			std::uintptr_t observedTarget{};
			return ReadPointer(
					vtable + slot * sizeof(std::uintptr_t),
					observedTarget) &&
				observedTarget == expectedTarget &&
				IsExecutableGameTextAddress(observedTarget);
		}

		[[nodiscard]] bool ValidateEngineObject(void* object) noexcept
		{
			if (!object) {
				return false;
			}

			std::uintptr_t vtable{};
			std::uintptr_t destructor{};
			return ReadPointer(
					reinterpret_cast<std::uintptr_t>(object),
					vtable) &&
				ReadPointer(vtable, destructor) &&
				IsExecutableGameTextAddress(destructor);
		}

		[[nodiscard]] std::uintptr_t ResolveVirtualTarget(
			void* object,
			const std::size_t slot) noexcept
		{
			if (!s_ready.load(std::memory_order_acquire) || !object) {
				return 0;
			}

			std::uintptr_t vtable{};
			if (!ReadPointer(
					reinterpret_cast<std::uintptr_t>(object),
					vtable) ||
				slot >
					(UINTPTR_MAX - vtable) / sizeof(std::uintptr_t)) {
				return 0;
			}

			std::uintptr_t target{};
			if (!ReadPointer(
					vtable + slot * sizeof(std::uintptr_t),
					target) ||
				!IsExecutableGameTextAddress(target)) {
				return 0;
			}
			return target;
		}

		void ReportDispatchFailure(const char* operation) noexcept
		{
			if (!s_dispatchFailureReported.exchange(
					true,
					std::memory_order_acq_rel)) {
				char message[256]{};
				std::snprintf(
					message,
					sizeof(message),
					"VirtualHolsters: rejected unsafe FO4VR NiNode %s "
					"dispatch; scene-graph mutation was skipped.",
					operation ? operation : "unknown");
				LegacyBridge::LogError(message);
			}
		}

		[[nodiscard]] bool RejectInitialization(
			const char* reason) noexcept
		{
			s_ready.store(false, std::memory_order_release);
			LegacyBridge::LogError(reason);
			return false;
		}
	}

	bool Initialize() noexcept
	{
		try {
			s_ready.store(false, std::memory_order_release);
			s_dispatchFailureReported.store(
				false,
				std::memory_order_release);

			if (!REL::Module::IsVR() ||
				REL::Module::get().version() !=
					F4SE::RUNTIME_VR_1_2_72) {
				return RejectInitialization(
					"VirtualHolsters: FO4VR NiNode ABI verification "
					"requires Fallout4VR.exe 1.2.72.0.");
			}

			const auto moduleBase = REL::Module::get().base();
			if (!moduleBase ||
				kAttachChildRva > UINTPTR_MAX - moduleBase ||
				kRemoveChildRva > UINTPTR_MAX - moduleBase) {
				return RejectInitialization(
					"VirtualHolsters: FO4VR module base is invalid for "
					"NiNode ABI verification.");
			}

			const auto attachTarget = moduleBase + kAttachChildRva;
			const auto removeTarget = moduleBase + kRemoveChildRva;
			if (!BytesEqual(attachTarget, kAttachChildPrefix) ||
				!BytesEqual(removeTarget, kRemoveChildPrefix)) {
				return RejectInitialization(
					"VirtualHolsters: FO4VR NiNode function signatures "
					"did not match the verified 1.2.72 executable.");
			}

			const auto niNodeVtable =
				RE::NiNode::VTABLE[0].address();
			if (!VerifyBaseVtableEntry(
					niNodeVtable,
					kAttachChildSlot,
					attachTarget) ||
				!VerifyBaseVtableEntry(
					niNodeVtable,
					kRemoveChildSlot,
					removeTarget)) {
				return RejectInitialization(
					"VirtualHolsters: FO4VR NiNode vtable identity "
					"verification failed.");
			}

			s_ready.store(true, std::memory_order_release);
			LegacyBridge::LogInfo(
				"VirtualHolsters: verified FO4VR NiNode scene-graph ABI.");
			return true;
		} catch (...) {
			return RejectInitialization(
				"VirtualHolsters: exception during FO4VR NiNode ABI "
				"verification.");
		}
	}

	void AttachChild(
		void* parent,
		void* child,
		const bool firstAvailable) noexcept
	{
		const auto target =
			ResolveVirtualTarget(parent, kAttachChildSlot);
		if (!target || !ValidateEngineObject(child)) {
			ReportDispatchFailure("AttachChild");
			return;
		}

		reinterpret_cast<AttachChildFunction>(target)(
			parent,
			child,
			firstAvailable);
	}

	void RemoveChild(void* parent, void* child) noexcept
	{
		const auto target =
			ResolveVirtualTarget(parent, kRemoveChildSlot);
		if (!target || !ValidateEngineObject(child)) {
			ReportDispatchFailure("RemoveChild");
			return;
		}

		reinterpret_cast<RemoveChildFunction>(target)(parent, child);
	}
}
