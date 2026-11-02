#pragma once

// Runs the mod's frame (LegacyBridge::RunFrame) from the game's main loop
// through one call site. The call's bytes are checked before patching, and a
// call another mod already redirected is chained, not replaced.

namespace MainLoopHook
{
	[[nodiscard]] bool Install() noexcept;
	void SetGameplayReady(bool ready) noexcept;
}
