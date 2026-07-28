#pragma once

namespace VRSceneGraph
{
	[[nodiscard]] bool Initialize() noexcept;

	void AttachChild(
		void* parent,
		void* child,
		bool firstAvailable) noexcept;
	void RemoveChild(void* parent, void* child) noexcept;
}
