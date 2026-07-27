#include "LegacyBridge.h"

#include "Holsters.h"
#include "MenuChecker.h"
#include "VRHook.h"

#include "common/IDebugLog.h"
#include "f4se/PluginAPI.h"

#include <ShlObj.h>

namespace
{
	void ReportException(const char* boundary, const char* detail) noexcept
	{
		char message[512]{};
		const int written = std::snprintf(
			message,
			sizeof(message),
			"VirtualHolsters: exception in %s%s%s",
			boundary ? boundary : "legacy bridge",
			detail ? ": " : "",
			detail ? detail : "");
		if (written > 0) {
			_ERROR("%s", message);
			::OutputDebugStringA(message);
		}
	}
}

namespace LegacyBridge
{
	bool InitializeLogging() noexcept
	{
		try {
			gLog.OpenRelative(
				CSIDL_MYDOCUMENTS,
				R"(\My Games\Fallout4VR\F4SE\VirtualHolsters.log)");
			gLog.SetPrintLevel(IDebugLog::kLevel_DebugMessage);
			gLog.SetLogLevel(IDebugLog::kLevel_DebugMessage);
			return true;
		} catch (...) {
			::OutputDebugStringA(
				"VirtualHolsters: failed to initialize the legacy log.\n");
			return false;
		}
	}

	void LogInfo(const char* message) noexcept
	{
		if (message) {
			_MESSAGE("%s", message);
		}
	}

	void LogError(const char* message) noexcept
	{
		if (message) {
			_ERROR("%s", message);
			::OutputDebugStringA(message);
		}
	}

	bool RegisterPapyrus(void* papyrusInterface) noexcept
	{
		try {
			auto* papyrus =
				static_cast<F4SEPapyrusInterface*>(papyrusInterface);
			if (!papyrus) {
				LogError("VirtualHolsters: F4SE Papyrus interface is unavailable.");
				return false;
			}
			if (!papyrus->Register(Holsters::RegisterFuncs)) {
				LogError(
					"VirtualHolsters: failed to register Papyrus functions.");
				return false;
			}
			return true;
		} catch (const std::exception& error) {
			ReportException("Papyrus registration", error.what());
			return false;
		} catch (...) {
			ReportException("Papyrus registration", nullptr);
			return false;
		}
	}

	bool OnGameLoaded() noexcept
	{
		try {
			static bool menuHandlerRegistered = false;
			if (!menuHandlerRegistered) {
				Holsters::MenuOpenCloseHandler::Register();
				menuHandlerRegistered = true;
			}

			VRHook::InitVRSystem();
			if (!VRHook::g_vrHook) {
				LogError(
					"VirtualHolsters: direct OpenVR bridge allocation failed.");
				return false;
			}
			Holsters::mainStartup();
			return true;
		} catch (const std::exception& error) {
			ReportException("GameLoaded", error.what());
			return false;
		} catch (...) {
			ReportException("GameLoaded", nullptr);
			return false;
		}
	}

	bool OnPostLoadGame() noexcept
	{
		try {
			Holsters::saveGameLoaded();
			return true;
		} catch (const std::exception& error) {
			ReportException("PostLoadGame", error.what());
			return false;
		} catch (...) {
			ReportException("PostLoadGame", nullptr);
			return false;
		}
	}

	void RunFrame()
	{
		Holsters::MainLoop();
	}
}
