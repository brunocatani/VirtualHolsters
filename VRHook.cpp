#include "VRHook.h"

#include "matrix.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace VRHook
{
	namespace
	{
		constexpr float kMaximumHapticSeconds = 0.25F;
		constexpr std::uint16_t kMaximumPulseMicroseconds = 3999;

		std::unique_ptr<VRSystem> s_vrSystem;

		void HmdMatrixToNiTransform(
			NiTransform* transform,
			vr::TrackedDevicePose_t* pose)
		{
			using Function = decltype(&HmdMatrixToNiTransform);
			RelocAddr<Function> function(0x1BAB210);
			function(transform, pose);
		}
	}

	RelocPtr<std::uint64_t*> vrDataStruct(0x59429C0);
	VRSystem* g_vrHook = nullptr;

	VRSystem::VRSystem() noexcept
	{
		(void)RefreshInterfaces();
	}

	bool VRSystem::RefreshInterfaces() noexcept
	{
		// Fallout 4 VR already owns the OpenVR session. These accessors only
		// borrow the interfaces exposed by that live process session.
		auto* currentSystem = vr::VRSystem();
		if (!currentSystem) {
			system_ = nullptr;
			compositor_ = nullptr;
			leftControllerAvailable_ = false;
			rightControllerAvailable_ = false;
			return false;
		}

		if (system_ != currentSystem) {
			system_ = currentSystem;
			compositor_ = vr::VRCompositor();
			DiscoverTrackedDevices();
		} else if (!compositor_) {
			compositor_ = vr::VRCompositor();
		}
		return true;
	}

	void VRSystem::DiscoverTrackedDevices() noexcept
	{
		viveTrackers_.clear();
		controllers_.clear();
		if (!system_) {
			return;
		}

		try {
			for (vr::TrackedDeviceIndex_t index = 0;
				 index < vr::k_unMaxTrackedDeviceCount;
				 ++index) {
				switch (system_->GetTrackedDeviceClass(index)) {
				case vr::TrackedDeviceClass_GenericTracker:
					viveTrackers_.insert_or_assign(
						GetProperty(vr::Prop_ModelNumber_String, index),
						index);
					break;
				case vr::TrackedDeviceClass_Controller:
				{
					const auto role =
						system_->GetControllerRoleForTrackedDeviceIndex(index);
					if (role == vr::TrackedControllerRole_LeftHand) {
						controllers_.insert_or_assign("Left", index);
					} else if (
						role == vr::TrackedControllerRole_RightHand) {
						controllers_.insert_or_assign("Right", index);
					}
					break;
				}
				case vr::TrackedDeviceClass_HMD:
					controllers_.insert_or_assign("HMD", index);
					break;
				default:
					break;
				}
			}
		} catch (...) {
			viveTrackers_.clear();
			controllers_.clear();
		}
	}

	std::string VRSystem::GetProperty(
		const vr::ETrackedDeviceProperty property,
		const vr::TrackedDeviceIndex_t index) const
	{
		if (!system_) {
			return {};
		}

		std::array<char, vr::k_unMaxPropertyStringSize> value{};
		vr::TrackedPropertyError error = vr::TrackedProp_Success;
		const auto length = system_->GetStringTrackedDeviceProperty(
			index,
			property,
			value.data(),
			static_cast<std::uint32_t>(value.size()),
			&error);
		if (error != vr::TrackedProp_Success || length == 0) {
			return {};
		}
		return { value.data() };
	}

	void VRSystem::setRoomNode(NiNode* node) noexcept
	{
		roomNode_ = node;
	}

	bool VRSystem::updatePoses() noexcept
	{
		if (!RefreshInterfaces() || !compositor_) {
			return false;
		}

		return compositor_->GetLastPoses(
				   renderPoses_,
				   vr::k_unMaxTrackedDeviceCount,
				   gamePoses_,
				   vr::k_unMaxTrackedDeviceCount) ==
			vr::VRCompositorError_None;
	}

	bool VRSystem::viveTrackersPresent() const noexcept
	{
		return !viveTrackers_.empty();
	}

	bool VRSystem::setVRControllerState() noexcept
	{
		if (!RefreshInterfaces()) {
			leftControllerState_ = {};
			rightControllerState_ = {};
			leftControllerAvailable_ = false;
			rightControllerAvailable_ = false;
			return false;
		}

		const auto leftIndex =
			system_->GetTrackedDeviceIndexForControllerRole(
				vr::TrackedControllerRole_LeftHand);
		const auto rightIndex =
			system_->GetTrackedDeviceIndexForControllerRole(
				vr::TrackedControllerRole_RightHand);

		const bool leftValid =
			leftIndex != vr::k_unTrackedDeviceIndexInvalid &&
			system_->GetControllerState(
				leftIndex,
				&leftControllerState_,
				sizeof(leftControllerState_));
		const bool rightValid =
			rightIndex != vr::k_unTrackedDeviceIndexInvalid &&
			system_->GetControllerState(
				rightIndex,
				&rightControllerState_,
				sizeof(rightControllerState_));
		leftControllerAvailable_ = leftValid;
		rightControllerAvailable_ = rightValid;

		if (!leftValid) {
			leftControllerState_ = {};
		} else {
			leftPacket_ = leftControllerState_.unPacketNum;
		}
		if (!rightValid) {
			rightControllerState_ = {};
		} else {
			rightPacket_ = rightControllerState_.unPacketNum;
		}

		ServiceHaptics();
		return leftValid || rightValid;
	}

	vr::VRControllerState_t VRSystem::getControllerState(
		const TrackerType tracker) const noexcept
	{
		switch (tracker) {
		case Left:
			return leftControllerState_;
		case Right:
			return rightControllerState_;
		default:
			return {};
		}
	}

	bool VRSystem::isControllerStateAvailable(
		const TrackerType tracker) const noexcept
	{
		switch (tracker) {
		case Left:
			return leftControllerAvailable_;
		case Right:
			return rightControllerAvailable_;
		default:
			return false;
		}
	}

	void VRSystem::StartHaptics(
		const unsigned int trackedControllerId,
		const float hapticTime,
		const float hapticIntensity) noexcept
	{
		if (!RefreshInterfaces()) {
			return;
		}

		auto* request = trackedControllerId == 1 ?
			&leftHaptic_ :
			trackedControllerId == 2 ? &rightHaptic_ : nullptr;
		if (!request) {
			return;
		}

		const float seconds = std::clamp(
			hapticTime,
			0.0F,
			kMaximumHapticSeconds);
		request->intensity = std::clamp(
			hapticIntensity,
			0.0F,
			1.0F);
		request->until =
			std::chrono::steady_clock::now() +
			std::chrono::duration_cast<std::chrono::steady_clock::duration>(
				std::chrono::duration<float>(seconds));
		request->active = seconds > 0.0F && request->intensity > 0.0F;

		if (request->active) {
			PulseController(
				trackedControllerId == 1 ?
					vr::TrackedControllerRole_LeftHand :
					vr::TrackedControllerRole_RightHand,
				request->intensity);
		}
	}

	void VRSystem::ServiceHaptics() noexcept
	{
		const auto now = std::chrono::steady_clock::now();
		const auto service = [this, now](
								 HapticRequest& request,
								 const vr::ETrackedControllerRole role) {
			if (!request.active) {
				return;
			}
			if (now >= request.until) {
				request.active = false;
				return;
			}
			PulseController(role, request.intensity);
		};

		service(leftHaptic_, vr::TrackedControllerRole_LeftHand);
		service(rightHaptic_, vr::TrackedControllerRole_RightHand);
	}

	void VRSystem::PulseController(
		const vr::ETrackedControllerRole role,
		const float intensity) noexcept
	{
		if (!system_) {
			return;
		}

		const auto index =
			system_->GetTrackedDeviceIndexForControllerRole(role);
		if (index == vr::k_unTrackedDeviceIndexInvalid) {
			return;
		}

		const auto pulse = static_cast<std::uint16_t>(std::clamp(
			std::lround(
				static_cast<float>(kMaximumPulseMicroseconds) *
				std::clamp(intensity, 0.0F, 1.0F)),
			1L,
			static_cast<long>(kMaximumPulseMicroseconds)));
		system_->TriggerHapticPulse(index, 0, pulse);
	}

	void VRSystem::getTrackerNiTransformByName(
		const std::string& trackerName,
		NiTransform* transform)
	{
		if (!transform || !updatePoses()) {
			return;
		}
		const auto found = viveTrackers_.find(trackerName);
		if (found == viveTrackers_.end()) {
			return;
		}
		getTrackerNiTransformByIndex(found->second, transform);
	}

	void VRSystem::getTrackerNiTransformByIndex(
		const vr::TrackedDeviceIndex_t index,
		NiTransform* transform)
	{
		if (!transform ||
			index >= vr::k_unMaxTrackedDeviceCount ||
			!renderPoses_[index].bPoseIsValid) {
			return;
		}

		HmdMatrixToNiTransform(transform, &renderPoses_[index]);
		if (vrDataStruct != nullptr && roomNode_ != nullptr) {
			auto* worldSpaceMatrix = reinterpret_cast<NiMatrix43*>(
				reinterpret_cast<char*>(*vrDataStruct) + 0x210);
			auto* worldSpaceVector = reinterpret_cast<NiPoint3*>(
				reinterpret_cast<char*>(*vrDataStruct) + 0x158);
			transform->pos = transform->pos - *worldSpaceVector;
			transform->pos = *worldSpaceMatrix * transform->pos;
		}
	}

	void VRSystem::getControllerNiTransformByName(
		const std::string& trackerName,
		NiTransform* transform)
	{
		if (!transform || !updatePoses()) {
			return;
		}
		const auto found = controllers_.find(trackerName);
		if (found == controllers_.end()) {
			return;
		}
		getTrackerNiTransformByIndex(found->second, transform);
	}

	void VRSystem::debugPrint()
	{
		if (!updatePoses()) {
			_MESSAGE("Unable to retrieve OpenVR poses.");
			return;
		}

		for (const auto& [name, index] : viveTrackers_) {
			if (!renderPoses_[index].bPoseIsValid) {
				continue;
			}
			NiTransform renderTransform;
			HmdMatrixToNiTransform(
				&renderTransform,
				&renderPoses_[index]);
			const auto& gameMatrix =
				gamePoses_[index].mDeviceToAbsoluteTracking;
			_MESSAGE(
				"%d : %s --> render = %f %f %f |||| "
				"game = %f %f %f",
				index,
				name.c_str(),
				renderTransform.pos.x,
				renderTransform.pos.y,
				renderTransform.pos.z,
				gameMatrix.m[0][3],
				gameMatrix.m[1][3],
				gameMatrix.m[2][3]);
		}
	}

	void InitVRSystem() noexcept
	{
		if (!s_vrSystem) {
			try {
				s_vrSystem = std::make_unique<VRSystem>();
			} catch (...) {
				g_vrHook = nullptr;
				_ERROR("Unable to allocate the direct OpenVR bridge.");
				return;
			}
		}
		g_vrHook = s_vrSystem.get();
	}
}
