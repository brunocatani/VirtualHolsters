#pragma once

#include "f4se/NiNodes.h"
#include "f4se/NiTypes.h"

#include <openvr.h>

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>

namespace VRHook
{
	class VRSystem
	{
	public:
		enum TrackerType
		{
			HMD,
			Left,
			Right,
			Vive
		};

		VRSystem() noexcept;

		void setRoomNode(NiNode* node) noexcept;
		bool updatePoses() noexcept;
		[[nodiscard]] bool viveTrackersPresent() const noexcept;
		[[nodiscard]] bool setVRControllerState() noexcept;
		[[nodiscard]] vr::VRControllerState_t getControllerState(
			TrackerType tracker) const noexcept;

		void StartHaptics(
			unsigned int trackedControllerId,
			float hapticTime,
			float hapticIntensity) noexcept;

		void getTrackerNiTransformByName(
			const std::string& trackerName,
			NiTransform* transform);
		void getTrackerNiTransformByIndex(
			vr::TrackedDeviceIndex_t index,
			NiTransform* transform);
		void getControllerNiTransformByName(
			const std::string& trackerName,
			NiTransform* transform);
		void debugPrint();

	private:
		struct HapticRequest
		{
			std::chrono::steady_clock::time_point until{};
			float intensity{};
			bool active{};
		};

		[[nodiscard]] bool RefreshInterfaces() noexcept;
		void DiscoverTrackedDevices() noexcept;
		[[nodiscard]] std::string GetProperty(
			vr::ETrackedDeviceProperty property,
			vr::TrackedDeviceIndex_t index) const;
		void ServiceHaptics() noexcept;
		void PulseController(
			vr::ETrackedControllerRole role,
			float intensity) noexcept;

		std::uint32_t leftPacket_{};
		std::uint32_t rightPacket_{};
		vr::IVRSystem* system_{};
		vr::IVRCompositor* compositor_{};
		vr::VRControllerState_t rightControllerState_{};
		vr::VRControllerState_t leftControllerState_{};
		vr::TrackedDevicePose_t
			renderPoses_[vr::k_unMaxTrackedDeviceCount]{};
		vr::TrackedDevicePose_t
			gamePoses_[vr::k_unMaxTrackedDeviceCount]{};
		std::map<std::string, vr::TrackedDeviceIndex_t> viveTrackers_;
		std::map<std::string, vr::TrackedDeviceIndex_t> controllers_;
		NiNode* roomNode_{};
		HapticRequest leftHaptic_{};
		HapticRequest rightHaptic_{};
	};

	extern VRSystem* g_vrHook;

	void InitVRSystem() noexcept;
}
