#pragma once

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
