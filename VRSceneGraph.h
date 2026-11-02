#pragma once

// NiNode AttachChild and RemoveChild for FO4VR. FO4VR's NiAVObject has three
// more virtual functions than flat Fallout 4, so the F4SEVR SDK declarations
// call the wrong vtable slots. These dispatch through the VR slots, checked
// against the function bytes once by Initialize().

namespace VRSceneGraph
{
	[[nodiscard]] bool Initialize() noexcept;

	void AttachChild(
		void* parent,
		void* child,
		bool firstAvailable) noexcept;
	void RemoveChild(void* parent, void* child) noexcept;
}
