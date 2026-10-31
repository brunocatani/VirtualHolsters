
// Thanks to RollingRock for orginal code from which this was adapted

#include "Holsters.h"
#include "ConfigPaths.h"
#include "HolsterInteraction.h"
#include "HolsterSlotPolicy.h"
#include "utils.h"
#include "Offsets.h"
#include "weaponOffset.h"
#include "VRHook.h"
#include "VRSceneGraph.h"
#include "VirtualHolstersAPI.h"
#include "NativeEquipment.h"
#include "RockProvider.h"
#include "f4se/GameMenus.h"
#include "ConfigModeMenu.h"
#include "MenuChecker.h"
#include "InvSorting.h"
#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <fstream>
#include <memory>
#include "f4se/NiNodes.h"
#include "f4se/GameReferences.h"
#include "f4se/BSCollision.h"

namespace {
    // On a missing file, record the same defaults the loader reads below.
    class FirstRunIni : public CSimpleIniA {
    public:
        explicit FirstRunIni(bool create) : create_(create) {}
        long GetLongValue(const char* section, const char* key, long fallback) {
            if (create_) SetLongValue(section, key, fallback);
            return CSimpleIniA::GetLongValue(section, key, fallback);
        }
        double GetDoubleValue(const char* section, const char* key, double fallback) {
            if (create_) SetDoubleValue(section, key, fallback);
            return CSimpleIniA::GetDoubleValue(section, key, fallback);
        }
        bool GetBoolValue(const char* section, const char* key, bool fallback) {
            if (create_) SetBoolValue(section, key, fallback);
            return CSimpleIniA::GetBoolValue(section, key, fallback);
        }
    private:
        bool create_;
    };

    bool saveNewDefaults(const CSimpleIniA& ini, const std::filesystem::path& path) {
        try {
            std::filesystem::create_directories(path.parent_path());
            std::string text;
            if (ini.Save(text, false) < 0) return false;
            const auto file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (file == INVALID_HANDLE_VALUE) {
                const auto error = GetLastError();
                return error == ERROR_FILE_EXISTS || error == ERROR_ALREADY_EXISTS;
            }
            std::unique_ptr<void, decltype(&CloseHandle)> output(file, &CloseHandle);
            DWORD written = 0;
            if (WriteFile(output.get(), text.data(), static_cast<DWORD>(text.size()), &written, nullptr) &&
                written == text.size() && FlushFileBuffers(output.get())) return true;
            output.reset();
            std::error_code error;
            std::filesystem::remove(path, error);
        } catch (const std::exception& error) {
            _WARNING("Cannot create VirtualHolsters defaults: %s", error.what());
        }
        return false;
    }
}

bool gPlayerisSneaking = false;
bool gInPowerArmor = false;
bool _isTouchEnabled;
bool ghapticsEnabled = false;
bool gshowSpheres = false;
bool gShowWeapons = false;
bool fistsPressed = false;
bool infoPressed = false;
bool saveloaded = false;
bool MenuCheck = false;
int doublepress = 0;
bool gIsRightHanded;
std::map<std::string, int> boneTreeMap;
std::vector<std::string> boneTreeVec;
std::string holsteredWeapNames[8] = { "Empty" ,"Empty", "Empty", "Empty", "Empty", "Empty", "Empty", "Empty" };
TESObjectREFR* HolsterContainers[8] = { nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr };
UInt32 gCurHolster;

namespace Holsters {

	RelocAddr<_IsSneaking> IsSneaking(0x24d20);

	typedef int(*_loadNif)(uint64_t path, uint64_t mem, uint64_t flags);
	RelocAddr<_loadNif> loadNif(0x1d0dee0);

	typedef NiNode* (*_cloneNode)(NiNode* node, NiCloneProcess* obj);
	RelocAddr<_cloneNode> cloneNode(0x1c13ff0);

	typedef NiNode* (*_addNode)(uint64_t attachNode, NiAVObject* node);
	RelocAddr<_addNode> addNode(0xada20);

	RelocAddr<uint64_t> g_frameCounter(0x65a2b48);
	RelocAddr<UInt64*> cloneAddr1(0x36ff560);
	RelocAddr<UInt64*> cloneAddr2(0x36ff564);

	const char* HolsterEventName = "HolsterEvent";
	RegistrationSetHolder<NullParameters>      g_HolsterEventRegs;
	std::map<UInt32, MyHolster*> HolsterRegisteredObjects;
	UInt32 nextHolsterHandle;
	std::map<UInt32, MyWeapon*> WeaponRegisteredObjects;
	UInt32 nextWeaponHandle;

	int c_holsterButtonID = vr::EVRButtonId::k_EButton_A;
	int c_configButtonID = vr::EVRButtonId::k_EButton_SteamVR_Touchpad;
	int c_configExitButtonID = vr::EVRButtonId::k_EButton_SteamVR_Trigger;
	int c_configModeShiftButton = vr::EVRButtonId::k_EButton_Grip;
	int c_configModeRotateButton = vr::EVRButtonId::k_EButton_A;
	int c_configChangeHolButton = vr::EVRButtonId::k_EButton_Grip;
	int c_selfieModeButtonID = vr::EVRButtonId::k_EButton_SteamVR_Touchpad;
	int c_movecamButtonID = vr::EVRButtonId::k_EButton_SteamVR_Touchpad;
	int c_equipButtonID = vr::EVRButtonId::k_EButton_SteamVR_Trigger;
	int c_ArtWorkButtonID = vr::EVRButtonId::k_EButton_ApplicationMenu;
	bool c_VATSDisabled = false;
	bool c_ActivationDisabled = false;
	bool menuOpenSticky = false;
	int holsterMode = 0;
	int weaponMode = 0;
	int c_mode = 0;
	int camDistancePA = 0;
	int camDistance = 0;
	int dist = 0;
	
