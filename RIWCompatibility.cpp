#include "RIWCompatibility.h"

#include "LegacyBridge.h"

#include <Windows.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <type_traits>

namespace Holsters::RIWCompatibility
{
	namespace
	{
		namespace Abi
		{
			constexpr std::uint32_t kApiVersion = 1;
			constexpr std::uint32_t kExactHandCapability = 1u << 2;
			constexpr std::uint32_t kExactHandFeature = 1u << 4;

			enum class Result : std::uint32_t
			{
				kOk = 0,
				kNotReady = 1,
				kInvalidArgument = 2,
				kInvalidSize = 3,
				kUnsupportedVersion = 4,
				kCapacityFull = 5,
				kOwnerNotRegistered = 6,
				kOwnerConflict = 7,
				kPermissionDenied = 8,
				kWrongThread = 9,
				kNotFound = 10,
				kHandUnavailable = 11
			};

			enum class Hand : std::uint32_t
			{
				kNone = 0,
				kRight = 1,
				kLeft = 2
			};

			enum class RuntimeFlag : std::uint32_t
			{
				kRockConnected = 1u << 1,
				kAuthorityPublished = 1u << 2,
				kLeftFiringInfrastructureAvailable = 1u << 5,
				kWeaponPresent = 1u << 8
			};

			struct ConsumerRegistration
			{
				std::uint32_t size{ sizeof(ConsumerRegistration) };
				std::uint32_t version{ kApiVersion };
				char modName[64]{};
				std::uint32_t requestedCapabilities{ 0 };
				std::uint32_t reserved[7]{};
			};

			struct ConsumerHandle
			{
				std::uint32_t size{ sizeof(ConsumerHandle) };
				std::uint32_t version{ kApiVersion };
				std::uint64_t ownerToken{ 0 };
				std::uint32_t grantedCapabilities{ 0 };
				std::uint32_t providerGeneration{ 0 };
				std::uint32_t reserved[6]{};
			};

			struct Limits
			{
				std::uint32_t size{ sizeof(Limits) };
				std::uint32_t version{ kApiVersion };
				std::uint32_t featureBits{ 0 };
				std::uint32_t maxConsumers{ 0 };
				std::uint32_t maxCallbacks{ 0 };
				std::uint32_t providerApiByteSize{ 0 };
				std::uint32_t reserved[10]{};
			};

			struct RuntimeState
			{
				std::uint32_t size{ sizeof(RuntimeState) };
				std::uint32_t version{ kApiVersion };
				std::uint32_t flags{ 0 };
				std::uint32_t authorityFlags{ 0 };
				std::uint64_t revision{ 0 };
				std::uint64_t rockFrameIndex{ 0 };
				std::uint64_t authorityExpiresAfterFrame{ 0 };
				std::uint64_t weaponGenerationKey{ 0 };
				std::uint32_t weaponFormId{ 0 };
				std::uint32_t fixedFiringHand{ 0 };
				std::uint32_t currentFiringHand{ 0 };
				std::uint32_t reserved[9]{};
			};

			struct EquippedWeaponHandRequest
			{
				std::uint32_t size{ sizeof(EquippedWeaponHandRequest) };
				std::uint32_t version{ kApiVersion };
				Hand hand{ Hand::kNone };
				std::uint32_t flags{ 0 };
				std::uint32_t weaponFormId{ 0 };
				std::uint32_t reserved0{ 0 };
				std::uint64_t weaponGenerationKey{ 0 };
				std::uint64_t reserved[2]{};
			};

			using EventCallback = void(__cdecl*)(const void*, void*);

			struct Api
			{
				std::uint32_t(__cdecl* getVersion)();
				const char* (__cdecl* getModVersion)();
				bool(__cdecl* isProviderReady)();
				bool(__cdecl* getLimitsV1)(Limits* outLimits);
				Result(__cdecl* registerConsumerV1)(
					const ConsumerRegistration* registration,
					ConsumerHandle* outHandle);
				Result(__cdecl* unregisterConsumerV1)(
					std::uint64_t ownerToken);
				bool(__cdecl* getConfigStateV1)(void* outState);
				bool(__cdecl* getRuntimeStateV1)(
					RuntimeState* outState);
				Result(__cdecl* registerEventCallbackV1)(
					std::uint64_t ownerToken,
					EventCallback callback,
					void* userData,
					std::uint64_t* outCallbackToken);
				Result(__cdecl* unregisterEventCallbackV1)(
					std::uint64_t ownerToken,
					std::uint64_t callbackToken);
				Result(__cdecl* requestEquippedWeaponHandV1)(
					std::uint64_t ownerToken,
					const EquippedWeaponHandRequest* request);
			};

			constexpr std::uint32_t kExactHandTableBytes =
				static_cast<std::uint32_t>(
					offsetof(Api, requestEquippedWeaponHandV1) +
					sizeof(Api::requestEquippedWeaponHandV1));

			using GetProviderApi = const Api* (__cdecl*)();

			static_assert(sizeof(ConsumerRegistration) == 104);
			static_assert(sizeof(ConsumerHandle) == 48);
			static_assert(sizeof(Limits) == 64);
			static_assert(sizeof(RuntimeState) == 96);
			static_assert(sizeof(EquippedWeaponHandRequest) == 48);
			static_assert(sizeof(Api) == 88);
			static_assert(kExactHandTableBytes == sizeof(Api));
			static_assert(std::is_standard_layout_v<Api>);
		}

		constexpr std::uint32_t kRequestLifetimeFrames = 180;

		struct PendingRequest
		{
			bool active{ false };
			bool isLeft{ false };
			std::uint32_t weaponFormId{ 0 };
			std::uint32_t remainingFrames{ 0 };
		};

