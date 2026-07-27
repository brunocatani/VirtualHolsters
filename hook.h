#pragma once

namespace MainLoopHook
{
	[[nodiscard]] bool Install() noexcept;
	void SetGameplayReady(bool ready) noexcept;
}
