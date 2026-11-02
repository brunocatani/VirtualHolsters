#pragma once

// The boundary between the CommonLibF4VR plugin entry (main.cpp, hook.cpp) and
// the F4SEVR SDK code the original mod is built on (Holsters, menus, Papyrus).
// Every call catches exceptions, so none reaches the engine.

namespace LegacyBridge
{
	bool InitializeLogging() noexcept;
	void LogInfo(const char* message) noexcept;
	void LogError(const char* message) noexcept;

	bool RegisterPapyrus(void* papyrusInterface) noexcept;
	bool OnGameLoaded() noexcept;
	void OnGameplaySuspended() noexcept;
	bool OnPostLoadGame() noexcept;
	void RunFrame();
}
