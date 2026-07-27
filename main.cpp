#include "LegacyBridge.h"
#include "hook.h"
#include "version.h"

#include <F4SE/F4SE.h>
#include <REL/Relocation.h>

#include <Windows.h>

namespace
{
	bool s_gameInitialized = false;

	void ReportPluginBoundaryFailure(
		const char* boundary,
		const char* detail) noexcept
	{
		char message[512]{};
		const int written = std::snprintf(
			message,
			sizeof(message),
			"VirtualHolsters: unhandled exception in %s%s%s\n",
			boundary ? boundary : "plugin boundary",
			detail ? ": " : "",
			detail ? detail : "");
		if (written > 0) {
			LegacyBridge::LogError(message);
			::OutputDebugStringA(message);
		}
	}

	void OnF4SEMessage(F4SE::MessagingInterface::Message* message) noexcept
	{
		if (!message) {
			return;
		}

		switch (message->type) {
		case F4SE::MessagingInterface::kGameLoaded:
			MainLoopHook::SetGameplayReady(false);
			s_gameInitialized = LegacyBridge::OnGameLoaded();
			if (s_gameInitialized) {
				MainLoopHook::SetGameplayReady(true);
				LegacyBridge::LogInfo(
					"VirtualHolsters: game lifecycle initialized.");
			} else {
				LegacyBridge::LogError(
					"VirtualHolsters: GameLoaded initialization failed; "
					"gameplay remains disabled.");
			}
			break;
		case F4SE::MessagingInterface::kPreLoadGame:
			MainLoopHook::SetGameplayReady(false);
			break;
		case F4SE::MessagingInterface::kPostLoadGame:
		case F4SE::MessagingInterface::kNewGame:
			if (s_gameInitialized && LegacyBridge::OnPostLoadGame()) {
				MainLoopHook::SetGameplayReady(true);
			}
			break;
		default:
			break;
		}
	}
}

extern "C" bool F4SEAPI F4SEPlugin_Query(
	const F4SE::QueryInterface* a_f4se,
	F4SE::PluginInfo* a_info) noexcept
{
	try {
		if (!a_f4se || !a_info) {
			return false;
		}

		LegacyBridge::InitializeLogging();
		LegacyBridge::LogInfo(
			"=== VirtualHolsters " VirtualHolsters_VERSION_VERSTRING
			" query ===");

		a_info->infoVersion = F4SE::PluginInfo::kVersion;
		a_info->name = "VirtualHolsters";
		a_info->version =
			VirtualHolsters_VERSION_MAJOR * 10000 +
			VirtualHolsters_VERSION_MINOR * 100 +
			VirtualHolsters_VERSION_PATCH;

		if (a_f4se->IsEditor()) {
			LegacyBridge::LogError(
				"VirtualHolsters: editor runtime is unsupported.");
			return false;
		}
		if (!REL::Module::IsVR()) {
			LegacyBridge::LogError(
				"VirtualHolsters: Fallout 4 VR runtime is required.");
			return false;
		}

		// F4SEVR 0.6.21 reports the flat-compatible 1.10.138 runtime in
		// QueryInterface. This is the loader compatibility domain.
		const auto requiredRuntime = F4SE::RUNTIME_1_10_138;
		if (a_f4se->RuntimeVersion() < requiredRuntime) {
			LegacyBridge::LogError(
				"VirtualHolsters: the F4SEVR loader compatibility runtime "
				"is older than 1.10.138.");
			return false;
		}

		// REL reads Fallout4VR.exe's actual file version. Keep this executable
		// version domain separate from QueryInterface::RuntimeVersion().
		const auto executableVersion = REL::Module::get().version();
		if (executableVersion != F4SE::RUNTIME_VR_1_2_72) {
			LegacyBridge::LogError(
				"VirtualHolsters: only Fallout4VR.exe 1.2.72.0 is "
				"supported.");
			return false;
		}

		LegacyBridge::LogInfo(
			"VirtualHolsters: FO4VR executable and F4SEVR loader gates "
			"passed.");
		return true;
	} catch (const std::exception& error) {
		ReportPluginBoundaryFailure("F4SEPlugin_Query", error.what());
		return false;
	} catch (...) {
		ReportPluginBoundaryFailure("F4SEPlugin_Query", nullptr);
		return false;
	}
}

extern "C" bool F4SEAPI F4SEPlugin_Load(
	const F4SE::LoadInterface* a_f4se) noexcept
{
	try {
		if (!a_f4se) {
			return false;
		}

		F4SE::Init(a_f4se, false);

		auto* papyrus = a_f4se->QueryInterface(
			F4SE::LoadInterface::kPapyrus);
		if (!LegacyBridge::RegisterPapyrus(papyrus)) {
			return false;
		}

		const auto* messaging = F4SE::GetMessagingInterface();
		if (!messaging || !messaging->RegisterListener(OnF4SEMessage)) {
			LegacyBridge::LogError(
				"VirtualHolsters: F4SE messaging registration failed.");
			return false;
		}

		F4SE::AllocTrampoline(256);
		if (!MainLoopHook::Install()) {
			LegacyBridge::LogError(
				"VirtualHolsters: verified frame hook installation failed.");
			return false;
		}

		LegacyBridge::LogInfo(
			"VirtualHolsters loaded; waiting for GameLoaded.");
		return true;
	} catch (const std::exception& error) {
		ReportPluginBoundaryFailure("F4SEPlugin_Load", error.what());
		return false;
	} catch (...) {
		ReportPluginBoundaryFailure("F4SEPlugin_Load", nullptr);
		return false;
	}
}