	int holsterArtWorkPresets[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	int PAholsterArtWorkPresets[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	int PAholsterMaxValue[8];
	int holsterMaxValue[8];
	float c_hapticDuration = 0.05F;
	float c_hapticIntensity = 0.5F;
	std::string HolsterBones[8] = { "nout", "HmdNode", "HmdNode", "LLeg_Thigh", "RLeg_Thigh", "SPINE1", "Chest", "Chest" };
	std::string HolsterNames[8] = { "nout", "LShoulderHolster", "RShoulderHolster", "LHipHolster", "RHipHolster", "LowerBackHolster", "LeftChestHolster", "RightChestHolster" };
	std::string WeaponBones[8] = { "nout", "LArm_Collarbone", "RArm_Collarbone", "LLeg_Thigh", "RLeg_Thigh", "SPINE1", "Chest", "Chest" };
	std::string PAWeaponBones[8] = { "nout", "Chest", "Chest", "LLeg_Thigh", "RLeg_Thigh", "SPINE1", "Chest", "Chest" };
	std::string VisualArt[8];
	const char* hArt[8] = { "nout", "HolsterArt1", "HolsterArt2", "HolsterArt3", "HolsterArt4", "HolsterArt5", "HolsterArt6", "HolsterArt7" };
	UInt32 CurrentEquippedArmor[3][3];
	UInt32 LastEquippedArmor[3][3];
	UInt32 lastInHolster[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
	std::vector<std::string> c_WeaponBlacklist[8]{};
	std::vector<std::string> c_WeaponBlacklistPA[8]{};
	bool testclonefix = false;
	std::vector<BSTriShape*> c_OriginalNodes{};
	std::vector<BSTriShape*> c_CloneNodes{};
	std::vector<std::string> c_ExcludedMagazines{};
	std::vector<std::string> c_SwordDrawSFX{};
	char* weaponRemovalStrings[5]{ "Mag", "ullet", "Cartridge", "Casing", "Object043:0" };
	char* meleeRemovalStrings[2]{ "Blood", "blood" };
	std::vector<char*> c_MeleeRemovalStrings{};
	bool _isConfigModeLShiftButtonPressed;
	bool _isConfigModeRShiftButtonPressed;
	bool _isConfigModeSaveButtonPressed;
	bool _isConfigButtonPressed;
	bool _isInfoButtonPressed;
	bool _isChangeConfigModeButtonPressed;
	bool _isChangeHolsterButtonPressed;
	bool _isSelfieModeButtonButtonPressed;
	bool _isMovecamButtonPressed;
	bool _isEquipButtonPressed;
	bool _isArtWorkButtonPressed;
	bool _WeaponHolsterInProgress;
	bool _isPipboyOpen;
	bool _selfieModeActive;
	bool _camMovedBack;
	bool _fistRun = true;
	bool gamesaveloaded = false;
	bool artSwitched = false;
	bool isInMasterMode = false;
	float rAxisOffsetX;
	float rAxisOffsetY;
	float lAxisOffsetX;
	float lAxisOffsetY;
	float RXOffset;
	float RYOffset;
	float RZOffset;
	bool equipsticky = false;
	bool fistssticky = false;
	bool isHolsterIntersected = false;
	bool _isConfigModeActive = false;
	bool initBoneTreeFlag = true;

	namespace
	{
		constexpr std::size_t kMaximumHolsterSlots = 7;
		SlotPolicy::SlotMask s_enabledHolsterSlots =
			SlotPolicy::kAllSlotsMask;

		using Clock = std::chrono::steady_clock;

		// A holster draw waits this long for ROCK or the engine to put the
		// weapon in the hand before the slot keeps its weapon again.
		constexpr auto kDrawTimeout = std::chrono::milliseconds(2000);
		// A weapon put away from a hand eases into its holster pose.
		constexpr float kHolsterBlendSeconds = 0.18F;
		constexpr float kHolsterHapticSeconds = 0.03F;

		struct HandInteractionRuntime
		{
			Interaction::HolsterGesture gesture{};
			UInt32 zone{};
			bool anchorAvailable{};
			// The current press started inside a zone; ROCK input stays
			// reserved for that hand until the button is released.
			bool pressFromZone{};
		};

		// What a hand holds, from ROCK with ROCK bound, else the single
		// vanilla weapon in the configured weapon hand.
		struct HandWeaponFacts
		{
			bool known{};
			bool holds{};
			bool canReceive{};
			UInt32 nativeIndex{};
			UInt32 formId{};
		};

		enum class HolsterAction : std::uint8_t
		{
			kNone,
			kHolster,
			kDraw,
			kSwap
		};

		// One running draw (or swap) per hand. The slot keeps its tag and
		// display until the weapon is in the hand, so a refused or failed equip
		// never empties a holster and the weapon never disappears in between.
		struct HandTransfer
		{
			bool active{};
			bool requested{};
			bool isLeft{};
			UInt32 slot{};
			UInt32 formId{};
			std::string name;
			std::uint64_t command{};
			Clock::time_point deadline{};
			// Swap: the weapon leaving the hand takes the slot on success. Its
			// clone stays hidden and follows the hand's model until the stored
			// weapon is presented, so the two never show at once.
			MyWeapon* outgoing{};
			std::string outgoingName;
			UInt16 outgoingNameLength{};
			bool outgoingHidden{};
			UInt32 outgoingIndex{};
			UInt32 outgoingFormId{};
		};

		struct GrabSettingSnapshot
		{
			float primaryDistance{};
			float secondaryDistance{};
			float floorMultiplier{};
			bool primaryCaptured{};
			bool secondaryCaptured{};
			bool floorCaptured{};
		};

		HandInteractionRuntime s_leftHand;
		HandInteractionRuntime s_rightHand;
		GrabSettingSnapshot s_grabSettingSnapshot;
		bool s_activationSuppressed = false;
		std::array<HandTransfer, 2> s_transfers{};   // [0] right, [1] left
		Clock::time_point s_lastFrame{};
		float s_frameSeconds = 0.0F;
		Clock::time_point s_fistsPressedUntil{};
		Clock::time_point s_menuCheckUntil{};
		Clock::time_point s_infoPressedUntil{};

		HandTransfer& TransferFor(const bool isLeft) noexcept
		{
			return s_transfers[isLeft ? 1 : 0];
		}

		// FO4VR stores child axes as matrix rows: a local vector maps to world
		// as Transpose()*v (raw compose helper 0x1401A8D60, as ROCK verifies).
		NiPoint3 LocalVectorToWorld(const NiMatrix43& m, const NiPoint3& v) noexcept
		{
			return NiPoint3(
				m.data[0][0] * v.x + m.data[1][0] * v.y + m.data[2][0] * v.z,
				m.data[0][1] * v.x + m.data[1][1] * v.y + m.data[2][1] * v.z,
				m.data[0][2] * v.x + m.data[1][2] * v.y + m.data[2][2] * v.z);
		}

		NiPoint3 WorldVectorToLocal(const NiMatrix43& m, const NiPoint3& v) noexcept
		{
			return NiPoint3(
				m.data[0][0] * v.x + m.data[0][1] * v.y + m.data[0][2] * v.z,
				m.data[1][0] * v.x + m.data[1][1] * v.y + m.data[1][2] * v.z,
				m.data[2][0] * v.x + m.data[2][1] * v.y + m.data[2][2] * v.z);
		}

		// Stored-row product: the engine's world rotation is child * parent.
		NiMatrix43 MultiplyStored(const NiMatrix43& lhs, const NiMatrix43& rhs) noexcept
		{
			NiMatrix43 result{};
			for (int row = 0; row < 3; ++row) {
				for (int column = 0; column < 3; ++column) {
					result.data[row][column] =
						lhs.data[row][0] * rhs.data[0][column] +
						lhs.data[row][1] * rhs.data[1][column] +
						lhs.data[row][2] * rhs.data[2][column];
				}
				result.data[row][3] = 0.0F;
			}
			return result;
		}

		NiPoint3 LocalPointToWorld(const NiTransform& parent, const NiPoint3& local) noexcept
		{
			const NiPoint3 offset = LocalVectorToWorld(parent.rot, local);
			return NiPoint3(
				parent.pos.x + offset.x * parent.scale,
				parent.pos.y + offset.y * parent.scale,
				parent.pos.z + offset.z * parent.scale);
		}

		// The local transform that places a child of `parent` at `world`.
		bool LocalFromWorld(const NiTransform& parent, const NiTransform& world, NiTransform& local) noexcept
		{
			if (!std::isfinite(parent.scale) || std::fabs(parent.scale) < 0.0001F) {
				return false;
			}
			const float inverseScale = 1.0F / parent.scale;
			local.rot = MultiplyStored(world.rot, parent.rot.Transpose());
			const NiPoint3 delta(
				world.pos.x - parent.pos.x,
				world.pos.y - parent.pos.y,
				world.pos.z - parent.pos.z);
			local.pos = WorldVectorToLocal(parent.rot, delta);
			local.pos.x *= inverseScale;
			local.pos.y *= inverseScale;
			local.pos.z *= inverseScale;
			local.scale = world.scale * inverseScale;
			return std::isfinite(local.pos.x) && std::isfinite(local.pos.y) &&
				std::isfinite(local.pos.z) && std::isfinite(local.scale);
		}

		struct Quaternion
		{
			float w{ 1.0F }, x{}, y{}, z{};
		};

		Quaternion ToQuaternion(const NiMatrix43& m) noexcept
		{
			const float (*r)[4] = m.data;
			Quaternion q{};
			const float trace = r[0][0] + r[1][1] + r[2][2];
			if (trace > 0.0F) {
				const float s = std::sqrt(trace + 1.0F) * 2.0F;
				q.w = 0.25F * s;
				q.x = (r[2][1] - r[1][2]) / s;
				q.y = (r[0][2] - r[2][0]) / s;
				q.z = (r[1][0] - r[0][1]) / s;
			} else if (r[0][0] > r[1][1] && r[0][0] > r[2][2]) {
				const float s = std::sqrt(1.0F + r[0][0] - r[1][1] - r[2][2]) * 2.0F;
				q.w = (r[2][1] - r[1][2]) / s;
				q.x = 0.25F * s;
				q.y = (r[0][1] + r[1][0]) / s;
				q.z = (r[0][2] + r[2][0]) / s;
			} else if (r[1][1] > r[2][2]) {
				const float s = std::sqrt(1.0F + r[1][1] - r[0][0] - r[2][2]) * 2.0F;
				q.w = (r[0][2] - r[2][0]) / s;
				q.x = (r[0][1] + r[1][0]) / s;
				q.y = 0.25F * s;
				q.z = (r[1][2] + r[2][1]) / s;
			} else {
				const float s = std::sqrt(1.0F + r[2][2] - r[0][0] - r[1][1]) * 2.0F;
				q.w = (r[1][0] - r[0][1]) / s;
				q.x = (r[0][2] + r[2][0]) / s;
				q.y = (r[1][2] + r[2][1]) / s;
				q.z = 0.25F * s;
			}
			return q;
		}

		NiMatrix43 FromQuaternion(const Quaternion& q) noexcept
		{
			NiMatrix43 m{};
			m.data[0][0] = 1.0F - 2.0F * (q.y * q.y + q.z * q.z);
			m.data[0][1] = 2.0F * (q.x * q.y - q.z * q.w);
			m.data[0][2] = 2.0F * (q.x * q.z + q.y * q.w);
			m.data[1][0] = 2.0F * (q.x * q.y + q.z * q.w);
			m.data[1][1] = 1.0F - 2.0F * (q.x * q.x + q.z * q.z);
			m.data[1][2] = 2.0F * (q.y * q.z - q.x * q.w);
			m.data[2][0] = 2.0F * (q.x * q.z - q.y * q.w);
			m.data[2][1] = 2.0F * (q.y * q.z + q.x * q.w);
			m.data[2][2] = 1.0F - 2.0F * (q.x * q.x + q.y * q.y);
			return m;
		}

		NiMatrix43 SlerpRotation(const NiMatrix43& from, const NiMatrix43& to, const float t) noexcept
		{
			Quaternion a = ToQuaternion(from);
			Quaternion b = ToQuaternion(to);
			float dot = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
			if (dot < 0.0F) {
				b = { -b.w, -b.x, -b.y, -b.z };
				dot = -dot;
			}
			Quaternion q{};
			if (dot > 0.9995F) {
				q = { a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
			} else {
				const float theta = std::acos(dot);
				const float sinTheta = std::sin(theta);
				const float wa = std::sin((1.0F - t) * theta) / sinTheta;
				const float wb = std::sin(t * theta) / sinTheta;
				q = { a.w * wa + b.w * wb, a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb };
			}
			const float length = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
			if (length > 0.0001F) {
				q = { q.w / length, q.x / length, q.y / length, q.z / length };
			}
			return FromQuaternion(q);
		}

		NiTransform BlendTransforms(const NiTransform& from, const NiTransform& to, const float t) noexcept
		{
			NiTransform result{};
			result.rot = SlerpRotation(from.rot, to.rot, t);
			result.pos = NiPoint3(
				from.pos.x + (to.pos.x - from.pos.x) * t,
				from.pos.y + (to.pos.y - from.pos.y) * t,
				from.pos.z + (to.pos.z - from.pos.z) * t);
			result.scale = from.scale + (to.scale - from.scale) * t;
			return result;
		}

		void PulseHand(const bool isLeft)
		{
			if (ghapticsEnabled && VRHook::g_vrHook) {
				VRHook::g_vrHook->StartHaptics(isLeft ? 1 : 2, kHolsterHapticSeconds, c_hapticIntensity);
			}
		}

		const HandInteractionRuntime& GetHandRuntime(
			const bool isLeft) noexcept
		{
			return isLeft ? s_leftHand : s_rightHand;
		}

		bool TryGetHandPosition(
			BSFlattenedBoneTree* tree,
			const bool isLeft,
			NiPoint3& position) noexcept
		{
			if (!tree || !tree->transforms || tree->numTransforms <= 0) {
				return false;
			}

			const char* boneName =
				isLeft ? "LArm_Finger23" : "RArm_Finger23";
			const auto found = boneTreeMap.find(boneName);
			if (found == boneTreeMap.end() ||
				found->second < 0 ||
				found->second >= tree->numTransforms) {
				return false;
			}

			position = tree->transforms[found->second].world.pos;
			return true;
		}

		void SendActivationEvent(const bool suppress)
		{
			SInt32 eventType = suppress ?
				HolsterEvent_DisableActivate :
				HolsterEvent_EnableActivate;
			if (g_HolsterEventRegs.m_data.size() > 0) {
				auto dispatch = [&eventType](
									const EventRegistration<NullParameters>& reg) {
					SendPapyrusEvent2<SInt32, UInt32>(
						reg.handle,
						reg.scriptName,
						HolsterEventName,
						eventType,
						gCurHolster);
				};
				g_HolsterEventRegs.ForEach(dispatch);
			}
		}

		void CaptureAndDisableGrabSettings()
		{
			s_grabSettingSnapshot = {};

			if (auto* setting =
					GetINISetting("fPrimaryWandMaxGrabDistance:VRWand")) {
				s_grabSettingSnapshot.primaryDistance = setting->data.f32;
				s_grabSettingSnapshot.primaryCaptured = true;
				setting->SetDouble(0.0);
			}
			if (auto* setting =
					GetINISetting("fSecondaryWandMaxGrabDistance:VRWand")) {
				s_grabSettingSnapshot.secondaryDistance = setting->data.f32;
				s_grabSettingSnapshot.secondaryCaptured = true;
				setting->SetDouble(0.0);
			}
			if (auto* setting = GetINISetting(
					"fDownwardGrabFloorDistanceMultiplier:VRWand")) {
				s_grabSettingSnapshot.floorMultiplier = setting->data.f32;
				s_grabSettingSnapshot.floorCaptured = true;
				setting->SetDouble(0.0);
			}
		}

		void RestoreGrabSettings()
		{
			if (s_grabSettingSnapshot.primaryCaptured) {
				if (auto* setting =
						GetINISetting("fPrimaryWandMaxGrabDistance:VRWand")) {
					setting->SetDouble(
						s_grabSettingSnapshot.primaryDistance);
				}
			}
			if (s_grabSettingSnapshot.secondaryCaptured) {
				if (auto* setting = GetINISetting(
						"fSecondaryWandMaxGrabDistance:VRWand")) {
					setting->SetDouble(
						s_grabSettingSnapshot.secondaryDistance);
				}
			}
			if (s_grabSettingSnapshot.floorCaptured) {
				if (auto* setting = GetINISetting(
						"fDownwardGrabFloorDistanceMultiplier:VRWand")) {
					setting->SetDouble(
						s_grabSettingSnapshot.floorMultiplier);
				}
			}
			s_grabSettingSnapshot = {};
		}

		void SetActivationSuppressed(const bool suppress)
		{
			if (suppress == s_activationSuppressed) {
				return;
			}

			if (suppress) {
				CaptureAndDisableGrabSettings();
			} else {
				RestoreGrabSettings();
			}
			SendActivationEvent(suppress);
			s_activationSuppressed = suppress;
		}

		void UpdateHolsterSpherePresentation()
		{
			for (auto& [slot, holster] : HolsterRegisteredObjects) {
				if (!holster) {
					continue;
				}

				holster->stickyLeft = s_leftHand.zone == slot;
				holster->stickyRight = s_rightHand.zone == slot;
				if (_isConfigModeActive || !holster->debugSphere) {
					continue;
				}

				const bool active =
					holster->stickyLeft || holster->stickyRight;
				if (active && gshowSpheres) {
					holster->debugSphere->flags &=
						0xfffffffffffffffe;
					holster->debugSphere->m_localTransform.scale = 12.0F;
				} else {
					holster->debugSphere->flags |= 0x1;
					holster->debugSphere->m_localTransform.scale = 0.0F;
				}
			}
		}

		void ClearHandContacts()
		{
			s_leftHand.zone = Interaction::kNoSlot;
			s_leftHand.anchorAvailable = false;
			s_rightHand.zone = Interaction::kNoSlot;
			s_rightHand.anchorAvailable = false;
			isHolsterIntersected = false;
			for (auto& [slot, holster] : HolsterRegisteredObjects) {
				(void)slot;
				if (holster) {
					holster->stickyLeft = false;
					holster->stickyRight = false;
				}
			}
		}

		// Defined with the weapon display functions below.
		NiNode* attachWeaponClone(MyWeapon& record, NiNode* source, UInt32 handle,
			const char* nodeName, const NiTransform* startWorld);
		void attachHolsterArt(MyWeapon& record, UInt32 handle);
		void releaseWeaponClone(MyWeapon*& record);

		bool SlotAvailable(const UInt32 slot) noexcept
		{
			return slot >= 1 && slot <= kMaximumHolsterSlots &&
				isHolsterSlotEnabled(slot) && HolsterContainers[slot] && *g_player;
		}

		HandWeaponFacts GetHandWeaponFacts(const bool isLeft)
		{
			HandWeaponFacts facts{};
			if (Rock::IsBound()) {
				// ROCK owns hand assignment; act only on its current publication.
				if (!Rock::IsActive()) {
					return facts;
				}
				const auto weapon = Rock::GetHandWeapon(isLeft);
				facts.known = true;
				facts.holds = weapon.role == Rock::HandRole::kFiring &&
					weapon.formId != 0 && weapon.slot <= 1;
				facts.canReceive = weapon.role != Rock::HandRole::kSupport;
				facts.nativeIndex = facts.holds ? weapon.slot : 0;
				facts.formId = facts.holds ? weapon.formId : 0;
				return facts;
			}
			if (!*g_player || !*g_gameVM || !(*g_gameVM)->m_virtualMachine) {
				return facts;
			}
			// Vanilla FO4VR holds one weapon, in the configured weapon hand; a
			// draw into the other hand would replace it.
			facts.known = true;
			const bool weaponHand = isLeft == !gIsRightHanded;
			TESObjectWEAP* drawn = (*g_player)->actorState.IsWeaponDrawn() ?
				Offsets::GetEquippedWeapon((*g_gameVM)->m_virtualMachine, 0, *g_player, 0) :
				nullptr;
			facts.holds = weaponHand && drawn;
			facts.canReceive = weaponHand || !drawn;
			facts.formId = drawn ? drawn->formID : 0;
			return facts;
		}

		bool SlotHasTransfer(const UInt32 slot) noexcept
		{
			for (const auto& transfer : s_transfers) {
				if (transfer.active && transfer.slot == slot) {
					return true;
				}
			}
			return false;
		}

		std::array<bool, 8> BusySlots() noexcept
		{
			std::array<bool, 8> busy{};
			for (const auto& transfer : s_transfers) {
				if (transfer.active && transfer.slot < busy.size()) {
					busy[transfer.slot] = true;
				}
			}
			return busy;
		}

		HolsterAction DecideAction(
			const Interaction::GestureActionKind kind,
			const UInt32 slot,
			const bool isLeft,
			const HandWeaponFacts& facts)
		{
			if (!facts.known || !SlotAvailable(slot) ||
				TransferFor(isLeft).active || SlotHasTransfer(slot)) {
				return HolsterAction::kNone;
			}
			const bool occupied = holsteredWeapNames[slot] != "Empty";
			switch (kind) {
			case Interaction::GestureActionKind::kPullOut:
				return occupied && !facts.holds && facts.canReceive ?
					HolsterAction::kDraw :
					HolsterAction::kNone;
			case Interaction::GestureActionKind::kReleaseInZone:
				if (facts.holds) {
					return occupied ? HolsterAction::kSwap : HolsterAction::kHolster;
				}
				return occupied && facts.canReceive ?
					HolsterAction::kDraw :
					HolsterAction::kNone;
			case Interaction::GestureActionKind::kNone:
				break;
			}
			return HolsterAction::kNone;
		}

		// Where the hand's weapon model is on screen now. The first weapon's
		// model is rendered where its node stands. ROCK re-places the second
		// weapon's model late in the frame after the engine returns it to its
		// biped carry, so its last placement comes from ROCK (getGrip(1) is
		// that model's own world).
		bool CaptureHeldWeapon(const HandWeaponFacts& facts, NiNode*& model, NiTransform& world)
		{
			model = static_cast<NiNode*>(
				NativeEquipment::FindEquippedModel(facts.nativeIndex, facts.formId));
			Rock::WeaponWorld placed{};
			if (model && facts.nativeIndex == 1 && Rock::IsActive() && Rock::GetWeaponWorld(1, placed)) {
				for (int row = 0; row < 3; ++row) {
					for (int column = 0; column < 3; ++column) {
						world.rot.data[row][column] = placed.rotate[row * 3 + column];
					}
					world.rot.data[row][3] = 0.0F;
				}
				world.pos = NiPoint3(placed.translate[0], placed.translate[1], placed.translate[2]);
				world.scale = placed.scale;
				return model != nullptr;
			}
			if (model) {
				world = model->m_worldTransform;
			}
			return model != nullptr;
		}

		WeaponStack FindHandStack(const HandWeaponFacts& facts)
		{
			if (!facts.holds || facts.nativeIndex > 1) {
				return {};
			}
			const auto equipped = NativeEquipment::ReadEquippedWeapons()[facts.nativeIndex];
			if (equipped.formId != facts.formId) {
				_WARNING("Holster refused: hand weapon %08X is not native index %u (%08X)",
					facts.formId, facts.nativeIndex, equipped.formId);
				return {};
			}
			return FindEquippedStack(facts.formId, equipped.instance);
		}

		bool UnequipHandWeapon(const HandWeaponFacts& facts, const WeaponStack& stack)
		{
			if (Rock::IsBound()) {
				return Rock::UnequipWeapon(facts.nativeIndex, facts.formId) == Rock::Result::kOk;
			}
			return NativeUnequip(stack);
		}

		// The hand's weapon goes into the empty slot. The clone appears where
		// the hand held the weapon in the frame the weapon leaves the hand, so
		// it never disappears, then eases into the holster.
		bool HolsterHandWeapon(const bool isLeft, const UInt32 slot, const HandWeaponFacts& facts)
		{
			const WeaponStack stack = FindHandStack(facts);
			if (!stack.found) {
				_WARNING("Holster %u refused: the %s hand's weapon stack was not found", slot, isLeft ? "left" : "right");
				return false;
			}
			if (!CanTagAnother(stack.name, true)) {
				_MESSAGE("Holster %u refused: every '%s' is already holstered", slot, stack.name.c_str());
				return false;
			}
			NiNode* model = nullptr;
			NiTransform world{};
			const bool captured = CaptureHeldWeapon(facts, model, world);
			nDestoryWeapon(slot, true, false);
			if (gShowWeapons && captured) {
				nRegisterWeapon(stack.form->GetFullName(), stack.form, model, slot, stack.melee, &world);
			}
			const auto registered = WeaponRegisteredObjects.find(slot);
			const bool presented = registered != WeaponRegisteredObjects.end() &&
				registered->second && registered->second->WeapMesh;
			if (!UnequipHandWeapon(facts, stack)) {
				_WARNING("Holster %u refused: the %s hand's weapon %08X could not be unequipped",
					slot, isLeft ? "left" : "right", facts.formId);
				nDestoryWeapon(slot, true, false);
				return false;
			}
			SetHolsterTag(slot, stack.name, stack.nameLength);
			if (gShowWeapons && !presented) {
				QueueStoredVisual(FindStoredStack(stack.name), slot);
			}
			PulseHand(isLeft);
			_MESSAGE("Holster %u stored '%s' from the %s hand (index %u)",
				slot, stack.name.c_str(), isLeft ? "left" : "right", facts.nativeIndex);
			return true;
		}

		bool RequestDraw(HandTransfer& transfer)
		{
			const WeaponStack stored = FindStoredStack(transfer.name);
			if (!stored.found) {
				return false;
			}
			if (Rock::IsBound()) {
				std::uint64_t command = 0;
				const auto result = Rock::RequestEquip(
					transfer.isLeft, stored.form->formID, static_cast<std::uint32_t>(stored.stackID), command);
				if (result == Rock::Result::kBusy) {
					return true;  // retried next frame until the deadline
				}
				if (result != Rock::Result::kQueued) {
					return false;
				}
				transfer.command = command;
				transfer.requested = true;
				return true;
			}
			transfer.requested = NativeEquip(stored);
			return transfer.requested;
		}

		// The stored weapon goes into the hand. With swap, the hand's weapon
		// takes the slot once the stored one is in the hand.
		bool BeginDraw(const bool isLeft, const UInt32 slot, const HandWeaponFacts& facts, const bool swap)
		{
			auto& transfer = TransferFor(isLeft);
			transfer = {};
			transfer.active = true;
			transfer.isLeft = isLeft;
			transfer.slot = slot;
			transfer.name = holsteredWeapNames[slot];
			transfer.deadline = Clock::now() + kDrawTimeout;
			const WeaponStack stored = FindStoredStack(transfer.name);
			if (!stored.found) {
				transfer = {};
				ReconcileHolsterTags(BusySlots());
				return false;
			}
			transfer.formId = stored.form->formID;

			if (swap) {
				const WeaponStack outgoing = FindHandStack(facts);
				if (!outgoing.found ||
					(outgoing.name != transfer.name && !CanTagAnother(outgoing.name, true))) {
					transfer = {};
					return false;
				}
				transfer.outgoingName = outgoing.name;
				transfer.outgoingNameLength = outgoing.nameLength;
				NiNode* model = nullptr;
				NiTransform world{};
				if (gShowWeapons && CaptureHeldWeapon(facts, model, world)) {
					auto* record = new MyWeapon(
						FindNode(gInPowerArmor ? PAWeaponBones[slot].c_str() : WeaponBones[slot].c_str()),
						HolsterNames[slot].c_str(), outgoing.melee, outgoing.form->GetFullName(), outgoing.form->formID);
					const std::string nodeName = HolsterNames[slot] + "Swap";
					if (record->wbone && attachWeaponClone(*record, model, slot, nodeName.c_str(), &world)) {
						record->WeapMesh->flags |= 0x1;
						record->blending = false;
						transfer.outgoing = record;
						transfer.outgoingHidden = true;
						transfer.outgoingIndex = facts.nativeIndex;
						transfer.outgoingFormId = facts.formId;
					} else {
						delete record;
					}
				}
			}

			if (!RequestDraw(transfer)) {
				releaseWeaponClone(transfer.outgoing);
				_WARNING("Holster %u draw refused for '%s'", slot, transfer.name.c_str());
				transfer = {};
				return false;
			}
			return true;
		}

		void AbortTransfer(HandTransfer& transfer, const char* reason)
		{
			_WARNING("Holster %u draw of '%s' into the %s hand ended without the weapon: %s",
				transfer.slot, transfer.name.c_str(), transfer.isLeft ? "left" : "right", reason);
			if (transfer.command) {
				Rock::CancelEquip(transfer.command);
			}
			releaseWeaponClone(transfer.outgoing);
			const UInt32 slot = transfer.slot;
			transfer = {};
			// The weapon stays holstered; restore a display hidden early.
			if (gShowWeapons && holsteredWeapNames[slot] != "Empty" &&
				WeaponRegisteredObjects.find(slot) == WeaponRegisteredObjects.end()) {
				QueueStoredVisual(FindStoredStack(holsteredWeapNames[slot]), slot);
			}
		}

		// While the outgoing weapon is still in the hand, its hidden clone
		// starts from wherever the hand holds it this frame.
		void TrackOutgoing(HandTransfer& transfer)
		{
			if (!transfer.outgoing || !transfer.outgoingHidden || !transfer.outgoing->WeapMesh) {
				return;
			}
			HandWeaponFacts facts{};
			facts.known = true;
			facts.holds = true;
			facts.nativeIndex = transfer.outgoingIndex;
			facts.formId = transfer.outgoingFormId;
			NiNode* model = nullptr;
			NiTransform world{};
			NiTransform start{};
			if (CaptureHeldWeapon(facts, model, world) &&
				LocalFromWorld(transfer.outgoing->wbone->m_worldTransform, world, start)) {
				transfer.outgoing->blendFrom = start;
				transfer.outgoing->WeapMesh->m_localTransform = start;
			}
		}

		void RevealOutgoing(HandTransfer& transfer)
		{
			if (!transfer.outgoing || !transfer.outgoingHidden || !transfer.outgoing->WeapMesh) {
				return;
			}
			transfer.outgoingHidden = false;
			transfer.outgoing->WeapMesh->flags &= ~0x1;
			transfer.outgoing->blendElapsed = 0.0F;
			transfer.outgoing->blendDuration = kHolsterBlendSeconds;
			transfer.outgoing->blending = true;
		}

		void CompleteTransfer(HandTransfer& transfer)
		{
			RevealOutgoing(transfer);
			const UInt32 slot = transfer.slot;
			const bool hadDisplay = WeaponRegisteredObjects.find(slot) != WeaponRegisteredObjects.end();
			nDestoryWeapon(slot, true, hadDisplay);
			if (transfer.outgoing || !transfer.outgoingName.empty()) {
				SetHolsterTag(slot, transfer.outgoingName, transfer.outgoingNameLength);
				if (transfer.outgoing && transfer.outgoing->WeapMesh) {
					transfer.outgoing->WeapMesh->m_name = HolsterNames[slot].c_str();
					WeaponRegisteredObjects[slot] = transfer.outgoing;
					VisualArt[slot] = transfer.outgoing->WeaponName.c_str();
					lastInHolster[slot] = transfer.outgoing->ThisForm;
					attachHolsterArt(*transfer.outgoing, slot);
					transfer.outgoing = nullptr;
				} else if (gShowWeapons) {
					releaseWeaponClone(transfer.outgoing);
					QueueStoredVisual(FindStoredStack(transfer.outgoingName), slot);
				}
			} else {
				resetHolsterName(slot);
			}
			PulseHand(transfer.isLeft);
			_MESSAGE("Holster %u drew '%s' into the %s hand", slot, transfer.name.c_str(), transfer.isLeft ? "left" : "right");
			transfer = {};
		}

		// The holster display hides on the frame the weapon is presented in
		// the hand; the tag clears only once the equip has succeeded.
		void HidePresentedDisplay(const HandTransfer& transfer)
		{
			const auto registered = WeaponRegisteredObjects.find(transfer.slot);
			if (registered != WeaponRegisteredObjects.end()) {
				nDestoryWeapon(transfer.slot, true, true);
			}
		}

		void ServiceTransfers()
		{
			const auto now = Clock::now();
			for (auto& transfer : s_transfers) {
				if (!transfer.active) {
					continue;
				}
				if (!transfer.requested) {
					if (now > transfer.deadline) {
						AbortTransfer(transfer, "the equip was never admitted");
					} else if (!RequestDraw(transfer)) {
						AbortTransfer(transfer, "the equip was refused");
					}
					continue;
				}
				if (Rock::IsBound()) {
					const auto progress = Rock::PollEquip(transfer.command);
					if (progress == Rock::EquipProgress::kPending) {
						TrackOutgoing(transfer);
					}
					switch (progress) {
					case Rock::EquipProgress::kPresented:
						HidePresentedDisplay(transfer);
						RevealOutgoing(transfer);
						break;
					case Rock::EquipProgress::kSucceeded:
						CompleteTransfer(transfer);
						continue;
					case Rock::EquipProgress::kFailed:
						transfer.command = 0;
						AbortTransfer(transfer, "ROCK reported the equip failed");
						continue;
					case Rock::EquipProgress::kPending:
						break;
					}
				} else {
					TrackOutgoing(transfer);
					const auto equipped = NativeEquipment::ReadEquippedWeapons()[0];
					if (equipped.formId == transfer.formId) {
						CompleteTransfer(transfer);
						continue;
					}
				}
				if (transfer.active && now > transfer.deadline) {
					AbortTransfer(transfer, "timed out");
				}
			}
		}

		void CancelTransfers()
		{
			for (auto& transfer : s_transfers) {
				if (transfer.active) {
					if (transfer.command) {
						Rock::CancelEquip(transfer.command);
					}
					releaseWeaponClone(transfer.outgoing);
					transfer = {};
				}
			}
		}

		void AdvanceBlend(MyWeapon& weapon)
		{
			if (!weapon.blending || !weapon.WeapMesh) {
				weapon.blending = false;
				return;
			}
			weapon.blendElapsed += s_frameSeconds;
			const float linear = weapon.blendDuration > 0.0F ?
				(std::min)(1.0F, weapon.blendElapsed / weapon.blendDuration) :
				1.0F;
			const float eased = linear * linear * (3.0F - 2.0F * linear);
			weapon.WeapMesh->m_localTransform = linear >= 1.0F ?
				weapon.blendTo :
				BlendTransforms(weapon.blendFrom, weapon.blendTo, eased);
			if (linear >= 1.0F) {
				weapon.blending = false;
			}
		}

		void UpdateWeaponBlends()
		{
			for (auto& [slot, weapon] : WeaponRegisteredObjects) {
				(void)slot;
				if (weapon) {
					AdvanceBlend(*weapon);
				}
			}
			for (auto& transfer : s_transfers) {
				if (transfer.outgoing) {
					AdvanceBlend(*transfer.outgoing);
				}
			}
		}

		void FinishWeaponBlends()
		{
			for (auto& [slot, weapon] : WeaponRegisteredObjects) {
				(void)slot;
				if (weapon && weapon->blending && weapon->WeapMesh) {
					weapon->WeapMesh->m_localTransform = weapon->blendTo;
					weapon->blending = false;
				}
			}
		}

		// ROCK must not grab, drop, stash or act on the holster button with a
		// hand working a holster, including a pull-out that has left the zone.
		void ReserveRockInput(const bool isLeft, const HandInteractionRuntime& hand, const bool actionPossible)
		{
			if (!Rock::IsBound()) {
				return;
			}
			const auto phase = hand.gesture.Phase();
			const bool heldFromZone = hand.pressFromZone &&
				(phase == Interaction::GesturePhase::kHeld || phase == Interaction::GesturePhase::kConsumed);
			if (!actionPossible && !heldFromZone) {
				return;
			}
			const auto mask = vr::ButtonMaskFromId(static_cast<vr::EVRButtonId>(c_holsterButtonID));
			Rock::ReserveHand(
				isLeft,
				c_holsterButtonID == vr::EVRButtonId::k_EButton_Grip,
				mask,
				hand.zone == 1 || hand.zone == 2);
		}
	}

	// Main Functions

	void resetInteractionState() {
		s_leftHand.gesture.Reset();
		s_rightHand.gesture.Reset();
		s_leftHand.pressFromZone = false;
		s_rightHand.pressFromZone = false;
		CancelTransfers();
		Rock::Reset();
		ClearHandContacts();
		SetActivationSuppressed(false);
	}

	bool isHolsterSlotEnabled(const UInt32 slot) noexcept {
		return SlotPolicy::IsEnabled(s_enabledHolsterSlots, slot);
	}

	UInt32 getNextEnabledHolster(const UInt32 current) noexcept {
		return SlotPolicy::NextEnabled(s_enabledHolsterSlots, current);
	}

	bool isHandInHolsterZone(const bool isLeft) {
		return GetHandRuntime(isLeft).zone != Interaction::kNoSlot;
	}

	UInt32 getHandHolster(const bool isLeft) {
		return GetHandRuntime(isLeft).zone;
	}

	NiPoint3 HolsterCenterWorld(const MyHolster& holster) {
		return holster.bone ?
			LocalPointToWorld(holster.bone->m_worldTransform, holster.offset) :
			NiPoint3(0.0F, 0.0F, 0.0F);
	}

	bool initBoneTree() {
		BSFlattenedBoneTree* rt = (BSFlattenedBoneTree*)(*g_player)->unkF0->rootNode->m_children.m_data[0]->GetAsNiNode();
		if (!rt) {
			return false;
		}
		boneTreeMap.clear();
		boneTreeVec.clear();
		_MESSAGE("BoneTree Init -> Num Transforms = %d", rt->numTransforms);
		if (rt->numTransforms <= 0) {
			return false;
		}
		for (auto i = 0; i < rt->numTransforms; i++) {
			//_MESSAGE("BoneTree Init -> Push %s into position %d", rt->transforms[i].name.c_str(), i);
			boneTreeMap.insert({ rt->transforms[i].name.c_str(), i });
			boneTreeVec.push_back(rt->transforms[i].name.c_str());
		}
		_MESSAGE("BoneTree Init Completed");
		return true;
	}

	void MainLoop() {
		if (*g_ui) {  // Fix for if player dies and then loads a save with a missing .esp (player is kicked to main menu and mod needs to start like fresh boot).
			BSFixedString menuName("MainMenu");
			if (((*g_ui)->IsMenuOpen(menuName)) && (!_fistRun)) {
				_fistRun = true;
				_MESSAGE("Main Menu Detected - RESETTING TO FIRST RUN");
			}
		}
		if (!(*g_player) ||
			!(*g_player)->unkF0 ||
			!(*g_player)->unkF0->rootNode ||
			!(*g_player)->firstPersonSkeleton) {
			resetInteractionState();
			return;
		}
		if ((*g_player)->unkF0 && (*g_player)->unkF0->rootNode && (*g_player)->firstPersonSkeleton != nullptr) {
			if (initBoneTreeFlag) {
				if (!initBoneTree()) {
					resetInteractionState();
					return;
				}
				initBoneTreeFlag = false;
				PlayerLoaded();
			}
			const auto now = Clock::now();
			s_frameSeconds = s_lastFrame == Clock::time_point{} ? 0.0F :
				(std::min)(0.1F, std::chrono::duration<float>(now - s_lastFrame).count());
			s_lastFrame = now;
			if (fistsPressed && now > s_fistsPressedUntil) {
				fistsPressed = false;
			}
			if (MenuCheck && now > s_menuCheckUntil) {
				MenuCheck = false;
			}
			if (infoPressed && now > s_infoPressedUntil) {
				infoPressed = false;
				doublepress = 0;
			}
			Rock::BeginFrame();
			ServiceStoredVisuals();
			if (!_isConfigModeActive && !isGameStopped()) {
				ServiceTransfers();
			}
			UpdateWeaponBlends();
			detectHolsterSphere();
			VRButtonsMain();
			closeMenus();
			handlePowerArmor();
			playerIsSneaking();
			if (_isConfigModeActive) {
				updateSneakUI();
				updatePAUI();
				updateUIInfo();
				updateUITiles();
			}
			if (!gPlayerisSneaking && gInPowerArmor) {
				holsterMode = 1;
			}
			else if (gPlayerisSneaking && !gInPowerArmor) {
				holsterMode = 2;
			}
			else if (gPlayerisSneaking && gInPowerArmor) {
				holsterMode = 3;
			}
			else {
				holsterMode = 0;
			}
			gInPowerArmor ? weaponMode = 1 : weaponMode = 0;
			checkArmor(); //checks for any changes to armor and repositions weapons if needed
			checkfists();
			if (isVRHMenuOpen() && !menuOpenSticky) {
				menuOpenSticky = true;
			}
			else if (!isVRHMenuOpen() && menuOpenSticky) {
				menuOpenSticky = false;
				ReconcileHolsterTags(BusySlots());
			}
			BSFixedString pipboyMenu("PipboyMenu");
			IMenu* menu = (*g_ui)->GetMenu(pipboyMenu);
			if (menu) {
				_isPipboyOpen = true;
			}
			else {
				_isPipboyOpen = false;
			}
			Rock::EndFrame();
		}
	}

	void checkArmor() {
		int ArmorIndex[3][3]{ {3, 6 ,11 }, {3, 9 ,14 }, { 3, 10 ,15 } };
		for (int i = 0; i < 3; i++) {
			for (int j = 0; j < 3; j++) {
				TESForm* equippedItem = (*g_player)->equipData->slots[ArmorIndex[i][j]].item;
				if (equippedItem) {
					CurrentEquippedArmor[i][j] = equippedItem->formID;
				}
				else {
					CurrentEquippedArmor[i][j] = 0;
				}
			}
			if ((CurrentEquippedArmor[i][0] != LastEquippedArmor[i][0]) || (CurrentEquippedArmor[i][1] != LastEquippedArmor[i][1]) || (CurrentEquippedArmor[i][2] != LastEquippedArmor[i][2])) {
				for (int j = 0; j < 3; j++) {
					LastEquippedArmor[i][j] = CurrentEquippedArmor[i][j];
				}
				if (i == 0) {
					int holNum[5] = { 1, 2, 5, 6, 7 };
					for (int i = 0; i < 5; i++) {
						moveWeapon(holNum[i]);
					}
				}
				else if (i == 1) {
					moveWeapon(3);
				}
				else {
					moveWeapon(4);
				}
			}
		}
	}

	void checkfists() {
		bool isWeaponDrawn = (*g_player)->actorState.IsWeaponDrawn();
		if (isWeaponDrawn && !equipsticky) {
			equipsticky = true;
			TESObjectWEAP* currentWeap = Offsets::GetEquippedWeapon((*g_gameVM)->m_virtualMachine, 0, *g_player, 0);
			if (!currentWeap && !fistssticky) {
				fistssticky = true;
				setFingerPositionScalar(true, 0.0, 0.0, 0.0, 0.0, 0.0);
			}
		}
		else if (!isWeaponDrawn && equipsticky) {
			equipsticky = false;
			if (fistssticky) {
				fistssticky = false;
				//_MESSAGE("Fists UnEquipped");
				restoreFingerPoseControl(true);
			}
		}
	}

	void handlePowerArmor() {
		if (detectInPowerArmor() && !gInPowerArmor) {
			std::string itemName;
			int slot[3] = { 11, 14, 15 };
			for (int i = 0; i < 3; i++) {
				int ii = slot[i];
				TESForm* equippedItem = (*g_player)->equipData->slots[ii].item;
				if (equippedItem) {
					itemName = equippedItem->GetFullName();
				}
				else {
					itemName = "None";
				}
			}
			gInPowerArmor = true;
			weaponsPAChange();
		}
		else if (!detectInPowerArmor() && gInPowerArmor) {
			gInPowerArmor = false;
			weaponsPAChange();
		}
	}

	bool playerIsSneaking() {
		bool isSneaking = IsSneaking(*g_player);
		if (isSneaking && !gPlayerisSneaking) {
			gPlayerisSneaking = true;
			sphereMoveSneak();
			return playerIsSneaking;
		}
		else if (!isSneaking && gPlayerisSneaking) {
			gPlayerisSneaking = false;
			sphereMoveStand();
			return playerIsSneaking;
		}
		else {
			return playerIsSneaking;
		}
	}

	void weaponsPAChange() {
		destroyAllHolsterSpheres();
		bool t = initBoneTree();
		for (int i = 1; i < 8; i++) {
			nDestoryWeapon(i, true, false);	
		}
		GameLoadInvSort();
		updateSpheresForPAEvent();
	}

	void VRButtonsMain() {
		if (VRHook::g_vrHook != nullptr) {
			(void)VRHook::g_vrHook->setVRControllerState();
			const auto leftController =
				VRHook::g_vrHook->getControllerState(
					VRHook::VRSystem::TrackerType::Left);
			const auto rightController =
				VRHook::g_vrHook->getControllerState(
					VRHook::VRSystem::TrackerType::Right);
			const bool leftControllerAvailable =
				VRHook::g_vrHook->isControllerStateAvailable(
					VRHook::VRSystem::TrackerType::Left);
			const bool rightControllerAvailable =
				VRHook::g_vrHook->isControllerStateAvailable(
					VRHook::VRSystem::TrackerType::Right);
			const uint64_t leftButtons =
				leftControllerAvailable ?
					leftController.ulButtonPressed :
					0;
			const uint64_t rightButtons =
				rightControllerAvailable ?
					rightController.ulButtonPressed :
					0;
			const uint64_t dominantHand =
				gIsRightHanded ? rightButtons : leftButtons;
			const uint64_t offHand =
				gIsRightHanded ? leftButtons : rightButtons;
			const vr::VRControllerAxis_t doinantHandStick =
				gIsRightHanded ?
					rightController.rAxis[0] :
					leftController.rAxis[0];
			const vr::VRControllerAxis_t offHandStick =
				gIsRightHanded ?
					leftController.rAxis[0] :
					rightController.rAxis[0];

			const auto holsterButtonMask = vr::ButtonMaskFromId(
				static_cast<vr::EVRButtonId>(c_holsterButtonID));
			const bool leftHolsterHeld =
				(leftButtons & holsterButtonMask) != 0;
			const bool rightHolsterHeld =
				(rightButtons & holsterButtonMask) != 0;
			const bool gameplayEnabled =
				!_isConfigModeActive &&
				!isGameStopped() &&
				!_WeaponHolsterInProgress;
			const HandWeaponFacts leftFacts = GetHandWeaponFacts(true);
			const HandWeaponFacts rightFacts = GetHandWeaponFacts(false);
			const auto pullOutPossible = [](const HandInteractionRuntime& hand, const bool isLeft, const HandWeaponFacts& facts) {
				return DecideAction(Interaction::GestureActionKind::kPullOut, hand.zone, isLeft, facts) != HolsterAction::kNone;
			};
			const auto releasePossible = [](const HandInteractionRuntime& hand, const bool isLeft, const HandWeaponFacts& facts) {
				return DecideAction(Interaction::GestureActionKind::kReleaseInZone, hand.zone, isLeft, facts) != HolsterAction::kNone;
			};
			const bool leftCanArmPullOut =
				gameplayEnabled &&
				leftControllerAvailable &&
				s_leftHand.anchorAvailable &&
				leftHolsterHeld &&
				s_leftHand.gesture.Phase() ==
					Interaction::GesturePhase::kIdle &&
				pullOutPossible(s_leftHand, true, leftFacts);
			const bool rightCanArmPullOut =
				gameplayEnabled &&
				rightControllerAvailable &&
				s_rightHand.anchorAvailable &&
				rightHolsterHeld &&
				s_rightHand.gesture.Phase() ==
					Interaction::GesturePhase::kIdle &&
				pullOutPossible(s_rightHand, false, rightFacts);
			for (auto* hand : { &s_leftHand, &s_rightHand }) {
				const bool held = hand == &s_leftHand ? leftHolsterHeld : rightHolsterHeld;
				if (!held) {
					hand->pressFromZone = false;
				} else if (hand->gesture.Phase() == Interaction::GesturePhase::kIdle) {
					hand->pressFromZone = hand->zone != Interaction::kNoSlot;
				}
			}
			const auto leftDecision = s_leftHand.gesture.Update({
				leftControllerAvailable && s_leftHand.anchorAvailable,
				gameplayEnabled,
				leftHolsterHeld,
				s_leftHand.zone,
				leftCanArmPullOut
			});
			const auto rightDecision = s_rightHand.gesture.Update({
				rightControllerAvailable && s_rightHand.anchorAvailable,
				gameplayEnabled,
				rightHolsterHeld,
				s_rightHand.zone,
				rightCanArmPullOut
			});

			SetActivationSuppressed(
				c_ActivationDisabled &&
				gameplayEnabled &&
				(s_leftHand.gesture.IsHoldingCandidate() ||
					s_rightHand.gesture.IsHoldingCandidate()));
			if (gameplayEnabled) {
				ReserveRockInput(true, s_leftHand,
					s_leftHand.zone != Interaction::kNoSlot &&
						(releasePossible(s_leftHand, true, leftFacts) || pullOutPossible(s_leftHand, true, leftFacts)));
				ReserveRockInput(false, s_rightHand,
					s_rightHand.zone != Interaction::kNoSlot &&
						(releasePossible(s_rightHand, false, rightFacts) || pullOutPossible(s_rightHand, false, rightFacts)));
			}

			// Gameplay Controls 
			if (!_isConfigModeActive) {
				const auto ConfigButtonPressed = dominantHand & vr::ButtonMaskFromId((vr::EVRButtonId)c_configButtonID);
				const auto EquipButtonPressed = dominantHand & vr::ButtonMaskFromId((vr::EVRButtonId)c_equipButtonID);
				if (!EquipButtonPressed && _isEquipButtonPressed && !_isPipboyOpen) {
					_isEquipButtonPressed = false;
					if ((*g_player)->actorState.IsWeaponDrawn()) {
						TESObjectWEAP* currentWeap = Offsets::GetEquippedWeapon((*g_gameVM)->m_virtualMachine, 0, *g_player, 0);
						if (!currentWeap) {
							if (fistsPressed) {
								Offsets::DrawWeaponMagicHands(*g_player, false);
								fistsPressed = false;
							}
						}
					}
				}
				else if (EquipButtonPressed && !_isEquipButtonPressed && !_isPipboyOpen) {
					_isEquipButtonPressed = true;
					s_fistsPressedUntil = Clock::now() + std::chrono::milliseconds(500);
					fistsPressed = true;
				}

				// Each hand acts on its own holster in the same frame; a
				// hand-directed draw leaves the other hand's weapon in place.
				const std::pair<const Interaction::GestureDecision*, const HandWeaponFacts*> handInputs[2]{
					{ &leftDecision, &leftFacts }, { &rightDecision, &rightFacts }
				};
				for (int index = 0; index < 2 && gameplayEnabled; ++index) {
					const bool isLeft = index == 0;
					const auto& decision = *handInputs[index].first;
					if (!decision.HasAction()) {
						continue;
					}
					// The other hand may have changed what this hand can reach
					// this frame (a holster or swap), so read its weapon again.
					const HandWeaponFacts facts = index == 1 ? GetHandWeaponFacts(false) : *handInputs[index].second;
					const UInt32 slot = decision.ActionSlot();
					const HolsterAction action = DecideAction(decision.Kind(), slot, isLeft, facts);
					bool acted = false;
					switch (action) {
					case HolsterAction::kHolster:
						acted = HolsterHandWeapon(isLeft, slot, facts);
						break;
					case HolsterAction::kDraw:
						acted = BeginDraw(isLeft, slot, facts, false);
						break;
					case HolsterAction::kSwap:
						acted = BeginDraw(isLeft, slot, facts, true);
						break;
					case HolsterAction::kNone:
						break;
					}
					if (acted) {
						gCurHolster = slot;
						if (c_ActivationDisabled) {
							MenuCheck = true;
							s_menuCheckUntil = Clock::now() + std::chrono::milliseconds(1000);
						}
					}
				}

				const UInt32 dominantHolster =
					getHandHolster(!gIsRightHanded);
				if (ConfigButtonPressed &&
					!_isConfigButtonPressed &&
					dominantHolster != Interaction::kNoSlot &&
					!(*g_player)->actorState.IsWeaponDrawn()) {
					_isConfigButtonPressed = true;
					BSFixedString pipboyMenu("PipboyMenu");
					IMenu* menu = (*g_ui)->GetMenu(pipboyMenu);
					if (!menu) {
						gCurHolster = dominantHolster;
						FinishWeaponBlends();
						setupConfigMode();
						_isConfigModeActive = true;
					}
				}
				else if (!ConfigButtonPressed) {
					_isConfigButtonPressed = false;
				}

				if (ConfigButtonPressed && !_isInfoButtonPressed && _isPipboyOpen) {
					_isInfoButtonPressed = true;
					s_infoPressedUntil = Clock::now() + std::chrono::milliseconds(350);
					infoPressed = true;
					if (doublepress < 2) {
						doublepress++;
					}
				}
				else if (!ConfigButtonPressed && _isInfoButtonPressed) {
					_isInfoButtonPressed = false;
					if (infoPressed && doublepress == 2) {
						infoPressed = false;
						doublepress = 0;
						std::string mes = ("<br>Left Shoulder: " + holsteredWeapNames[1] + "</br>" + "<br>Right Shoulder: " + holsteredWeapNames[2] + "</br>" + "<br>Left Hip: " + holsteredWeapNames[3] + "</br>" + "<br>Right Hip: " + holsteredWeapNames[4] + "</br>" + "<br>Lower Back: " + holsteredWeapNames[5] + "</br>" + "<br>Left Chest: " + holsteredWeapNames[6] + "</br>"+ "<br>Right Chest: " + holsteredWeapNames[7] + "</br>");
						ShowMessagebox(mes.c_str());
						BSFixedString menuName("FavoritesMenu");
						if ((*g_ui)->IsMenuOpen(menuName)) {
							if ((*g_ui)->IsMenuRegistered(menuName)) {
								CALL_MEMBER_FN(*g_uiMessageManager, SendUIMessage)(menuName, kMessage_Close);
							}
						}
					}
					
				}






			}
			// ConfigMode Controls
			if (_isConfigModeActive) {
				const auto ConfigExitLButtonPressed = offHand & vr::ButtonMaskFromId((vr::EVRButtonId)c_configExitButtonID);
				const auto ConfigExitRButtonPressed = dominantHand & vr::ButtonMaskFromId((vr::EVRButtonId)c_configExitButtonID);
				const auto ConfigShiftLButtonPressed = offHand & vr::ButtonMaskFromId((vr::EVRButtonId)c_configModeShiftButton);
				const auto ConfigShiftRButtonPressed = dominantHand & vr::ButtonMaskFromId((vr::EVRButtonId)c_configModeRotateButton);
				//const auto ConfigSaveButtonPressed = offHand & vr::ButtonMaskFromId((vr::EVRButtonId)c_configModeSaveButton);
				//const auto ChangeConfigModeButtonPressed = dominantHand & vr::ButtonMaskFromId((vr::EVRButtonId)c_configChangeModeButton);
				const auto ChangeHolsterButtonPressed = dominantHand & vr::ButtonMaskFromId((vr::EVRButtonId)c_configChangeHolButton);
				const auto SelfieButtonPressed = dominantHand & vr::ButtonMaskFromId((vr::EVRButtonId)c_selfieModeButtonID);
				const auto CamButtonPressed = offHand & vr::ButtonMaskFromId((vr::EVRButtonId)c_movecamButtonID);
				const auto ArtWorkButtonPressed = offHand & vr::ButtonMaskFromId((vr::EVRButtonId)c_ArtWorkButtonID);
				if (SelfieButtonPressed && !_isSelfieModeButtonButtonPressed && _isConfigModeRShiftButtonPressed) {
					_isSelfieModeButtonButtonPressed = true;
					if (!_selfieModeActive) {
						_selfieModeActive = true;
					}
					else {
						_selfieModeActive = false;
					}
					SInt32 evt = HolsterEvent_Selfie;
					if (g_HolsterEventRegs.m_data.size() > 0) {
						g_HolsterEventRegs.ForEach(
							[&evt](const EventRegistration<NullParameters>& reg) {
								SendPapyrusEvent2<SInt32, UInt32>(reg.handle, reg.scriptName, HolsterEventName, evt, gCurHolster);
							}
						);
					}
				}
				else if (!SelfieButtonPressed) {
					_isSelfieModeButtonButtonPressed = false;
				}
				if (CamButtonPressed && !_isMovecamButtonPressed) {
					_isMovecamButtonPressed = true;
					if (!ConfigShiftLButtonPressed) {
						std::string a = std::to_string(_isTouchEnabled);
						_isTouchEnabled ? _isTouchEnabled = false : _isTouchEnabled = true;

					}
					else {
						if (!_camMovedBack) {
							_camMovedBack = true;
							SInt32 evt;
							if (gInPowerArmor) {
								evt = HolsterEvent_CamBack;
								dist = camDistancePA / 10;
							}
							else {
								evt = HolsterEvent_CamForward;
								dist = camDistance / 10;
							}
							if (g_HolsterEventRegs.m_data.size() > 0) {
								g_HolsterEventRegs.ForEach(
									[&evt](const EventRegistration<NullParameters>& reg) {
										for (int i = 0; i < dist; i++) {
											SendPapyrusEvent2<SInt32, UInt32>(reg.handle, reg.scriptName, HolsterEventName, evt, gCurHolster);
										}
									}
								);
							}
						}
						else {
							_isMovecamButtonPressed = true;
							_camMovedBack = false;
							SInt32 evt;
							if (gInPowerArmor) {
								evt = HolsterEvent_CamForward;
								dist = camDistancePA / 10;
							}
							else {
								evt = HolsterEvent_CamBack;
								dist = camDistance / 10;
							}
							if (g_HolsterEventRegs.m_data.size() > 0) {
								g_HolsterEventRegs.ForEach(
									[&evt](const EventRegistration<NullParameters>& reg) {
										for (int i = 0; i < dist; i++) {
											SendPapyrusEvent2<SInt32, UInt32>(reg.handle, reg.scriptName, HolsterEventName, evt, gCurHolster);
										}
									}
								);
							}
						}
					}
				}
				else if (!CamButtonPressed) {
					_isMovecamButtonPressed = false;
				}
				if (ArtWorkButtonPressed && !_isArtWorkButtonPressed && !_isConfigModeLShiftButtonPressed) {
					_isArtWorkButtonPressed = true;
					pressArtWorkChange(gCurHolster);
				}
				else if (!ArtWorkButtonPressed) {
					_isArtWorkButtonPressed = false;
				}
				if (ArtWorkButtonPressed && !_isArtWorkButtonPressed && _isConfigModeLShiftButtonPressed) {
					_isArtWorkButtonPressed = true;
					PressChangeAllArtWork();
				}
				if (ConfigShiftLButtonPressed && !_isConfigModeLShiftButtonPressed) {
					_isConfigModeLShiftButtonPressed = true;
				}
				else if (!ConfigShiftLButtonPressed) {
					_isConfigModeLShiftButtonPressed = false;
				}
				if (ConfigShiftRButtonPressed && !_isConfigModeRShiftButtonPressed) {
					_isConfigModeRShiftButtonPressed = true;
				}
				else if (!ConfigShiftRButtonPressed) {
					_isConfigModeRShiftButtonPressed = false;
				}
				//if (ChangeConfigModeButtonPressed && !_isChangeConfigModeButtonPressed && !_isConfigModeRShiftButtonPressed) {
					//_isChangeConfigModeButtonPressed = true;
				//}
				//else if (!ChangeConfigModeButtonPressed) {
					//_isChangeConfigModeButtonPressed = false;
				//}
				if (ChangeHolsterButtonPressed && !_isChangeHolsterButtonPressed && !_isConfigModeRShiftButtonPressed) {
					_isChangeHolsterButtonPressed = true;
					switchHolster();
				}
				else if (!ChangeHolsterButtonPressed) {
					_isChangeHolsterButtonPressed = false;
				}
				//if (ConfigSaveButtonPressed && !_isConfigModeSaveButtonPressed && !_isConfigModeLShiftButtonPressed) {
					//_isConfigModeSaveButtonPressed = true;
					//saveconfig();
				//}
				//else if (!ConfigSaveButtonPressed) {
					//_isConfigModeSaveButtonPressed = false;
				//}
				if (doinantHandStick.y > 0.50 || doinantHandStick.y < -0.50) {
					if (!_isConfigModeLShiftButtonPressed) {
						rAxisOffsetY = doinantHandStick.y / 10;
						if (!_isConfigModeRShiftButtonPressed) {
							moveholsterY();
						}
						else {
							if (c_mode == 2) {
								rotateholsterY();
							}
						}
					}
				}
				if (doinantHandStick.x > 0.50 || doinantHandStick.x < -0.50) {
					if (!_isConfigModeLShiftButtonPressed) {
						rAxisOffsetX = doinantHandStick.x / 10;
						if (!_isConfigModeRShiftButtonPressed) {
							moveholsterX();
						}
						else {
							if (c_mode == 2) {
								rotateholsterX();
							}
						}
					}
				}
				if (offHandStick.x > 0.50 || offHandStick.x < -0.50) {
					if (!_isConfigModeLShiftButtonPressed) {
						lAxisOffsetX = offHandStick.x / 10;
						if (!_isConfigModeRShiftButtonPressed) {
							moveholsterZ();
						}
						else {
							if (c_mode == 2) {
								rotateholsterZ();
							}
						}
					}
				}
				if (offHandStick.y > 0.50 || offHandStick.y < -0.50) {
					if (_isConfigModeLShiftButtonPressed) {
						lAxisOffsetY = offHandStick.y / 10;
						scaleHolster();
					}
				}
				if (ConfigExitLButtonPressed || ConfigExitRButtonPressed) {
					exitConfigMode();
					_isConfigModeActive = false;
				}
			}
		}
		else {
			resetInteractionState();
			VRHook::InitVRSystem();
		}
	}

	void mainStartup() {
		_MESSAGE("Starting up Virtual Holsters");
		resetInteractionState();
		nextHolsterHandle = 1;
		nextWeaponHandle = 1;
		if (VRHook::g_vrHook != nullptr) {
			(void)VRHook::g_vrHook->setVRControllerState();
		}
		else {
			VRHook::InitVRSystem();
			if (VRHook::g_vrHook != nullptr) {
				(void)VRHook::g_vrHook->setVRControllerState();
			}
		}
		loadConfigFiles();
		_fistRun = true;
	}

	void saveGameLoaded() {
		resetInteractionState();
		initBoneTreeFlag = true;
	}

	void PlayerLoaded() {
		_MESSAGE("Game Event:: Game Loaded");
		resetInteractionState();
		createAllHolsterSpheres();
		_MESSAGE("---- Holster Spheres Recreated");
		if (!_fistRun) {
			_MESSAGE("---- Destorying All Weapons....");
			for (int i = 1; i < 8; i++) {
				nDestoryWeapon(i, true, false);
			}
			_MESSAGE("---- All Weapons Destoryed");
			saveloaded = true;
		}
		else {
			_fistRun = false;
			saveloaded = true;
		}
		_MESSAGE("---- Holster Container Links:: Setup Start");
		UInt32 HolsterID[8] = { 0x001022, 0x001022, 0x001021, 0x001020, 0x00101F, 0x001023, 0x001024, 0x006B86 };
		//UInt32 HolsterID[8] = { 0x01C1FC, 0x01C1FC, 0x01C1FD, 0x01C1F8, 0x01C1FB, 0x01C1F9, 0x01C1FA, 0x01C1FE };
		for (int i = 1; i < 8; i++) {
			TESObjectREFR* ref = DYNAMIC_CAST(GetFormFromFile(HolsterID[i], "VirtualHolsters.esp"), TESForm, TESObjectREFR);
			if (ref) {
				std::string mes = "-------- Holster0" + std::to_string(i) + " Linked to Container0" + std::to_string(i);
				HolsterContainers[i] = ref;
				_MESSAGE(mes.c_str());
			}
			else {
				std::string mes = "-------- Error: Unable to find Container0" + std::to_string(i);
				_MESSAGE(mes.c_str());
			}
		}
		_MESSAGE("---- Holster Container Links:: Setup Complete");
		_MESSAGE("---- Holster Config:: Update");
		//for (int i = 1; i < 8; i++) {
			GameLoadInvSort();
		//}
		_MESSAGE("---- Holster Config:: Update Completed");
		_MESSAGE("Game Event:: Game Load Completed");
	}

	bool loadConfigFiles() {
        const auto iniPath = config::path("VirtualHolsters.ini");
        if (iniPath.empty()) {
            _WARNING("Documents could not be resolved; VirtualHolsters configuration is unavailable");
            return false;
        }
        std::error_code pathError;
        const bool missing = !std::filesystem::exists(iniPath, pathError);
        if (pathError) return false;
        FirstRunIni ini(missing);
        SI_Error rc = missing ? SI_OK : ini.LoadFile(iniPath.c_str());

		if (rc < 0) {
			_MESSAGE("ERROR: cannot read VirtualHolsters.ini");
			return false;
		}
		c_holsterButtonID = (int)ini.GetLongValue("Button Config", "HolsterButtonID", vr::EVRButtonId::k_EButton_A);
		c_configButtonID = (int)ini.GetLongValue("Button Config", "ConfigModeButtonID", vr::EVRButtonId::k_EButton_SteamVR_Touchpad);
		c_configModeShiftButton = (int)ini.GetLongValue("Button Config", "ConfigModeShiftButtonID", vr::EVRButtonId::k_EButton_Grip);
		c_configModeRotateButton = (int)ini.GetLongValue("Button Config", "ConfigRotateButtonID", vr::EVRButtonId::k_EButton_A);
		//c_configModeSaveButton = (int)ini.GetLongValue("Button Config", "SaveProfileButtonID", vr::EVRButtonId::k_EButton_A);
		//c_configChangeModeButton = (int)ini.GetLongValue("Button Config", "ModeSwitchButtonID", vr::EVRButtonId::k_EButton_ApplicationMenu);
		c_configChangeHolButton = (int)ini.GetLongValue("Button Config", "CycleHolstersButtonID", vr::EVRButtonId::k_EButton_Grip);
		c_selfieModeButtonID = (int)ini.GetLongValue("Button Config", "ToggleSelfieModeButtonID", vr::EVRButtonId::k_EButton_SteamVR_Touchpad);
		gshowSpheres = ini.GetBoolValue("Main Options", "DisplaySpheres", true);
		c_ActivationDisabled = ini.GetBoolValue("Main Options", "bDisableActivation", false);
		c_VATSDisabled = ini.GetBoolValue("Main Options", "bDisableVATS", false);
		ghapticsEnabled = ini.GetBoolValue("Main Options", "EnableHaptics", true);
		c_hapticDuration = static_cast<float>(
			ini.GetDoubleValue(
				"Main Options",
				"hapticDuration",
				0.05));
		c_hapticIntensity = static_cast<float>(
			ini.GetDoubleValue(
				"Main Options",
				"hapticIntensity",
				0.5));
		gShowWeapons = ini.GetBoolValue("Main Options", "DisplayWeapons", true);
		gIsRightHanded = ini.GetBoolValue("Main Options", "isRightHanded", true);
		testclonefix = ini.GetBoolValue("Main Options", "bTestCloneFix", false);
		s_enabledHolsterSlots = SlotPolicy::kNoSlotsMask;
		for (UInt32 slot = SlotPolicy::kFirstSlot;
			 slot <= SlotPolicy::kLastSlot;
			 ++slot) {
			s_enabledHolsterSlots = SlotPolicy::SetEnabled(
				s_enabledHolsterSlots,
				slot,
				ini.GetBoolValue(
					"Holster Slots",
					SlotPolicy::kConfigKeys[slot],
					true));
		}
		_MESSAGE(
			"Holster slot enable mask: 0x%02X",
			static_cast<unsigned int>(s_enabledHolsterSlots));
		camDistance = (int)ini.GetLongValue("Config Mode", "CameraMoveBackDist", 120);
		camDistancePA = (int)ini.GetLongValue("Config Mode", "CameraMoveBackDistPA", 240);
		RXOffset = (float)ini.GetDoubleValue("Config Mode", "RotateXMultiplier", 0.0);
		RYOffset = (float)ini.GetDoubleValue("Config Mode", "RotateYMultiplier", 0.0);
		RZOffset = (float)ini.GetDoubleValue("Config Mode", "RotateZMultiplier", 0.0);
		//grabDistance = (double*)ini.GetDoubleValue("Main Options", "fPrimaryWandMaxGrabDistance", 96.0);
		isInMasterMode = ini.GetBoolValue("Config Mode", "MasterMode", false);
		holsterArtWorkPresets[0] = 0;
		holsterArtWorkPresets[1] = (int)ini.GetLongValue("Config Mode", "Holster01CustomArtSlot", 0);
		holsterArtWorkPresets[2] = (int)ini.GetLongValue("Config Mode", "Holster02CustomArtSlot", 0);
		holsterArtWorkPresets[3] = (int)ini.GetLongValue("Config Mode", "Holster03CustomArtSlot", 0);
		holsterArtWorkPresets[4] = (int)ini.GetLongValue("Config Mode", "Holster04CustomArtSlot", 0);
		holsterArtWorkPresets[5] = (int)ini.GetLongValue("Config Mode", "Holster05CustomArtSlot", 0);
		holsterArtWorkPresets[6] = (int)ini.GetLongValue("Config Mode", "Holster06CustomArtSlot", 0);
		holsterArtWorkPresets[7] = (int)ini.GetLongValue("Config Mode", "Holster07CustomArtSlot", 0);
		PAholsterArtWorkPresets[0] = 0;
		PAholsterArtWorkPresets[1] = (int)ini.GetLongValue("Config Mode", "PAHolster01CustomArtSlot", 0);
		PAholsterArtWorkPresets[2] = (int)ini.GetLongValue("Config Mode", "PAHolster02CustomArtSlot", 0);
		PAholsterArtWorkPresets[3] = (int)ini.GetLongValue("Config Mode", "PAHolster03CustomArtSlot", 0);
		PAholsterArtWorkPresets[4] = (int)ini.GetLongValue("Config Mode", "PAHolster04CustomArtSlot", 0);
		PAholsterArtWorkPresets[5] = (int)ini.GetLongValue("Config Mode", "PAHolster05CustomArtSlot", 0);
		PAholsterArtWorkPresets[6] = (int)ini.GetLongValue("Config Mode", "PAHolster06CustomArtSlot", 0);
		PAholsterArtWorkPresets[7] = (int)ini.GetLongValue("Config Mode", "PAHolster07CustomArtSlot", 0);
        if (missing && !saveNewDefaults(ini, iniPath)) {
            _WARNING("Cannot create VirtualHolsters configuration");
            return false;
        }
		readOffsetJson();
		std::string fileName[8] = { "","LeftShoulder.ini", "RightShoulder.ini", "LeftHip.ini", "RightHip.ini", "LowerBack.ini", "LeftChest.ini", "RightChest.ini", };
		std::string fileName2[8] = { "","LeftShoulderPA.ini", "RightShoulderPA.ini", "LeftHiPA.ini", "RightHipPA.ini", "LowerBackPA.ini", "LeftChestPA.ini", "RightChestPA.ini", };
		for (int i = 1; i < 8; i++) {
			const std::string path = { config::path("VRH_WeaponBlackList") + "\\" + fileName[i] };
			std::string str;
			std::fstream file;
			file.open(path, std::ios::in);
			while (getline(file, str))
			{
				c_WeaponBlacklist[i].push_back(str);
			}
		}
		for (int i = 1; i < 8; i++) {
			const std::string path = { config::path("VRH_WeaponBlackList") + "\\" + fileName2[i] };
			//_MESSAGE(path.c_str());
			std::string str;
			std::fstream file;
			file.open(path, std::ios::in);
			while (getline(file, str))
			{
				c_WeaponBlacklistPA[i].push_back(str);
			}

		}
		const std::string path = { config::path("VRH_MagazineBlackList/MagazineBlackList.ini") };
		//_MESSAGE(path.c_str());
		std::string str;
		std::fstream file;
		file.open(path, std::ios::in);
		while (getline(file, str))
		{
			c_ExcludedMagazines.push_back(str);
		}
		const std::string path2 = { config::path("VRH_weapon_offsets/SwordDrawSFXList.ini") };
		//_MESSAGE(path.c_str());
		std::string str2;
		std::fstream file2;
		file2.open(path2, std::ios::in);
		while (getline(file2, str2))
		{
			c_SwordDrawSFX.push_back(str2);
		}
		return true;
	}

	// Config Mode Functions

	void ActivationToggle() {
		CSimpleIniA ini;
		SI_Error rc = ini.LoadFile(config::path("VirtualHolsters.ini").c_str());
		rc = ini.SetBoolValue("Main Options", "bDisableActivation", c_ActivationDisabled);
		rc = ini.SaveFile(config::path("VirtualHolsters.ini").c_str());
	}

	void ChangeVATS() {
		if (c_VATSDisabled) {
			SInt32 evt2 = HolsterEvent_EnableVATS;
			if (g_HolsterEventRegs.m_data.size() > 0) {
				g_HolsterEventRegs.ForEach(
					[&evt2](const EventRegistration<NullParameters>& reg) {
						SendPapyrusEvent2<SInt32, UInt32>(reg.handle, reg.scriptName, HolsterEventName, evt2, gCurHolster);
					}
				);
			}
			c_VATSDisabled = false;
			if (c_holsterButtonID == 1) {
				c_holsterButtonID = 7;
			}
		}
		else {
			SInt32 evt2 = HolsterEvent_DisableVATS;
			if (g_HolsterEventRegs.m_data.size() > 0) {
				g_HolsterEventRegs.ForEach(
					[&evt2](const EventRegistration<NullParameters>& reg) {
						SendPapyrusEvent2<SInt32, UInt32>(reg.handle, reg.scriptName, HolsterEventName, evt2, gCurHolster);
					}
				);
			}
			c_VATSDisabled = true;
		}
		CSimpleIniA ini;
		SI_Error rc = ini.LoadFile(config::path("VirtualHolsters.ini").c_str());
		rc = ini.SetBoolValue("Main Options", "bDisableVATS", c_VATSDisabled);
		rc = ini.SaveFile(config::path("VirtualHolsters.ini").c_str());
	}

	void swapHolsterButton() {
		CSimpleIniA ini;
		SI_Error rc = ini.LoadFile(config::path("VirtualHolsters.ini").c_str());
		rc = ini.SetLongValue("Button Config", "HolsterButtonID", c_holsterButtonID);
		rc = ini.SaveFile(config::path("VirtualHolsters.ini").c_str());
	}

	void configmodeupdate(int mode) {
		c_mode = mode;
	}

	void closeSelfieMode() {
		if (_selfieModeActive) {
			SInt32 evt = HolsterEvent_Selfie;
			if (g_HolsterEventRegs.m_data.size() > 0) {
				g_HolsterEventRegs.ForEach(
					[&evt](const EventRegistration<NullParameters>& reg) {
						SendPapyrusEvent2<SInt32, UInt32>(reg.handle, reg.scriptName, HolsterEventName, evt, gCurHolster);
					}
				);
			}
			_selfieModeActive = false;
		}
	}

	void retorecam() {
		if (_camMovedBack) {
			SInt32 evt;
			if (gInPowerArmor) {
				evt = HolsterEvent_CamForward;
				dist = camDistancePA / 10;
			}
			else {
				evt = HolsterEvent_CamBack;
				dist = camDistance / 10;
			}
			if (g_HolsterEventRegs.m_data.size() > 0) {
				g_HolsterEventRegs.ForEach(
					[&evt](const EventRegistration<NullParameters>& reg) {
						for (int i = 0; i < dist; i++) {
							SendPapyrusEvent2<SInt32, UInt32>(reg.handle, reg.scriptName, HolsterEventName, evt, gCurHolster);
						}
					}
				);
			}
			_camMovedBack = false;
		}
	}

	void toggleHaptics() {
		ghapticsEnabled ? ghapticsEnabled = false : ghapticsEnabled = true;
		CSimpleIniA ini;
		SI_Error rc = ini.LoadFile(config::path("VirtualHolsters.ini").c_str());
		rc = ini.SetBoolValue("Main Options", "EnableHaptics", ghapticsEnabled);
		rc = ini.SaveFile(config::path("VirtualHolsters.ini").c_str());
	}

	void toggleWeapVis() {
		if (gShowWeapons) {
			//destoryAllWeapons();
			for (int i = 1; i < 8; i++) {
				nDestoryWeapon(i, true, false);
			}
			gShowWeapons = false;
		}
		else {
			gShowWeapons = true;
			//for (int i = 1; i < 8; i++) {
			GameLoadInvSort();
			//}
			/*SInt32 evt = HolsterEvent_Recreate;
			if (g_HolsterEventRegs.m_data.size() > 0) {
				g_HolsterEventRegs.ForEach(
					[&evt](const EventRegistration<NullParameters>& reg) {
						SendPapyrusEvent2<SInt32, UInt32>(reg.handle, reg.scriptName, HolsterEventName, evt, gCurHolster);
					}
				);
			}*/


		}
		CSimpleIniA ini;
		SI_Error rc = ini.LoadFile(config::path("VirtualHolsters.ini").c_str());
		rc = ini.SetBoolValue("Main Options", "DisplayWeapons", gShowWeapons);
		rc = ini.SaveFile(config::path("VirtualHolsters.ini").c_str());
	}

	void toggleSphereVis() {
		gshowSpheres ? gshowSpheres = false : gshowSpheres = true;
		CSimpleIniA ini;
		SI_Error rc = ini.LoadFile(config::path("VirtualHolsters.ini").c_str());
		rc = ini.SetBoolValue("Main Options", "DisplaySpheres", gshowSpheres);
		rc = ini.SaveFile(config::path("VirtualHolsters.ini").c_str());
	}

	void toggleSwitchRGA() {
		if (c_configModeRotateButton == vr::EVRButtonId::k_EButton_A) {
			c_configModeRotateButton = vr::EVRButtonId::k_EButton_Grip;
		}
		else if (c_configModeRotateButton == vr::EVRButtonId::k_EButton_Grip) {
			c_configModeRotateButton = vr::EVRButtonId::k_EButton_A;
		}
		else {
			_MESSAGE("IRREGULAR BUTTON CONFIG NO CHANGES MADE");
		}
		CSimpleIniA ini;
		SI_Error rc = ini.LoadFile(config::path("VirtualHolsters.ini").c_str());
		rc = ini.SetLongValue("Button Config", "ConfigRotateButtonID", c_configModeRotateButton);
		rc = ini.SaveFile(config::path("VirtualHolsters.ini").c_str());
	}

	void toggleSwitchLGA() {
		if (c_configModeShiftButton == vr::EVRButtonId::k_EButton_Grip) {
			c_configModeShiftButton = vr::EVRButtonId::k_EButton_A;

		}
		else if (c_configModeShiftButton == vr::EVRButtonId::k_EButton_A) {
			c_configModeShiftButton = vr::EVRButtonId::k_EButton_Grip;
		}
		else {
			_MESSAGE("IRREGULAR BUTTON CONFIG NO CHANGES MADE");
		}
		CSimpleIniA ini;
		SI_Error rc = ini.LoadFile(config::path("VirtualHolsters.ini").c_str());
		rc = ini.SetLongValue("Button Config", "ConfigModeShiftButtonID", c_configModeShiftButton);
		rc = ini.SaveFile(config::path("VirtualHolsters.ini").c_str());
	}

	void toggleSouthPaw() {
		gIsRightHanded ? gIsRightHanded = false : gIsRightHanded = true;
		CSimpleIniA ini;
		SI_Error rc = ini.LoadFile(config::path("VirtualHolsters.ini").c_str());
		rc = ini.SetBoolValue("Main Options", "isRightHanded", gIsRightHanded);
		rc = ini.SaveFile(config::path("VirtualHolsters.ini").c_str());
	}

	void PressChangeAllArtWork() {
		//_MESSAGE("UI: CHANGE ALL ARTWORK");
		int x = gInPowerArmor ? PAholsterArtWorkPresets[gCurHolster] : holsterArtWorkPresets[gCurHolster];
		int mx = gInPowerArmor ? PAholsterMaxValue[gCurHolster] : holsterMaxValue[gCurHolster];
		std::string y = (std::to_string(gCurHolster) + " " + std::to_string(x));
		//_MESSAGE(y.c_str());
		if (x == 0) {
			x = mx;
		}
		else {
			x = x - 1;
		}
		for (int i = 1; i < 8; i++) {
			if (static_cast<UInt32>(i) != gCurHolster &&
				isHolsterSlotEnabled(i)) {
				gInPowerArmor ? PAholsterArtWorkPresets[i] = x : holsterArtWorkPresets[i] = x;
				std::string p = std::to_string(i) + " " + std::to_string(x);
				//_MESSAGE(p.c_str());
				artSwitched = false;
				pressArtWorkChange(i);
				if (artSwitched == false) {
					gInPowerArmor ? PAholsterArtWorkPresets[i] = x + 1 : holsterArtWorkPresets[i] = x + 1;
					CSimpleIniA ini;
					SI_Error rc = ini.LoadFile(config::path("VirtualHolsters.ini").c_str());
					std::string slot;
					gInPowerArmor ? slot = "PAHolster0" + std::to_string(i) + "CustomArtSlot" : slot = "Holster0" + std::to_string(i) + "CustomArtSlot";
					//_MESSAGE(slot.c_str());
					gInPowerArmor ? rc = ini.SetLongValue("Config Mode", slot.c_str(), PAholsterArtWorkPresets[i]) : rc = ini.SetLongValue("Config Mode", slot.c_str(), holsterArtWorkPresets[i]);
					rc = ini.SaveFile(config::path("VirtualHolsters.ini").c_str());
				}
			}
		}
	}

	void pressArtWorkChange(int holster) {
		std::string artName = "HolsterArt" + std::to_string(gCurHolster);
		NiNode* artToChange = getChildNode(artName.c_str(), (*g_player)->unkF0->rootNode);
		if (artToChange) {
			//_MESSAGE("UI: CHANGING ART");
			NiNode* bone = nullptr;
			gInPowerArmor ? bone = FindNode(PAWeaponBones[holster].c_str()) : bone = FindNode(WeaponBones[holster].c_str());
			std::vector<std::string> listOfFiles;
			std::string currentWeapon = VisualArt[holster];
			std::string searchpath;
			//_MESSAGE("UI: SEARCHING FOR ART");
			gInPowerArmor ? searchpath = "Data\\Meshes\\VRH\\Holsters\\_PA\\" + currentWeapon + "\\HolsterArt" + std::to_string(holster) + "*.nif" : searchpath = "Data\\Meshes\\VRH\\Holsters\\" + currentWeapon + "\\HolsterArt" + std::to_string(holster) + "*.nif";
			listOfFiles = getFileNames(searchpath);
			if (listOfFiles.size() == 0) {
				//_MESSAGE("UI: NO CUSTOM ART FOUND FOR THIS SLOT / WEAPON");
				return;
			}
			//_MESSAGE("UI: CUSTOM ART FOUND, SWITCHING TO NEXT ART");
			gInPowerArmor ? PAholsterArtWorkPresets[holster] += 1 : holsterArtWorkPresets[holster] += 1;
			std::string finalpath;
			int x = gInPowerArmor ? PAholsterArtWorkPresets[holster] : holsterArtWorkPresets[holster];
			int xx = listOfFiles.size();
			gInPowerArmor ? PAholsterMaxValue[holster] = (xx - 1) : holsterMaxValue[holster] = (xx - 1);
			std::string yy;
			yy = "UI: NUMBER OF ART FOUND " + std::to_string(xx);
			//_MESSAGE(yy.c_str());
			std::string yyy = "UI: CURRENT SLOT NUMBER " + std::to_string(x);
			//_MESSAGE(yyy.c_str());
			if (x > xx) {
				return;
			}
			if (x == xx) {
				gInPowerArmor ? PAholsterArtWorkPresets[holster] = 0 : holsterArtWorkPresets[holster] = 0;
			}
			x = gInPowerArmor ? PAholsterArtWorkPresets[holster] : holsterArtWorkPresets[holster];
			std::string zzz = "UI: CURRENT SLOT NUMBER2 " + std::to_string(x);
			gInPowerArmor ? finalpath = "Data/Meshes/VRH/Holsters/_PA/" + currentWeapon + "/" + listOfFiles[x] : finalpath = "Data/Meshes/VRH/Holsters/" + currentWeapon + "/" + listOfFiles[x];
			//_MESSAGE(finalpath.c_str());
			if (!exists_file(finalpath)) {
				return;
			}
			char* hP;
			hP = new char[finalpath.size() + 1];
			std::copy(finalpath.begin(), finalpath.end(), hP);
			hP[finalpath.size()] = '\0';
			//gInPowerArmor ? _MESSAGE("UI MODE: APPLYING PA HOLSTER ART FOUND AT") : _MESSAGE("UI : APPLYING HOLSTER ART FOUND AT");
			//_MESSAGE(finalpath.c_str());
			NiNode* retNode = loadNifFromFile(hP);
			NiCloneProcess proc;
			proc.unk18 = Offsets::cloneAddr1;
			proc.unk48 = Offsets::cloneAddr2;
			NiNode* holArt = Offsets::cloneNode(retNode, &proc);
			holArt->m_name = artName.c_str();
			VRSceneGraph::AttachChild(bone, holArt, true);
			holArt->m_localTransform = artToChange->m_localTransform;
			artToChange->m_localTransform.scale = 0;
			VRSceneGraph::RemoveChild(
				artToChange->m_parent,
				artToChange);
			//_MESSAGE("UI: HOLSTER ART SUCCESSFULLY CHANGED");
			CSimpleIniA ini;
			SI_Error rc = ini.LoadFile(config::path("VirtualHolsters.ini").c_str());
			std::string slot;
			gInPowerArmor ? slot = "PAHolster0" + std::to_string(holster) + "CustomArtSlot" : slot = "Holster0" + std::to_string(holster) + "CustomArtSlot";
			//_MESSAGE(slot.c_str());
			gInPowerArmor ? rc = ini.SetLongValue("Config Mode", slot.c_str(), PAholsterArtWorkPresets[holster]) : rc = ini.SetLongValue("Config Mode", slot.c_str(), holsterArtWorkPresets[holster]);
			rc = ini.SaveFile(config::path("VirtualHolsters.ini").c_str());
			artSwitched = true;
		}
	}

	void closeMenus() {
		BSFixedString menuName("FavoritesMenu");
		if (((*g_ui)->IsMenuOpen(menuName)) && (isHolsterIntersected)) {
			if ((*g_ui)->IsMenuRegistered(menuName)) {
				CALL_MEMBER_FN(*g_uiMessageManager, SendUIMessage)(menuName, kMessage_Close);
			}
		}
		BSFixedString menuName2("ScopeMenu");
		if ((*g_ui)->IsMenuOpen(menuName2)) {
			if ((*g_ui)->IsMenuRegistered(menuName2)) {
				//NiNode* sphere = FindNode("BlackSphere");
				//if (sphere) {
					//auto& pHashSet = (*g_ui)->menuTable;
					//auto* pItem = pHashSet.Find(&menuName2);
					//RemoveImagespaceModifier(g_Data.PipboyMenuImod);
					//sphere->m_localTransform.scale = 0.0;
					//pItem->menuInstance->flags &= ~IMenu::kFlag_BlurBackground;
					//pItem->menuInstance->flags &= ~IMenu::kFlag_ShaderdWorld;
				//}
			}
		}
		if (MenuCheck) {
			const char* Menus[5] = { "PowerArmorModMenu", "CookingMenu", "ExamineMenu", "ContainerMenu", "TerminalMenu" };
			for (int i = 0; i < 5; i++) {
				BSFixedString M = Menus[i];
				if ((*g_ui)->IsMenuOpen(M)) {
					if ((*g_ui)->IsMenuRegistered(M)) {
						CALL_MEMBER_FN(*g_uiMessageManager, SendUIMessage)(M, kMessage_Close);
						MenuCheck = false;
						i = 6;
						return;
					}
				}
			}
		}
	}

	void scaleHolster() {
		if (configMode == 1) {
			if (gCurHolster == 1 || gCurHolster == 2) {
				return;
			}
			if (HolsterRegisteredObjects[gCurHolster]) {
				NiNode* hol = HolsterRegisteredObjects[gCurHolster]->debugSphere;
				float nScale(lAxisOffsetY);
				float origScale = hol->m_localTransform.scale;
				if ((origScale + nScale) < 1) {
					//_MESSAGE("SCALE RETURN");
					return;
				}
				hol->m_localTransform.scale = (origScale + nScale);
				float radiusA = hol->m_localTransform.scale;
				radiusA = radiusA / 2;
				HolsterRegisteredObjects[gCurHolster]->radius = radiusA;
			}
		}
	}

	void moveholsterX() {
		NiNode* hol = nullptr;
		bool isMelee = WeaponRegisteredObjects[gCurHolster]->isMelee;
		if (configMode == 1) {
			if (gCurHolster == 1 || gCurHolster == 2) {
				return;
			}
			if (HolsterRegisteredObjects[gCurHolster]) {
				hol = HolsterRegisteredObjects[gCurHolster]->debugSphere;
			}
			else {
				return;
			}
			isMelee = false;
		}
		else {
			if (WeaponRegisteredObjects[gCurHolster]) {
				hol = WeaponRegisteredObjects[gCurHolster]->WeapMesh;
			}
			else {
				return;
			}
		}
		if (hol) {
			float nLoc;
			float origLoc;
			if (gCurHolster == 1) {
				bool isInPA = detectInPowerArmor();
				isInPA ? nLoc = rAxisOffsetX : nLoc = rAxisOffsetX * -1;
				isInPA ? origLoc = hol->m_localTransform.pos.z : origLoc = hol->m_localTransform.pos.x;
				if ((origLoc + nLoc) > 50 || (origLoc + nLoc) < -50) {
					return;
				}
				isInPA ? hol->m_localTransform.pos.z = (origLoc + nLoc) : hol->m_localTransform.pos.x = (origLoc + nLoc);
			}
			else if (gCurHolster == 2) {
				bool isInPA = detectInPowerArmor();
				isInPA ? nLoc = rAxisOffsetX : nLoc = rAxisOffsetX;
				isInPA ? origLoc = hol->m_localTransform.pos.z : origLoc = hol->m_localTransform.pos.x;
				if ((origLoc + nLoc) > 50 || (origLoc + nLoc) < -50) {
					return;
				}
				isInPA ? hol->m_localTransform.pos.z = (origLoc + nLoc) : hol->m_localTransform.pos.x = (origLoc + nLoc);
			}

			else if (gCurHolster == 3) {
				nLoc = rAxisOffsetX;
				isMelee ? origLoc = hol->m_localTransform.pos.z : origLoc = hol->m_localTransform.pos.z;
				if ((origLoc + nLoc) > 50 || (origLoc + nLoc) < -50) {
					return;
				}
				isMelee ? hol->m_localTransform.pos.z = (origLoc + nLoc) : hol->m_localTransform.pos.z = (origLoc + nLoc);
			}
			else if (gCurHolster == 4) {
				nLoc = rAxisOffsetX;
				//origLoc = hol->m_localTransform.pos.z;
				isMelee ? origLoc = hol->m_localTransform.pos.z : origLoc = hol->m_localTransform.pos.z;
				if ((origLoc + nLoc) > 50 || (origLoc + nLoc) < -50) {
					return;
				}
				//hol->m_localTransform.pos.z = (origLoc + nLoc);
				isMelee ? hol->m_localTransform.pos.z = (origLoc + nLoc) : hol->m_localTransform.pos.z = (origLoc + nLoc);
			}

			else if (gCurHolster == 5) {
				nLoc = rAxisOffsetX * -1;
				origLoc = hol->m_localTransform.pos.z;
				if ((origLoc + nLoc) > 50 || (origLoc + nLoc) < -50) {
					return;
				}
				hol->m_localTransform.pos.z = (origLoc + nLoc);
			}
			else if (gCurHolster == 6) {
				nLoc = rAxisOffsetX * -1;
				origLoc = hol->m_localTransform.pos.z;
				if ((origLoc + nLoc) > 50 || (origLoc + nLoc) < -50) {
					return;
				}
				hol->m_localTransform.pos.z = (origLoc + nLoc);
			}
			else if (gCurHolster == 7) {
				nLoc = rAxisOffsetX * -1;
				origLoc = hol->m_localTransform.pos.z;
				if ((origLoc + nLoc) > 50 || (origLoc + nLoc) < -50) {
					return;
				}
				hol->m_localTransform.pos.z = (origLoc + nLoc);
			}
			if (configMode == 2) {
				std::string artName = "HolsterArt" + std::to_string(gCurHolster);
				NiNode* artToMove = getChildNode(artName.c_str(), (*g_player)->unkF0->rootNode); //Remove any Holster Art applied this slot
				if (artToMove) {
					artToMove->m_localTransform.pos = hol->m_localTransform.pos;
				}
			}
		}
		if (configMode == 1) {
			HolsterRegisteredObjects[gCurHolster]->offset = hol->m_localTransform.pos;
		}
	}

	void moveholsterY() {
		NiNode* hol = nullptr;
		bool isMelee;
		if (configMode == 1) {
			if (gCurHolster == 1 || gCurHolster == 2) {
				return;
			}
			if (HolsterRegisteredObjects[gCurHolster]) {
				hol = HolsterRegisteredObjects[gCurHolster]->debugSphere;
			}
			else {
				return;
			}
			isMelee = false;
		}
		else {
			if (WeaponRegisteredObjects[gCurHolster]) {
				hol = WeaponRegisteredObjects[gCurHolster]->WeapMesh;
				isMelee = WeaponRegisteredObjects[gCurHolster]->isMelee;
			}
			else {
				return;
			}
		}

		float nLoc(rAxisOffsetY);

		if (hol) {
			float origLoc;
			if (gCurHolster > 4 || gCurHolster < 4) {
				isMelee ? origLoc = hol->m_localTransform.pos.y : origLoc = hol->m_localTransform.pos.y;
			}
			else {
				origLoc = hol->m_localTransform.pos.y;
			}
			//origLoc = hol->m_localTransform.pos.x
			//origLoc = hol->m_localTransform.pos;
			if ((origLoc + nLoc) > 50 || (origLoc + nLoc) < -50) {
				return;
			}
			isMelee ? hol->m_localTransform.pos.y = (origLoc + nLoc) : hol->m_localTransform.pos.y = (origLoc + nLoc);
			//hol->m_localTransform.pos = (origLoc + nLoc);
			if (configMode == 2) {
				std::string artName = "HolsterArt" + std::to_string(gCurHolster);
				
				NiNode* artToMove = getChildNode(artName.c_str(), (*g_player)->unkF0->rootNode);
				if (artToMove) {
					artToMove->m_localTransform.pos = hol->m_localTransform.pos;
				}
			}
		}
		if (configMode == 1) {
			HolsterRegisteredObjects[gCurHolster]->offset = hol->m_localTransform.pos;
		}
	}

	void moveholsterZ() {
		NiNode* hol = nullptr;
		if (configMode == 1) {
			if (gCurHolster == 1 || gCurHolster == 2) {
				return;
			}
			if (HolsterRegisteredObjects[gCurHolster]) {
				hol = HolsterRegisteredObjects[gCurHolster]->debugSphere;
			}
			else {
				return;
			}
		}
		else {
			if (WeaponRegisteredObjects[gCurHolster]) {
				hol = WeaponRegisteredObjects[gCurHolster]->WeapMesh;
			}
			else {
				return;
			}
		}
		float nLoc;
		float origLoc;
		if (gCurHolster == 1) {
			bool isInPA = detectInPowerArmor();
			isInPA ? nLoc = lAxisOffsetX : nLoc = lAxisOffsetX;
			isInPA ? origLoc = hol->m_localTransform.pos.x : origLoc = hol->m_localTransform.pos.z;
			if ((origLoc + nLoc) > 50 || (origLoc + nLoc) < -50) {
				return;
			}
			isInPA ? hol->m_localTransform.pos.x = (origLoc + nLoc) : hol->m_localTransform.pos.z = (origLoc + nLoc);
		}
		else if (gCurHolster == 2) {
			bool isInPA = detectInPowerArmor();
			isInPA ? nLoc = lAxisOffsetX : nLoc = lAxisOffsetX * -1;
			isInPA ? origLoc = hol->m_localTransform.pos.x : origLoc = hol->m_localTransform.pos.z;
			if ((origLoc + nLoc) > 50 || (origLoc + nLoc) < -50) {
				return;
			}
			isInPA ? hol->m_localTransform.pos.x = (origLoc + nLoc) : hol->m_localTransform.pos.z = (origLoc + nLoc);
		}
		else if (gCurHolster == 3) {
			nLoc = lAxisOffsetX;
			origLoc = hol->m_localTransform.pos.x;
			if ((origLoc + nLoc) > 50 || (origLoc + nLoc) < -50) {
				return;
			}
			hol->m_localTransform.pos.x = (origLoc + nLoc);
		}
		else if (gCurHolster == 4) {
			nLoc = lAxisOffsetX;
			origLoc = hol->m_localTransform.pos.x;
			if ((origLoc + nLoc) > 50 || (origLoc + nLoc) < -50) {
				return;
			}
			hol->m_localTransform.pos.x = (origLoc + nLoc);
		}
		else if (gCurHolster == 5) {
			nLoc = lAxisOffsetX * -1;
			origLoc = hol->m_localTransform.pos.x;
			if ((origLoc + nLoc) > 50 || (origLoc + nLoc) < -50) {
				return;
			}
			hol->m_localTransform.pos.x = (origLoc + nLoc);
		}
		else if (gCurHolster == 6) {
			nLoc = lAxisOffsetX * -1;
			origLoc = hol->m_localTransform.pos.x;
			if ((origLoc + nLoc) > 50 || (origLoc + nLoc) < -50) {
				return;
			}
			hol->m_localTransform.pos.x = (origLoc + nLoc);
		}
		else if (gCurHolster == 7) {
			nLoc = lAxisOffsetX * -1;
			origLoc = hol->m_localTransform.pos.x;
			if ((origLoc + nLoc) > 50 || (origLoc + nLoc) < -50) {
				return;
			}
			hol->m_localTransform.pos.x = (origLoc + nLoc);
		}
		if (configMode == 2) {
			std::string artName = "HolsterArt" + std::to_string(gCurHolster);
			NiNode* artToMove = getChildNode(artName.c_str(), (*g_player)->unkF0->rootNode);
			if (artToMove) {
				artToMove->m_localTransform.pos = hol->m_localTransform.pos;
			}
		}
		if (configMode == 1) {
			HolsterRegisteredObjects[gCurHolster]->offset = hol->m_localTransform.pos;
		}
	}

	void rotateholsterX() {
		NiNode* hol = nullptr;
		Matrix44 rot;
		bool MeleeWeapon = (WeaponRegisteredObjects[gCurHolster]->isMelee);
		float tAxisOffset;
		//_MESSAGE(std::to_string(RXOffset).c_str());
		if (RXOffset != 0.0) {
			tAxisOffset = rAxisOffsetX * RXOffset;
		}
		else {
			tAxisOffset = rAxisOffsetX;
		}
		MeleeWeapon ? rot.setEulerAngles((degrees_to_rads(tAxisOffset)), 0, 0) : rot.setEulerAngles(0, 0, (degrees_to_rads(tAxisOffset)));
		if (configMode == 2) {
			if (WeaponRegisteredObjects[gCurHolster]) {
				hol = WeaponRegisteredObjects[gCurHolster]->WeapMesh;
				if (hol) {
					hol->m_localTransform.rot = rot.multiply43Left(hol->m_localTransform.rot);
					rot.multiply43Left(hol->m_localTransform.rot);
					std::string artName = "HolsterArt" + std::to_string(gCurHolster);
					
					NiNode* artToMove = getChildNode(artName.c_str(), (*g_player)->unkF0->rootNode);
					if (artToMove) {
						artToMove->m_localTransform.rot = hol->m_localTransform.rot;
					}
				}
			}
			else {
				return;
			}
		}
	}

	void rotateholsterY() {
		Matrix44 rot;
		NiNode* hol = nullptr;
		bool MeleeWeapon = (WeaponRegisteredObjects[gCurHolster]->isMelee);
		float tAxisOffset;
		//_MESSAGE(std::to_string(RYOffset).c_str());
		if (RYOffset != 0.0) {
			tAxisOffset = rAxisOffsetY * RYOffset;
		}
		else {
			tAxisOffset = rAxisOffsetY;
		}
		MeleeWeapon ? rot.setEulerAngles(0, 0, (degrees_to_rads(tAxisOffset))) : rot.setEulerAngles((degrees_to_rads(tAxisOffset)), 0, 0);
		if (configMode == 2) {
			if (WeaponRegisteredObjects[gCurHolster]) {
				hol = WeaponRegisteredObjects[gCurHolster]->WeapMesh;
				if (hol) {
					hol->m_localTransform.rot = rot.multiply43Left(hol->m_localTransform.rot);
					rot.multiply43Left(hol->m_localTransform.rot);
					std::string artName = "HolsterArt" + std::to_string(gCurHolster);
					NiNode* artToMove = getChildNode(artName.c_str(), (*g_player)->unkF0->rootNode);
					if (artToMove) {
						artToMove->m_localTransform.rot = hol->m_localTransform.rot;
					}

				}
			}
			else {
				return;
			}
		}
	}

	void rotateholsterZ() {
		NiNode* hol = nullptr;
		Matrix44 rot;
		float tAxisOffset;
		//_MESSAGE(std::to_string(RZOffset).c_str());
		if (RZOffset != 0.0) {
			tAxisOffset = lAxisOffsetX * RZOffset;
		}
		else {
			tAxisOffset = lAxisOffsetX;
		}
		rot.setEulerAngles(0, (degrees_to_rads(tAxisOffset)), 0);
		if (configMode == 2) {
			if (WeaponRegisteredObjects[gCurHolster]) {
				hol = WeaponRegisteredObjects[gCurHolster]->WeapMesh;
				if (hol) {
					hol->m_localTransform.rot = rot.multiply43Left(hol->m_localTransform.rot);
					rot.multiply43Left(hol->m_localTransform.rot);
					std::string artName = "HolsterArt" + std::to_string(gCurHolster);
					NiNode* artToMove = getChildNode(artName.c_str(), (*g_player)->unkF0->rootNode);
					if (artToMove) {
						artToMove->m_localTransform.rot = hol->m_localTransform.rot;
					}
				}
			}
			else {
				return;
			}
		}
	}

	void saveconfig() {
		BSFixedString storedName;
		BSFixedString itemName;
		std::string name;
		NiNode* mesh;
		NiNode* mesh2;
		if (c_mode == 1) {
			itemName = HolsterRegisteredObjects[gCurHolster]->MeshName;
			storedName = HolsterRegisteredObjects[gCurHolster]->MeshName;
			mesh = HolsterRegisteredObjects[gCurHolster]->debugSphere;
			name = (storedName);
		}


		else {
			if (WeaponRegisteredObjects[gCurHolster]) {
				itemName = WeaponRegisteredObjects[gCurHolster]->HolsterName;
				storedName = WeaponRegisteredObjects[gCurHolster]->WeaponName;
				mesh2 = WeaponRegisteredObjects[gCurHolster]->WeapMesh;
				if (!isInMasterMode) {
					UInt32 WeapForm = WeaponRegisteredObjects[gCurHolster]->ThisForm;
					name = FileNameCreation(gCurHolster, WeapForm);
				}
				else {
					std::string ArmorName;
					if (gInPowerArmor) {
						if (gCurHolster > 4 || gCurHolster < 3) {
							TESForm* equippedItem = (*g_player)->equipData->slots[11].item;
							if (equippedItem) {
								ArmorName = equippedItem->GetFullName();
							}
						}
						else if (gCurHolster == 3) {
							TESForm* equippedItem = (*g_player)->equipData->slots[14].item;
							if (equippedItem) {
								ArmorName = equippedItem->GetFullName();
							}
						}
						else {
							TESForm* equippedItem = (*g_player)->equipData->slots[15].item;
							if (equippedItem) {
								ArmorName = equippedItem->GetFullName();
							}
						}
					}
					if (!WeaponRegisteredObjects[gCurHolster]->isMelee) {
						if (gInPowerArmor) {
							name = "Weapon" + std::to_string(gCurHolster) + ArmorName + "-PowerArmor";
						}
						else {
							name = "Weapon" + std::to_string(gCurHolster) + "-Normal";
						}
					}
					else {
						if (gInPowerArmor) {
							name = "Melee" + std::to_string(gCurHolster) + ArmorName + "-PowerArmor";
						}
						else {
							name = "Melee" + std::to_string(gCurHolster) + "-Normal";
						}
					}
				}
			}
			else {
				return;
			}
		}



		NiNode* hol = FindNode(itemName);
		if (!hol) {
			return;
		}
		std::string wNaa = "ConfigMODE: ";
		wNaa += (name);
		//_MESSAGE(wNaa.c_str());
		std::string wN = "ConfigMODE: ";
		wN += std::to_string(c_mode);
		//_MESSAGE(wN.c_str());
		NiTransform _customTransform1;
		_customTransform1.rot = hol->m_localTransform.rot;
		_customTransform1.pos = hol->m_localTransform.pos;
		if (c_mode == 1) {
			_customTransform1.scale = (hol->m_localTransform.scale / 2);
		}
		else {
			_customTransform1.scale = (hol->m_localTransform.scale);
		}

		if (c_mode == 1) {
			if (holsterMode == 1) {
				g_weaponOffsets->addOffset(name, _customTransform1, powerArmor);
			}
			else if (holsterMode == 2) {
				g_weaponOffsets->addOffset(name, _customTransform1, noPASneak);
			}
			else if (holsterMode == 3) {
				g_weaponOffsets->addOffset(name, _customTransform1, sneakinpowerArmor);
			}
			else {
				g_weaponOffsets->addOffset(name, _customTransform1, noPA);
			}
		}
		else {
			/*if (weaponMode == 1) {
				g_weaponOffsets->addOffset(name, _customTransform1, powerArmor);
			}
			else {
				g_weaponOffsets->addOffset(name, _customTransform1, noPA);
			}*/
			g_weaponOffsets->addOffset(name, _customTransform1, normal);
		}
		if (configMode == 1) {
			HolsterRegisteredObjects[gCurHolster]->radius = _customTransform1.scale;
		}
		CSimpleIniA ini;
		SI_Error rc = ini.LoadFile(config::path("VirtualHolsters.ini").c_str());
		for (int i = 1; i < 8; i++) {
			std::string val;
			gInPowerArmor ? val = ("PAHolster0" + std::to_string(i) + "CustomArtSlot") : val = ("Holster0" + std::to_string(i) + "CustomArtSlot");
			gInPowerArmor ? rc = ini.SetLongValue("Config Mode", val.c_str(), PAholsterArtWorkPresets[i]) : rc = ini.SetLongValue("Config Mode", val.c_str(), holsterArtWorkPresets[i]);
		}
		rc = ini.SaveFile(config::path("VirtualHolsters.ini").c_str());
	}

	void restoreAllWeapons() {
		for (int i = 1; i < 8; i++) {
			nDestoryWeapon(i, true, false);
			resetHolsterName(i);
		}
	}

	// Holster Sphere Functions

	void detectHolsterSphere() {
		if (!(*g_player) ||
			!(*g_player)->firstPersonSkeleton ||
			!(*g_player)->unkF0 ||
			!(*g_player)->unkF0->rootNode ||
			!(*g_player)->unkF0->rootNode->m_children.m_data ||
			(*g_player)->unkF0->rootNode->m_children.m_emptyRunStart == 0) {
			ClearHandContacts();
			return;
		}

		auto* firstChild =
			(*g_player)->unkF0->rootNode->m_children.m_data[0];
		auto* treeNode = firstChild ? firstChild->GetAsNiNode() : nullptr;
		auto* boneTree =
			reinterpret_cast<BSFlattenedBoneTree*>(treeNode);
		if (!boneTree) {
			ClearHandContacts();
			return;
		}

		NiPoint3 leftPosition;
		NiPoint3 rightPosition;
		const bool leftAvailable =
			TryGetHandPosition(boneTree, true, leftPosition);
		const bool rightAvailable =
			TryGetHandPosition(boneTree, false, rightPosition);

		std::array<Interaction::ZoneSample, kMaximumHolsterSlots>
			leftSamples{};
		std::array<Interaction::ZoneSample, kMaximumHolsterSlots>
			rightSamples{};
		std::size_t leftSampleCount = 0;
		std::size_t rightSampleCount = 0;
		for (const auto& [slot, holster] : HolsterRegisteredObjects) {
			if (!isHolsterSlotEnabled(slot) ||
				!holster ||
				!holster->bone ||
				holster->radius <= 0.0F) {
				continue;
			}

			// The same placement the engine gives the sphere attached at this
			// bone-local offset, so detection matches what is drawn.
			const NiPoint3 center =
				LocalPointToWorld(holster->bone->m_worldTransform, holster->offset);
			if (leftAvailable && leftSampleCount < leftSamples.size()) {
				leftSamples[leftSampleCount++] = {
					slot,
					vec3_len(leftPosition - center),
					holster->radius
				};
			}
			if (rightAvailable && rightSampleCount < rightSamples.size()) {
				rightSamples[rightSampleCount++] = {
					slot,
					vec3_len(rightPosition - center),
					holster->radius
				};
			}
		}

		const UInt32 previousLeft = s_leftHand.zone;
		const UInt32 previousRight = s_rightHand.zone;
		s_leftHand.anchorAvailable = leftAvailable;
		s_rightHand.anchorAvailable = rightAvailable;
		s_leftHand.zone = leftAvailable ?
			Interaction::SelectStableZone(
				previousLeft,
				leftSamples.data(),
				leftSampleCount) :
			Interaction::kNoSlot;
		s_rightHand.zone = rightAvailable ?
			Interaction::SelectStableZone(
				previousRight,
				rightSamples.data(),
				rightSampleCount) :
			Interaction::kNoSlot;

		const bool leftEntered =
			s_leftHand.zone != Interaction::kNoSlot &&
			s_leftHand.zone != previousLeft;
		const bool rightEntered =
			s_rightHand.zone != Interaction::kNoSlot &&
			s_rightHand.zone != previousRight;

		if (!_isConfigModeActive) {
			// Update the non-preferred hand first so a simultaneous entry
			// leaves the configured hand as the legacy current holster.
			if (gIsRightHanded) {
				if (leftEntered) {
					gCurHolster = s_leftHand.zone;
				}
				if (rightEntered) {
					gCurHolster = s_rightHand.zone;
				}
			} else {
				if (rightEntered) {
					gCurHolster = s_rightHand.zone;
				}
				if (leftEntered) {
					gCurHolster = s_leftHand.zone;
				}
			}
		}

		if (ghapticsEnabled && VRHook::g_vrHook) {
			if (leftEntered) {
				VRHook::g_vrHook->StartHaptics(
					1,
					c_hapticDuration,
					c_hapticIntensity);
			}
			if (rightEntered) {
				VRHook::g_vrHook->StartHaptics(
					2,
					c_hapticDuration,
					c_hapticIntensity);
			}
		}

		isHolsterIntersected =
			s_leftHand.zone != Interaction::kNoSlot ||
			s_rightHand.zone != Interaction::kNoSlot;
		UpdateHolsterSpherePresentation();
	}

	void displayHolstersSphere(UInt32 handle) {
		if (!isHolsterSlotEnabled(handle)) {
			return;
		}
		if (HolsterRegisteredObjects[handle]) {
			NiNode* bone = HolsterRegisteredObjects[handle]->bone;
			NiNode* hol;
			BSFixedString NameofMesh = HolsterRegisteredObjects[handle]->MeshName;
			NiNode* retNode = loadNifFromFile("Data/Meshes/VRH/1x1Sphere.nif");
			NiCloneProcess proc;
			proc.unk18 = Offsets::cloneAddr1;
			proc.unk48 = Offsets::cloneAddr2;
			hol = Offsets::cloneNode(retNode, &proc);
			if (!hol) {
				_MESSAGE("NIF NOT LOADED");
			}
			if (hol) {
				hol->m_name = BSFixedString(NameofMesh);
				if (hol->m_children.m_emptyRunStart > 0) {
					for (auto i = 0; i < hol->m_children.m_emptyRunStart; ++i) {
						auto nextNode = hol->m_children.m_data[i];
						if (nextNode) {
							BSTriShape* NodeTri = nextNode->GetAsBSTriShape();
							if (NodeTri) {
								std::string n = "OrbMesh" + handle + i;
								nextNode->m_name = n.c_str();
								//_MESSAGE("RENAMED ORB");
							}
						}
					}
				}
				NiNode* Weap2 = getChildNode(NameofMesh, (*g_player)->unkF0->rootNode); //Check for accidental spawned clones & delete
				if (Weap2) {
					Weap2->flags |= 0x1;
					Weap2->m_localTransform.scale = 0;
					VRSceneGraph::RemoveChild(Weap2->m_parent, Weap2);
				}
				VRSceneGraph::AttachChild(bone, hol, true);
				hol->m_localTransform.scale = (HolsterRegisteredObjects[handle]->radius * 2);
				HolsterRegisteredObjects[handle]->debugSphere = hol;
				hol->m_localTransform.pos.x = (hol->m_localTransform.pos.x + HolsterRegisteredObjects[handle]->offset.x);
				hol->m_localTransform.pos.y = (hol->m_localTransform.pos.y + HolsterRegisteredObjects[handle]->offset.y);
				hol->m_localTransform.pos.z = (hol->m_localTransform.pos.z + HolsterRegisteredObjects[handle]->offset.z);
				hol->flags |= 0x1;
				hol->m_localTransform.scale = 0;
				//_MESSAGE("SPHERE ATTACHED");
			}
		}

	}

	void registerHolsterSphere(float radius, BSFixedString bone, NiPoint3 pos, BSFixedString MeshPath, BSFixedString MeshName, UInt32 handle) {
		if (!isHolsterSlotEnabled(handle) || radius == 0.0) {
			return;
		}
		if (!(*g_player)->unkF0) {
			_MESSAGE("can't register yet as new game");
			return;
		}
		NiNode* boneNode = FindNode(bone);
		if (!boneNode) {
			return;
		}
		NiPoint3 offsetVec;
		MyHolster* hol = new MyHolster(radius, boneNode, pos, MeshPath, MeshName);
		HolsterRegisteredObjects[handle] = hol;
		//_MESSAGE("HOLSTER SPHERE EVENT: SPHERE REGISTERED");
		displayHolstersSphere(handle);
		return;
	}

	void createAllHolsterSpheres() {
		for (int i = 1; i < 8; i++) {
			if (!isHolsterSlotEnabled(i)) {
				continue;
			}
			std::string c1 = HolsterBones[i];
			std::string hN = "Holster" + std::to_string(i);
			NiTransform _customTransform;
			auto lookup = g_weaponOffsets->getOffset(hN, noPA);
			if (holsterMode == 1) {
				lookup = g_weaponOffsets->getOffset(hN, powerArmor);
			}
			else if (holsterMode == 2) {
				lookup = g_weaponOffsets->getOffset(hN, noPASneak);
			}
			else if (holsterMode == 3) {
				lookup = g_weaponOffsets->getOffset(hN, sneakinpowerArmor);
			}
			else {
				lookup = g_weaponOffsets->getOffset(hN, noPA);
			}
			if (lookup.has_value()) {
				_customTransform = lookup.value();
			}
			else {
				lookup = g_weaponOffsets->getOffset(hN, noPA);
				if (lookup.has_value()) {
					_customTransform = lookup.value();
				}
				else {
					_MESSAGE("UNABLE TO FIND HOLSTER JSON");
					return;
				}
			}
			BSFixedString bone = c1.c_str();
			BSFixedString MeshPath = hN.c_str();
			BSFixedString MeshName = hN.c_str();
			NiPoint3 pos = _customTransform.pos;
			float radius = _customTransform.scale;
			NiNode* boneNode = FindNode(bone);
			if (!boneNode) {
				return;
			}
			registerHolsterSphere(radius, bone, pos, MeshPath, MeshName, i);
		}
	}

	void updateSpheresForPAEvent() {
		detectInPowerArmor() ? _MESSAGE("HOLSTER SPHERE EVENT: STARTING SPHERES CREATION FOR PA") : _MESSAGE("HOLSTER SPHERE EVENT: STARTING SPHERES CREATION FOR NON-POWER ARMOR MODE");
		for (int i = 1; i < 8; i++) {
			if (!isHolsterSlotEnabled(i)) {
				continue;
			}
			std::string c1 = HolsterBones[i];
			std::string hN = "Holster" + std::to_string(i);
			NiTransform _customTransform;
			auto lookup = detectInPowerArmor() ? g_weaponOffsets->getOffset(hN, powerArmor) : g_weaponOffsets->getOffset(hN, noPA);
			if (lookup.has_value()) {
				_customTransform = lookup.value();
			}
			else {
				return;
			}
			BSFixedString bone = c1.c_str();
			BSFixedString MeshPath = hN.c_str();
			BSFixedString MeshName = hN.c_str();
			NiPoint3 pos = _customTransform.pos;
			float radius = _customTransform.scale;
			NiNode* boneNode = FindNode(bone);
			if (!boneNode) {
				return;
			}
			registerHolsterSphere(radius, bone, pos, MeshPath, MeshName, i);
		}
		detectInPowerArmor() ? _MESSAGE("HOLSTER SPHERE EVENT: ALL SPHERES CREATED FOR POWER ARMOR MODE") : _MESSAGE("HOLSTER SPHERE EVENT: ALL SPHERES CREATED FOR NON-POWER ARMOR MODE");
	}

	void destroyAllHolsterSpheres() {
		//_MESSAGE("POWER ARMOR ENTER / EXIT EVENT:: REMOVING REDUNDANT HOLSTER SPHERES");
		resetInteractionState();
		for (auto& entry : HolsterRegisteredObjects) {
			MyHolster* registeredHolster = entry.second;
			if (registeredHolster && registeredHolster->debugSphere) {
				NiNode* sphere = registeredHolster->debugSphere;
				sphere->flags |= 0x1;
				sphere->m_localTransform.scale = 0;
				if (sphere->m_parent) {
					VRSceneGraph::RemoveChild(sphere->m_parent, sphere);
				}
				registeredHolster->debugSphere = nullptr;
			}
			delete registeredHolster;
		}
		HolsterRegisteredObjects.clear();
		//_MESSAGE("POWER ARMOR ENTER / EXIT EVENT:: ALL HOLSTER SPHERES REMOVED");
	}

	void sphereMoveSneak() {
		for (int i = 1; i < 8; i++) {
			if (!isHolsterSlotEnabled(i)) {
				continue;
			}
			const auto it = HolsterRegisteredObjects.find(i);
			if (it != HolsterRegisteredObjects.end() && it->second) {
				BSFixedString NameofMesh = it->second->MeshName;
				NiTransform _customTransform;
				NiNode* hol = it->second->debugSphere;
				if (!hol) {
					continue;
				}
				auto lookup = gInPowerArmor ? g_weaponOffsets->getOffset(NameofMesh.c_str(), sneakinpowerArmor) : g_weaponOffsets->getOffset(NameofMesh.c_str(), noPASneak);
				if (lookup.has_value()) {
					_customTransform = lookup.value();
					hol->m_localTransform.pos = _customTransform.pos;
					float f = 12.0;
					_isConfigModeActive ? hol->m_localTransform.scale = (_customTransform.scale * 2) : hol->m_localTransform.scale = f;
					it->second->offset = _customTransform.pos;
					it->second->radius = _customTransform.scale;
				}
			}
		}
	}

	void sphereMoveStand() {
		for (int i = 1; i < 8; i++) {
			if (!isHolsterSlotEnabled(i)) {
				continue;
			}
			const auto it = HolsterRegisteredObjects.find(i);
			if (it != HolsterRegisteredObjects.end() && it->second) {
				NiTransform _customTransform;
				BSFixedString NameofMesh = it->second->MeshName;
				NiNode* hol = it->second->debugSphere;
				if (!hol) {
					continue;
				}
				auto lookup = gInPowerArmor ? g_weaponOffsets->getOffset(NameofMesh.c_str(), powerArmor) : g_weaponOffsets->getOffset(NameofMesh.c_str(), noPA);
				if (lookup.has_value()) {
					_customTransform = lookup.value();
					hol->m_localTransform.pos = _customTransform.pos;
					float f = 12.0;
					_isConfigModeActive ? hol->m_localTransform.scale = (_customTransform.scale * 2) : hol->m_localTransform.scale = f;
					it->second->offset = _customTransform.pos;
					it->second->radius = _customTransform.scale;

				}
			}
		}
	}

	float getRadius(int handle) {
		if (!isHolsterSlotEnabled(handle)) {
			return 0.0F;
		}
		const auto it = HolsterRegisteredObjects.find(handle);
		if (it == HolsterRegisteredObjects.end() || !it->second) {
			return 0.0F;
		}
		return it->second->radius * 2.0F;
	}

	// Visible Weapon Functions

	void hideMagazine(NiNode* original, const bool IsWMelee) {
		original->m_name.c_str();
		original->m_children.m_emptyRunStart;
		if (original->m_children.m_emptyRunStart != 0) {
			for (auto i = 0; i < original->m_children.m_emptyRunStart; ++i) {
				auto nextNode = original->m_children.m_data[i];
				if (nextNode) {
					BSTriShape* NodeTri = nextNode->GetAsBSTriShape();
					if (NodeTri) {
						BSFixedString TriName = NodeTri->m_name;
						if (IsWMelee) {
							for (int i = 0; i < 2; i++) {
								if (matchSubString(TriName.c_str(), meleeRemovalStrings[i])) {
									NodeTri->m_localTransform.scale = 0.0;
								}
							}
						}
						else {
							for (int i = 0; i < 5; i++) {
								if (matchSubString(TriName.c_str(), weaponRemovalStrings[i])) {
									BSFixedString PName = NodeTri->m_parent->m_name;
									if (matchSubString(PName.c_str(), "P-Mag")) {
										NodeTri->m_localTransform.scale = 0.0;
									}
								}
							}
						}
					}
					else {
						NiNode* nextNodeToCheck = nextNode->GetAsNiNode();
						if (nextNodeToCheck != nullptr) {
							if (nextNodeToCheck->m_children.m_emptyRunStart > 0) {
								hideMagazine(nextNodeToCheck, IsWMelee);
							}
						}
					}
				}
			}
		}
	}

	// Test Function to Fix clones - introduces crashes.
	void clonemeshfix(NiNode* original, NiNode* clone) {
		_MESSAGE("STARTING CLONE FIX");
		original->m_name.c_str();
		original->m_children.m_emptyRunStart;
		if (original->m_children.m_emptyRunStart != 0) {
			for (auto i = 0; i < original->m_children.m_emptyRunStart; ++i) {
				auto nextNode = original->m_children.m_data[i];
				auto nextCloneNode = clone->m_children.m_data[i];
				if (nextNode && nextCloneNode) {
					nextCloneNode->m_name = nextNode->m_name;
					BSTriShape* NodeTri = nextNode->GetAsBSTriShape();
					BSTriShape* CloneTri = nextCloneNode->GetAsBSTriShape();
					NiNode* nextCloneNodeToCheck = nextCloneNode->GetAsNiNode();
					if (NodeTri && CloneTri) {
						c_OriginalNodes.push_back(NodeTri);
						c_CloneNodes.push_back(CloneTri);
					}
					else {
						NiNode* nextNodeToCheck = nextNode->GetAsNiNode();
						NiNode* nextCloneNodeToCheck = nextCloneNode->GetAsNiNode();
						if (nextNode->GetAsNiNode() != nullptr && nextCloneNode != nullptr) {
							if (nextNodeToCheck->m_children.m_emptyRunStart > 0) {
								clonemeshfix(nextNodeToCheck, nextCloneNodeToCheck);
							}
						}
					}
				}
			}
		}
	}

	namespace
	{
		// Name of the power armor piece whose authored poses a slot uses.
		std::string PowerArmorPieceName(const UInt32 handle) {
			const int equipSlot = (handle > 4 || handle < 3) ? 11 : (handle == 3 ? 14 : 15);
			TESForm* equippedItem = (*g_player)->equipData->slots[equipSlot].item;
			const char* name = equippedItem ? equippedItem->GetFullName() : nullptr;
			return name ? name : "";
		}

		// The authored bone-local pose of a weapon in a slot: its own saved
		// offset, else the slot's default for melee or other weapons.
		bool ResolveHolsterPose(const UInt32 handle, const MyWeapon& weapon, NiTransform& pose) {
			auto lookup = g_weaponOffsets->getOffset(FileNameCreation(handle, weapon.ThisForm), normal);
			if (!lookup.has_value()) {
				const bool inPowerArmor = detectInPowerArmor();
				const std::string base = std::string(weapon.isMelee ? "Melee" : "Weapon") + std::to_string(handle);
				lookup = inPowerArmor ?
					g_weaponOffsets->getOffset(base + PowerArmorPieceName(handle), powerArmor) :
					g_weaponOffsets->getOffset(base, noPA);
				if (!lookup.has_value()) {
					lookup = inPowerArmor ? g_weaponOffsets->getOffset(base, powerArmor) : g_weaponOffsets->getOffset(base, noPA);
				}
			}
			if (!lookup.has_value()) {
				return false;
			}
			pose = lookup.value();
			return true;
		}

		// A blending weapon keeps easing toward a moved pose.
		void ApplyHolsterPose(MyWeapon& weapon, const NiTransform& pose) {
			if (!weapon.WeapMesh) {
				return;
			}
			if (weapon.blending) {
				weapon.blendTo = pose;
			} else {
				weapon.WeapMesh->m_localTransform = pose;
			}
		}

		void StripCollision(NiAVObject* object, const int depth) {
			if (!object || depth < 0) {
				return;
			}
			object->m_spCollisionObject = nullptr;
			NiNode* node = object->GetAsNiNode();
			if (!node) {
				return;
			}
			for (UInt32 i = 0; i < node->m_children.m_emptyRunStart; ++i) {
				StripCollision(node->m_children.m_data[i], depth - 1);
			}
		}

		bool IsBlacklisted(const UInt32 handle, const char* weapName) {
			const auto& list = gInPowerArmor ? c_WeaponBlacklistPA[handle] : c_WeaponBlacklist[handle];
			return std::any_of(list.begin(), list.end(), [weapName](const std::string& entry) { return entry == weapName; });
		}

		void RemoveHolsterArtNode(const UInt32 handle) {
			const std::string artName = "HolsterArt" + std::to_string(handle);
			NiNode* art = getChildNode(artName.c_str(), (*g_player)->unkF0->rootNode);
			if (art) {
				art->flags |= 0x1;
				art->m_localTransform.scale = 0;
				VRSceneGraph::RemoveChild(art->m_parent, art);
			}
		}

		NiNode* attachWeaponClone(MyWeapon& record, NiNode* source, const UInt32 handle,
			const char* nodeName, const NiTransform* startWorld) {
			if (!source || !record.wbone || IsBlacklisted(handle, record.WeaponName.c_str())) {
				return nullptr;
			}
			NiCloneProcess proc;
			proc.unk18 = Offsets::cloneAddr1;
			proc.unk48 = Offsets::cloneAddr2;
			NiNode* clone = Offsets::cloneNode(source, &proc);
			if (!clone) {
				_WARNING("Holster %u display skipped: the weapon model could not be cloned", handle);
				return nullptr;
			}
			// Magazines and blood are hidden on the clone only; the source can
			// be the weapon still in the player's hand.
			const bool ripper = matchSubString(record.WeaponName.c_str(), "Ripper");
			if (record.isMelee && !ripper) {
				hideMagazine(clone, record.isMelee);
			} else if (!record.isMelee) {
				for (const auto& excluded : c_ExcludedMagazines) {
					if (matchSubString(record.WeaponName.c_str(), excluded.c_str())) {
						hideMagazine(clone, record.isMelee);
						break;
					}
				}
			}
			// Holstered weapons never collide (furniture, doors, ROCK hands).
			StripCollision(clone, 32);
			clone->m_name = nodeName;
			NiNode* stale = getChildNode(nodeName, (*g_player)->unkF0->rootNode);
			if (stale) {
				stale->m_localTransform.scale = 0;
				VRSceneGraph::RemoveChild(stale->m_parent, stale);
			}
			VRSceneGraph::AttachChild(record.wbone, clone, true);
			record.WeapMesh = clone;

			NiTransform pose{};
			if (!ResolveHolsterPose(handle, record, pose)) {
				pose = clone->m_localTransform;
			}
			NiTransform start{};
			if (startWorld && LocalFromWorld(record.wbone->m_worldTransform, *startWorld, start)) {
				clone->m_localTransform = start;
				record.blendFrom = start;
				record.blendTo = pose;
				record.blendElapsed = 0.0F;
				record.blendDuration = kHolsterBlendSeconds;
				record.blending = true;
			} else {
				clone->m_localTransform = pose;
				record.blending = false;
			}
			return clone;
		}

		void releaseWeaponClone(MyWeapon*& record) {
			if (!record) {
				return;
			}
			if (record->WeapMesh && record->WeapMesh->m_parent) {
				record->WeapMesh->flags |= 0x1;
				record->WeapMesh->m_localTransform.scale = 0;
				VRSceneGraph::RemoveChild(record->WeapMesh->m_parent, record->WeapMesh);
			}
			delete record;
			record = nullptr;
		}

		std::string ChooseArtFile(const UInt32 handle, const std::string& weapon) {
			const std::string normalDir = "Data\\Meshes\\VRH\\Holsters\\" + weapon;
			const std::string powerArmorDir = "Data\\Meshes\\VRH\\Holsters\\_PA\\" + weapon;
			const std::string pattern = "\\HolsterArt" + std::to_string(handle) + "*.nif";
			bool usePowerArmor = false;
			std::vector<std::string> files;
			if (gInPowerArmor) {
				files = getFileNames(powerArmorDir + pattern);
				usePowerArmor = !files.empty();
			}
			if (files.empty()) {
				files = getFileNames(normalDir + pattern);
			}
			if (files.empty()) {
				return {};
			}
			const int maxArtwork = static_cast<int>(files.size()) - 1;
			int& preset = usePowerArmor ? PAholsterArtWorkPresets[handle] : holsterArtWorkPresets[handle];
			(usePowerArmor || gInPowerArmor ? PAholsterMaxValue : holsterMaxValue)[gCurHolster] = maxArtwork - 1;
			if (preset > maxArtwork || preset < 0) {
				preset = 0;
			}
			const std::string path = std::string(usePowerArmor ? "Data/Meshes/VRH/Holsters/_PA/" : "Data/Meshes/VRH/Holsters/") +
				weapon + "/" + files[preset];
			return exists_file(path) ? path : std::string{};
		}

		void attachHolsterArt(MyWeapon& record, const UInt32 handle) {
			if (!record.WeapMesh || !record.wbone) {
				return;
			}
			std::string path = ChooseArtFile(handle, record.WeaponName.c_str());
			if (path.empty()) {
				return;
			}
			NiNode* loaded = loadNifFromFile(path.data());
			if (!loaded) {
				return;
			}
			NiCloneProcess proc;
			proc.unk18 = Offsets::cloneAddr1;
			proc.unk48 = Offsets::cloneAddr2;
			NiNode* art = Offsets::cloneNode(loaded, &proc);
			if (!art) {
				return;
			}
			const std::string artName = "HolsterArt" + std::to_string(handle);
			art->m_name = artName.c_str();
			VRSceneGraph::AttachChild(record.wbone, art, true);
			art->m_localTransform = record.blending ? record.blendTo : record.WeapMesh->m_localTransform;
			record.ArtMesh = art;
		}
	}

	void displayWeapon(NiNode* source, UInt32 handle, const NiTransform* startWorld) {
		const auto registered = WeaponRegisteredObjects.find(handle);
		if (registered == WeaponRegisteredObjects.end() || !registered->second) {
			return;
		}
		_WeaponHolsterInProgress = true;
		RemoveHolsterArtNode(handle);
		MyWeapon& record = *registered->second;
		if (attachWeaponClone(record, source, handle, HolsterNames[handle].c_str(), startWorld)) {
			lastInHolster[handle] = record.ThisForm;
			attachHolsterArt(record, handle);
		}
		_WeaponHolsterInProgress = false;
	}

	void nDestoryWeapon(UInt32 handle, bool destroyArt, bool playsound) {
		CancelStoredVisual(static_cast<int>(handle));
		if (gShowWeapons) {
			const auto registered = WeaponRegisteredObjects.find(handle);
			if (registered != WeaponRegisteredObjects.end()) {
				if (registered->second) {
					if (playsound) {
						playSwordSFX(handle);
					}
					NiNode* weap = registered->second->WeapMesh;
					if (weap) {
						weap->flags |= 0x1;
						weap->m_localTransform.scale = 0;
						if (weap->m_parent) {
							VRSceneGraph::RemoveChild(weap->m_parent, weap);
						}
					}
					delete registered->second;
				}
				WeaponRegisteredObjects.erase(registered);
			}
			if (destroyArt && *g_player && (*g_player)->unkF0 && (*g_player)->unkF0->rootNode) {
				NiNode* artToRemove = getChildNode(hArt[handle], (*g_player)->unkF0->rootNode); //Remove any Holster Art applied this slot
				if (artToRemove) {
					artToRemove->flags |= 0x1;
					artToRemove->m_localTransform.scale = 0;
					if (artToRemove->m_parent) {
						VRSceneGraph::RemoveChild(artToRemove->m_parent, artToRemove);
					}
				}
			}
		}
	}

	void moveWeapon(int handle) {
		if (!gShowWeapons) {
			return;
		}
		const std::string artName = "HolsterArt" + std::to_string(handle);
		NiNode* art = getChildNode(artName.c_str(), (*g_player)->unkF0->rootNode);
		const auto registered = WeaponRegisteredObjects.find(handle);
		if (registered != WeaponRegisteredObjects.end() && registered->second) {
			MyWeapon& weapon = *registered->second;
			NiTransform pose{};
			if (weapon.WeapMesh && ResolveHolsterPose(handle, weapon, pose)) {
				ApplyHolsterPose(weapon, pose);
				if (art) {
					art->m_localTransform = pose;
				}
			}
			return;
		}
		if (art) {
			auto lookup = g_weaponOffsets->getOffset(FileNameCreation(handle, lastInHolster[handle]), normal);
			if (lookup.has_value()) {
				art->m_localTransform = lookup.value();
			} else {
				art->flags |= 0x1;
				art->m_localTransform.scale = 0;
				VRSceneGraph::RemoveChild(art->m_parent, art);
			}
		}
	}

	void nRegisterWeapon(BSFixedString weapName, TESForm* baseForm, NiNode* source, UInt32 handle, bool isMeleeW, const NiTransform* startWorld) {
		if (!gShowWeapons || !baseForm || !source) {
			return;
		}
		if (weapName == ".44") {
			weapName = "44cal";
		}
		NiNode* bone = FindNode(gInPowerArmor ? PAWeaponBones[handle].c_str() : WeaponBones[handle].c_str());
		if (!bone) {
			return;
		}
		// One display per slot: a newer one replaces the old.
		nDestoryWeapon(handle, false, false);
		MyWeapon* weapon = new MyWeapon(bone, HolsterNames[handle].c_str(), isMeleeW, weapName, baseForm->formID);
		VisualArt[handle] = weapName;
		WeaponRegisteredObjects[handle] = weapon;
		displayWeapon(source, handle, startWorld);
	}

	// Papyrus}

	void SetPointingHand() {
		setFingerPositionScalar((gIsRightHanded ? true : false), 0.0, 1.0, 0.0, 0.0, 0.0);
	}

	void RestorePointingHand() {
		restoreFingerPoseControl((gIsRightHanded ? true : false));
	}

	void playSwordSFX(int handle) {
		BSFixedString weapname = WeaponRegisteredObjects[handle]->WeaponName;
		bool isMelee = WeaponRegisteredObjects[handle]->isMelee;
		if (isMelee) {
			for (int i = 0; i < c_SwordDrawSFX.size(); i++) {
				if (matchSubString(weapname, c_SwordDrawSFX[i].c_str())) {
					SInt32 evt0 = HolsterEvent_DrawSword;
					if (g_HolsterEventRegs.m_data.size() > 0) {
						g_HolsterEventRegs.ForEach(
							[&evt0](const EventRegistration<NullParameters>& reg) {
								SendPapyrusEvent2<SInt32, UInt32>(reg.handle, reg.scriptName, HolsterEventName, evt0, gCurHolster);
							}
						);
					}
				}
			}
		}
	}

	void RegisterForHolsterEvents(StaticFunctionTag* base, VMObject* thisObject) {
		if (!thisObject) {
			return;
		}
		g_HolsterEventRegs.Register(thisObject->GetHandle(), thisObject->GetObjectType());
		_MESSAGE("Papyrus Event:: 'VirtualHolstersQuestScript.pex' Registered to Receive Events");
		//Disable VATS if user has chosen to.
		if (c_VATSDisabled) {
			//_MESSAGE("VATS DISABLED");
			SInt32 evt2 = HolsterEvent_DisableVATS;
			if (g_HolsterEventRegs.m_data.size() > 0) {
				g_HolsterEventRegs.ForEach(
					[&evt2](const EventRegistration<NullParameters>& reg) {
						SendPapyrusEvent2<SInt32, UInt32>(reg.handle, reg.scriptName, HolsterEventName, evt2, gCurHolster);
					}
				);
			}
		}

		//Re-enable Activate 
		if (c_ActivationDisabled) {
			SInt32 evt2 = HolsterEvent_EnableActivate;
			if (g_HolsterEventRegs.m_data.size() > 0) {
				g_HolsterEventRegs.ForEach(
					[&evt2](const EventRegistration<NullParameters>& reg) {
						SendPapyrusEvent2<SInt32, UInt32>(reg.handle, reg.scriptName, HolsterEventName, evt2, gCurHolster);
					}
				);
			}
		}
	}

	void UnRegisterForHolsterEvents(StaticFunctionTag* base, VMObject* thisObject) {
		if (!thisObject) {
			return;
		}
		g_HolsterEventRegs.Unregister(thisObject->GetHandle(), thisObject->GetObjectType());
		_MESSAGE("PAPYRUS EVENT: VIRTUAL HOLSTERS SCRIPT UN-REGISTERED FOR HOLSTER EVENTS");
	}

	bool RegisterFuncs(VirtualMachine* vm) {
		vm->RegisterFunction(new NativeFunction1<StaticFunctionTag, void, VMObject*>("RegisterForHolsterEvents", "HolsterFunctions", Holsters::RegisterForHolsterEvents, vm));
		vm->RegisterFunction(new NativeFunction1<StaticFunctionTag, void, VMObject*>("UnRegisterForHolsterEvents", "HolsterFunctions", Holsters::UnRegisterForHolsterEvents, vm));
		return true;
	}
}

// ============================================================================
// VirtualHolsters API Implementation 
// 
// Credit: With thanks to Asciimov.
// ============================================================================

class VirtualHolstersAPIImpl : public VirtualHolstersAPI
{
public:
	static VirtualHolstersAPIImpl& GetSingleton()
	{
		static VirtualHolstersAPIImpl instance;
		return instance;
	}

	std::uint32_t VH_CALL GetVersion() const override
	{
		return 1;
	}

	bool VH_CALL IsHandInHolsterZone(bool isLeft) const override
	{
		return Holsters::isHandInHolsterZone(isLeft);
	}

	std::uint32_t VH_CALL GetCurrentHolster() const override
	{
		return gCurHolster;
	}

	bool VH_CALL IsHolsterFree(std::uint32_t holsterIndex) const override
	{
		if (holsterIndex < 1 || holsterIndex > 7 ||
			!Holsters::isHolsterSlotEnabled(holsterIndex)) {
			return false;
		}
		return (holsteredWeapNames[holsterIndex] == "Empty");
	}

	const char* VH_CALL GetHolsteredWeaponName(std::uint32_t holsterIndex) const override
	{
		if (holsterIndex < 1 || holsterIndex > 7) {
			return "";
		}
		return holsteredWeapNames[holsterIndex].c_str();
	}

	bool VH_CALL IsWeaponAlreadyHolstered(const char* weaponName) const override
	{
		if (!weaponName || weaponName[0] == '\0') {
			return false;
		}
		std::string name(weaponName);
		for (int i = 1; i <= 7; i++) {
			if (Holsters::isHolsterSlotEnabled(i) &&
				holsteredWeapNames[i] == name) {
				return true;
			}
		}
		return false;
	}

	bool VH_CALL GetHolsterPosition(std::uint32_t holsterIndex, float& outX, float& outY, float& outZ) const override
	{
		if (holsterIndex < 1 || holsterIndex > 7 ||
			!Holsters::isHolsterSlotEnabled(holsterIndex)) {
			return false;
		}
		auto it = Holsters::HolsterRegisteredObjects.find(holsterIndex);
		if (it == Holsters::HolsterRegisteredObjects.end() || !it->second || !it->second->bone) {
			return false;
		}
		const NiPoint3 worldPos = Holsters::HolsterCenterWorld(*it->second);
		outX = worldPos.x;
		outY = worldPos.y;
		outZ = worldPos.z;
		return true;
	}

	float VH_CALL GetHolsterRadius(std::uint32_t holsterIndex) const override
	{
		if (holsterIndex < 1 || holsterIndex > 7 ||
			!Holsters::isHolsterSlotEnabled(holsterIndex)) {
			return 0.0f;
		}
		auto it = Holsters::HolsterRegisteredObjects.find(holsterIndex);
		if (it == Holsters::HolsterRegisteredObjects.end() || !it->second) {
			return 0.0f;
		}
		return it->second->radius;
	}

	bool VH_CALL IsInitialized() const override
	{
		// Check if bone tree is initialized (set false after MainLoop processes)
		// and that holsters are registered
		return !Holsters::initBoneTreeFlag && !Holsters::HolsterRegisteredObjects.empty();
	}

	bool VH_CALL IsGripAssignedToHolster() const override
	{
		// k_EButton_Grip = 2 in OpenVR
		return (Holsters::c_holsterButtonID == vr::EVRButtonId::k_EButton_Grip);
	}

	std::uint32_t VH_CALL GetHolsterButtonId() const override
	{
		return static_cast<std::uint32_t>(Holsters::c_holsterButtonID);
	}

	bool VH_CALL IsLeftHandedMode() const override
	{
		return !gIsRightHanded;
	}

	void VH_CALL switchHandMode() const override
	{
		gIsRightHanded = !gIsRightHanded;
	}

	bool VH_CALL AddHolster(std::uint32_t holsterIndex, const char* weaponName,
		void* baseForm, void* weaponRef, bool isMelee) override
	{
		if (holsterIndex < 1 || holsterIndex > 7) {
			_MESSAGE("AddHolster: Invalid holster index %d", holsterIndex);
			return false;
		}
		if (!Holsters::isHolsterSlotEnabled(holsterIndex)) {
			_MESSAGE("AddHolster: Holster %d is disabled", holsterIndex);
			return false;
		}
		if (!weaponName || weaponName[0] == '\0') {
			_MESSAGE("AddHolster: Invalid weapon name");
			return false;
		}
		if (!baseForm || !weaponRef) {
			_MESSAGE("AddHolster: Invalid baseForm or weaponRef");
			return false;
		}

		// Cast void* back to game types
		TESForm* form = reinterpret_cast<TESForm*>(baseForm);
		TESObjectREFR* refr = reinterpret_cast<TESObjectREFR*>(weaponRef);

		if (holsteredWeapNames[holsterIndex] != "Empty") {
			_MESSAGE("AddHolster: Holster %d is occupied", holsterIndex);
			return false;
		}
		Holsters::addHolster(static_cast<int>(holsterIndex), weaponName, form, refr, isMelee);
		if (holsteredWeapNames[holsterIndex] != weaponName) {
			_MESSAGE("AddHolster: Holster %d did not accept '%s'", holsterIndex, weaponName);
			return false;
		}
		_MESSAGE("AddHolster: Added '%s' to holster %d (melee=%d)", weaponName, holsterIndex, isMelee);
		return true;
	}


private:
	VirtualHolstersAPIImpl() = default;
};

// Exported function - DO NOT CHANGE SIGNATURE
extern "C" __declspec(dllexport) VirtualHolstersAPI * VH_CALL VHAPI_GetApi()
{
	return &VirtualHolstersAPIImpl::GetSingleton();
}