		const Abi::Api* s_api{ nullptr };
		std::uint64_t s_ownerToken{ 0 };
		bool s_riwObserved{ false };
		PendingRequest s_pending{};

		[[nodiscard]] bool HasFlag(
			const std::uint32_t flags,
			const Abi::RuntimeFlag flag) noexcept
		{
			return (flags & static_cast<std::uint32_t>(flag)) != 0;
		}

		[[nodiscard]] bool Connect() noexcept
		{
			if (s_api && s_ownerToken != 0) {
				return true;
			}

			const HMODULE module = ::GetModuleHandleA("RIW.dll");
			if (!module) {
				return false;
			}
			s_riwObserved = true;

			const auto getProviderApi =
				reinterpret_cast<Abi::GetProviderApi>(
					::GetProcAddress(
						module,
						"ROCKIMMERSIVEWEAPONSAPI_GetProviderApi"));
			const Abi::Api* api =
				getProviderApi ? getProviderApi() : nullptr;
			if (!api ||
				!api->getVersion ||
				api->getVersion() < Abi::kApiVersion ||
				!api->isProviderReady ||
				!api->isProviderReady() ||
				!api->getLimitsV1 ||
				!api->registerConsumerV1) {
				return false;
			}

			Abi::Limits limits{};
			if (!api->getLimitsV1(&limits) ||
				limits.providerApiByteSize <
					Abi::kExactHandTableBytes ||
				(limits.featureBits & Abi::kExactHandFeature) == 0) {
				return false;
			}

			Abi::ConsumerRegistration registration{};
			constexpr char kConsumerName[] = "VirtualHolsters";
			std::memcpy(
				registration.modName,
				kConsumerName,
				sizeof(kConsumerName));
			registration.requestedCapabilities =
				Abi::kExactHandCapability;

			Abi::ConsumerHandle handle{};
			const auto result =
				api->registerConsumerV1(&registration, &handle);
			if (result != Abi::Result::kOk ||
				handle.ownerToken == 0 ||
				(handle.grantedCapabilities &
					Abi::kExactHandCapability) == 0) {
				return false;
			}

			s_api = api;
			s_ownerToken = handle.ownerToken;
			LegacyBridge::LogInfo(
				"VirtualHolsters: RIW exact-hand compatibility connected.");
			return true;
		}

		void ReportTerminalFailure(
			const Abi::Result result) noexcept
		{
			char message[192]{};
			const int written = std::snprintf(
				message,
				sizeof(message),
				"VirtualHolsters: RIW exact-hand request rejected "
				"result=%u form=%08X.",
				static_cast<std::uint32_t>(result),
				s_pending.weaponFormId);
			if (written > 0) {
				LegacyBridge::LogError(message);
			}
		}
	}

	void QueueEquippedWeaponHand(
		const bool isLeft,
		const std::uint32_t weaponFormId) noexcept
	{
		if (weaponFormId == 0) {
			return;
		}
		s_pending = {
			true,
			isLeft,
			weaponFormId,
			kRequestLifetimeFrames
		};
	}

	void ServicePendingRequest() noexcept
	{
		if (!s_pending.active) {
			return;
		}
		if (s_pending.remainingFrames == 0) {
			if (s_riwObserved) {
				LegacyBridge::LogError(
					"VirtualHolsters: RIW exact-hand request timed out.");
			}
			s_pending = {};
			return;
		}
		--s_pending.remainingFrames;

		if (!Connect() ||
			!s_api->getRuntimeStateV1 ||
			!s_api->requestEquippedWeaponHandV1) {
			return;
		}

		Abi::RuntimeState state{};
		if (!s_api->getRuntimeStateV1(&state) ||
			state.weaponFormId != s_pending.weaponFormId ||
			state.weaponGenerationKey == 0 ||
			!HasFlag(
				state.flags,
				Abi::RuntimeFlag::kRockConnected) ||
			!HasFlag(
				state.flags,
				Abi::RuntimeFlag::kAuthorityPublished) ||
			!HasFlag(
				state.flags,
				Abi::RuntimeFlag::kWeaponPresent) ||
			(s_pending.isLeft &&
				!HasFlag(
					state.flags,
					Abi::RuntimeFlag::
						kLeftFiringInfrastructureAvailable))) {
			return;
		}

		Abi::EquippedWeaponHandRequest request{};
		request.hand = s_pending.isLeft ?
			Abi::Hand::kLeft :
			Abi::Hand::kRight;
		request.weaponFormId = s_pending.weaponFormId;
		request.weaponGenerationKey = state.weaponGenerationKey;
		const auto result =
			s_api->requestEquippedWeaponHandV1(
				s_ownerToken,
				&request);
		if (result == Abi::Result::kOk) {
			char message[192]{};
			const int written = std::snprintf(
				message,
				sizeof(message),
				"VirtualHolsters: equipped weapon assigned to "
				"%s hand through RIW form=%08X generation=%016llX.",
				s_pending.isLeft ? "left" : "right",
				s_pending.weaponFormId,
				static_cast<unsigned long long>(
					state.weaponGenerationKey));
			if (written > 0) {
				LegacyBridge::LogInfo(message);
			}
			s_pending = {};
			return;
		}

		if (result == Abi::Result::kOwnerNotRegistered) {
			s_api = nullptr;
			s_ownerToken = 0;
			return;
		}
		if (result == Abi::Result::kNotReady ||
			result == Abi::Result::kPermissionDenied ||
			result == Abi::Result::kHandUnavailable) {
			return;
		}

		ReportTerminalFailure(result);
		s_pending = {};
	}

	void ResetPendingRequest() noexcept
	{
		s_pending = {};
	}
}
