
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
#include "HandHolsters.h"
#include "HolsterMath.h"
#include "HolsterTrace.h"
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

	// Writes the recorded defaults only when no file exists yet (CREATE_NEW),
	// so a file the user created in the meantime is never overwritten.
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

	// Which holster slots are switched on ([Holster Slots] in the INI).
	SlotPolicy::SlotMask s_enabledHolsterSlots = SlotPolicy::kAllSlotsMask;

	// The player skeleton the holster bones were looked up in. When the game
	// builds a new skeleton, the bones are looked up again next frame.
	const void* gResolvedRoot = nullptr;
	const void* gResolvedBoneTree = nullptr;
	int gResolvedTransforms = 0;
	bool s_skeletonRebuildPending = false;

	// Frame time, and timers that used to be sleeping threads.
	float gFrameSeconds = 0.0F;
	Clock::time_point s_lastFrame;
	Clock::time_point s_fistsPressedUntil;
	Clock::time_point s_menuCheckUntil;
	Clock::time_point s_infoPressedUntil;
	// Config mode opens on a double press: the second press must follow the
	// first this fast.
	const auto kConfigDoublePress = std::chrono::milliseconds(300);
	Clock::time_point s_configFirstPress;

	bool TryGetHandPosition(
		BSFlattenedBoneTree* tree,
		bool isLeft,
		NiPoint3& position)
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

	void UpdateHolsterSpherePresentation()
	{
		for (auto& [slot, holster] : HolsterRegisteredObjects) {
			if (!holster) {
				continue;
			}

			holster->stickyLeft = gLeftHand.zone == slot;
			holster->stickyRight = gRightHand.zone == slot;
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
		gLeftHand.zone = Interaction::kNoSlot;
		gLeftHand.anchorAvailable = false;
		gRightHand.zone = Interaction::kNoSlot;
		gRightHand.anchorAvailable = false;
		gLeftHand.trace.nearestSlot = Interaction::kNoSlot;
		gLeftHand.trace.zoneSamples = 0;
		gRightHand.trace.nearestSlot = Interaction::kNoSlot;
		gRightHand.trace.zoneSamples = 0;
		isHolsterIntersected = false;
		for (auto& [slot, holster] : HolsterRegisteredObjects) {
			(void)slot;
			if (holster) {
				holster->stickyLeft = false;
				holster->stickyRight = false;
			}
		}
	}

	// The slot's display, or null. Never inserts: a null entry would make
	// an empty slot look occupied to the draw and holster paths.
	MyWeapon* SlotDisplay(UInt32 slot)
	{
		const auto registered = WeaponRegisteredObjects.find(slot);
		return registered != WeaponRegisteredObjects.end() ? registered->second : nullptr;
	}

	// The slot's holster sphere record, or null; never inserts either.
	MyHolster* SlotHolster(UInt32 slot)
	{
		const auto registered = HolsterRegisteredObjects.find(slot);
		return registered != HolsterRegisteredObjects.end() ? registered->second : nullptr;
	}

	bool SlotAvailable(UInt32 slot)
	{
		return slot >= 1 && slot <= kMaximumHolsterSlots &&
			isHolsterSlotEnabled(slot) && HolsterContainers[slot] && *g_player;
	}

	// The game's activate stays checked for a second after a holster action.
	void StartMenuCheck() {
		MenuCheck = true;
		s_menuCheckUntil = Clock::now() + std::chrono::milliseconds(1000);
	}

	// Main Functions

	bool isHolsterSlotEnabled(UInt32 slot) {
		return SlotPolicy::IsEnabled(s_enabledHolsterSlots, slot);
	}

	UInt32 getNextEnabledHolster(UInt32 current) {
		return SlotPolicy::NextEnabled(s_enabledHolsterSlots, current);
	}

	bool isHandInHolsterZone(bool isLeft) {
		return GetHand(isLeft).zone != Interaction::kNoSlot;
	}

	UInt32 getHandHolster(bool isLeft) {
		return GetHand(isLeft).zone;
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
		gResolvedRoot = (*g_player)->unkF0->rootNode;
		gResolvedBoneTree = rt;
		gResolvedTransforms = rt->numTransforms;
		_MESSAGE("BoneTree Init Completed: root=%p tree=%p", gResolvedRoot, gResolvedBoneTree);
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
		// Ready once the player's third-person root exists: zones, hands and
		// bones all come from its flattened bone tree. The F4SE header's
		// firstPersonSkeleton (PlayerCharacter+0xB78) is a flat layout; on
		// FO4VR that offset holds lastKnownGoodPosition.z (CommonLibF4VR
		// PlayerCharacter, 0xB70), which is 0 on any floor at height 0, so
		// gating on it switched every zone off while the player stood there.
		if (!(*g_player) ||
			!(*g_player)->unkF0 ||
			!(*g_player)->unkF0->rootNode) {
			TraceFrameSkip(!(*g_player) ? FrameSkip_NoPlayer : FrameSkip_NoRoot);
			resetInteractionState();
			return;
		}
		{
			if (initBoneTreeFlag) {
				if (!initBoneTree()) {
					TraceFrameSkip(FrameSkip_BoneTreeInit);
					resetInteractionState();
					return;
				}
				initBoneTreeFlag = false;
				s_skeletonRebuildPending = false;
				PlayerLoaded();
			} else if (s_skeletonRebuildPending) {
				// The player's skeleton was rebuilt under the holsters: resolve
				// bones and displays again, the same rebuild a power armor
				// change runs. Logged every time it runs.
				s_skeletonRebuildPending = false;
				_MESSAGE("[%s] Holster skeleton rebuild: holster bones and weapon displays resolved again",
					TraceTime());
				resetInteractionState();
				weaponsPAChange();
			}
			TraceFrameSkip(FrameSkip_None);
			const auto now = Clock::now();
			gFrameSeconds = s_lastFrame == Clock::time_point{} ? 0.0F :
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
				ServiceThrowableDraws();
				ServiceDeposits();
			}
			ServiceHandoffs();
			ServiceWarmWeapons();
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
			// Buttons this mod reserves in ROCK disappear from the game's
			// controller read, so with ROCK bound they are read raw from ROCK.
			const uint64_t leftButtons = Rock::IsBound() ?
				Rock::OverlayRawButtons(true, leftControllerAvailable ? leftController.ulButtonPressed : 0) :
				(leftControllerAvailable ? leftController.ulButtonPressed : 0);
			const uint64_t rightButtons = Rock::IsBound() ?
				Rock::OverlayRawButtons(false, rightControllerAvailable ? rightController.ulButtonPressed : 0) :
				(rightControllerAvailable ? rightController.ulButtonPressed : 0);
			const uint64_t dominantHand =
				gIsRightHanded ? rightButtons : leftButtons;
			const uint64_t offHand =
				gIsRightHanded ? leftButtons : rightButtons;
			vr::VRControllerAxis_t leftStick = leftController.rAxis[0];
			vr::VRControllerAxis_t rightStick = rightController.rAxis[0];
			if (Rock::IsBound()) {
				float x = 0.0F;
				float y = 0.0F;
				if (Rock::ReadRawStick(true, x, y)) {
					leftStick.x = x;
					leftStick.y = y;
				}
				if (Rock::ReadRawStick(false, x, y)) {
					rightStick.x = x;
					rightStick.y = y;
				}
			}
			const vr::VRControllerAxis_t doinantHandStick =
				gIsRightHanded ? rightStick : leftStick;
			const vr::VRControllerAxis_t offHandStick =
				gIsRightHanded ? leftStick : rightStick;

			const auto holsterButtonMask = vr::ButtonMaskFromId(
				static_cast<vr::EVRButtonId>(c_holsterButtonID));
			const bool leftHolsterHeld = (leftButtons & holsterButtonMask) != 0;
			const bool rightHolsterHeld = (rightButtons & holsterButtonMask) != 0;
			const bool gameStopped = isGameStopped();
			// Looking through a scope, the hands are on the weapon.
			const bool scopeOpen = *g_ui && (*g_ui)->IsMenuOpen(BSFixedString("ScopeMenu"));
			HolsterButtonInput holsterInput;
			holsterInput.leftControllerAvailable = leftControllerAvailable;
			holsterInput.rightControllerAvailable = rightControllerAvailable;
			holsterInput.leftHolsterHeld = leftHolsterHeld;
			holsterInput.rightHolsterHeld = rightHolsterHeld;
			holsterInput.leftGameHeld = (leftController.ulButtonPressed & holsterButtonMask) != 0;
			holsterInput.rightGameHeld = (rightController.ulButtonPressed & holsterButtonMask) != 0;
			holsterInput.gameStopped = gameStopped;
			holsterInput.scopeOpen = scopeOpen;
			UpdateHandGestures(holsterInput);
			// Config mode drives both hands' buttons and sticks (grip shift and
			// cycle, A rotate, stick press, menu, trigger exit, sticks move the
			// holster): ROCK must not grab or act on them and the game must not
			// jump or move, also until they are released after exit. Entering
			// it (stick press with the dominant hand in a zone, weapon away)
			// must not reach ROCK or the game either.
			static bool s_configReleasePending = false;
			if (_isConfigModeActive) {
				s_configReleasePending = true;
			} else if (s_configReleasePending && ((leftButtons | rightButtons) & Rock::kReservableButtons) == 0) {
				s_configReleasePending = false;
			}
			if (_isConfigModeActive || s_configReleasePending) {
				Rock::ReserveHandExclusive(true);
				Rock::ReserveHandExclusive(false);
			} else if (getHandHolster(!gIsRightHanded) != Interaction::kNoSlot &&
				!(*g_player)->actorState.IsWeaponDrawn()) {
				Rock::ReserveHand(!gIsRightHanded, false,
					vr::ButtonMaskFromId(static_cast<vr::EVRButtonId>(c_configButtonID)), false);
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

				RunHandActions();

				// Config mode opens on a double press of the config button with
				// the dominant hand in a holster and the weapon away, so a single
				// stick click there never opens it by accident.
				const UInt32 dominantHolster =
					getHandHolster(!gIsRightHanded);
				if (ConfigButtonPressed && !_isConfigButtonPressed) {
					_isConfigButtonPressed = true;
					if (dominantHolster != Interaction::kNoSlot && !(*g_player)->actorState.IsWeaponDrawn()) {
						const auto now = Clock::now();
						const bool secondPress = s_configFirstPress != Clock::time_point{} &&
							now - s_configFirstPress <= kConfigDoublePress;
						s_configFirstPress = secondPress ? Clock::time_point{} : now;
						BSFixedString pipboyMenu("PipboyMenu");
						if (secondPress && !(*g_ui)->GetMenu(pipboyMenu)) {
							gCurHolster = dominantHolster;
							FinishWeaponBlends();
							setupConfigMode();
							_isConfigModeActive = true;
							_MESSAGE("Holster config mode entered at holster %u by a double press of the config button", dominantHolster);
						}
					} else {
						s_configFirstPress = {};
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
					_MESSAGE("Holster config mode exited");
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
		c_holsterButtonID = (int)ini.GetLongValue("Button Config", "HolsterButtonID", vr::EVRButtonId::k_EButton_Grip);
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
		for (const char* folder : { "VRH_weapon_offsets", "VRH_WeaponBlackList", "VRH_MagazineBlackList" }) {
			const int copied = config::seedMissingDefaults(folder);
			if (copied < 0) {
				_WARNING("Cannot prepare %s in the VirtualHolsters configuration folder", folder);
			} else if (copied > 0) {
				_MESSAGE("Copied %d shipped default file(s) into %s", copied, folder);
			}
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

	// A shoulder zone hangs off HmdNode, whose axes are the same for both
	// shoulders and in power armor (x right, y forward, z up), unlike the
	// shoulder weapon displays on the collarbones: the sticks move it
	// straight along those axes.
	bool IsShoulderZoneEdit() {
		return configMode == 1 && (gCurHolster == 1 || gCurHolster == 2);
	}

	void scaleHolster() {
		if (configMode == 1) {
			MyHolster* holster = SlotHolster(gCurHolster);
			if (holster && holster->debugSphere) {
				NiNode* hol = holster->debugSphere;
				float nScale(lAxisOffsetY);
				float origScale = hol->m_localTransform.scale;
				if ((origScale + nScale) < 1) {
					//_MESSAGE("SCALE RETURN");
					return;
				}
				hol->m_localTransform.scale = (origScale + nScale);
				float radiusA = hol->m_localTransform.scale;
				radiusA = radiusA / 2;
				holster->radius = radiusA;
			}
		}
	}

	void moveholsterX() {
		NiNode* hol = nullptr;
		bool isMelee = false;
		MyHolster* holster = SlotHolster(gCurHolster);
		if (configMode == 1) {
			if (!holster) {
				return;
			}
			hol = holster->debugSphere;
		}
		else {
			const MyWeapon* display = SlotDisplay(gCurHolster);
			if (!display) {
				return;
			}
			hol = display->WeapMesh;
			isMelee = display->isMelee;
		}
		if (!hol) {
			return;
		}
		{
			float nLoc;
			float origLoc;
			if (IsShoulderZoneEdit()) {
				nLoc = rAxisOffsetX;
				origLoc = hol->m_localTransform.pos.x;
				if ((origLoc + nLoc) > 50 || (origLoc + nLoc) < -50) {
					return;
				}
				hol->m_localTransform.pos.x = (origLoc + nLoc);
			}
			else if (gCurHolster == 1) {
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
			holster->offset = hol->m_localTransform.pos;
		}
	}

	void moveholsterY() {
		NiNode* hol = nullptr;
		bool isMelee = false;
		MyHolster* holster = SlotHolster(gCurHolster);
		if (configMode == 1) {
			if (!holster) {
				return;
			}
			hol = holster->debugSphere;
		}
		else {
			const MyWeapon* display = SlotDisplay(gCurHolster);
			if (!display) {
				return;
			}
			hol = display->WeapMesh;
			isMelee = display->isMelee;
		}
		if (!hol) {
			return;
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
			holster->offset = hol->m_localTransform.pos;
		}
	}

	void moveholsterZ() {
		NiNode* hol = nullptr;
		MyHolster* holster = SlotHolster(gCurHolster);
		if (configMode == 1) {
			if (!holster) {
				return;
			}
			hol = holster->debugSphere;
		}
		else {
			const MyWeapon* display = SlotDisplay(gCurHolster);
			if (!display) {
				return;
			}
			hol = display->WeapMesh;
		}
		if (!hol) {
			return;
		}
		float nLoc;
		float origLoc;
		if (IsShoulderZoneEdit()) {
			nLoc = lAxisOffsetX;
			origLoc = hol->m_localTransform.pos.z;
			if ((origLoc + nLoc) > 50 || (origLoc + nLoc) < -50) {
				return;
			}
			hol->m_localTransform.pos.z = (origLoc + nLoc);
		}
		else if (gCurHolster == 1) {
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
			holster->offset = hol->m_localTransform.pos;
		}
	}

	void rotateholsterX() {
		NiNode* hol = nullptr;
		Matrix44 rot;
		if (!SlotDisplay(gCurHolster)) {
			return;
		}
		bool MeleeWeapon = (SlotDisplay(gCurHolster)->isMelee);
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
			if (SlotDisplay(gCurHolster)) {
				hol = SlotDisplay(gCurHolster)->WeapMesh;
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
		if (!SlotDisplay(gCurHolster)) {
			return;
		}
		bool MeleeWeapon = (SlotDisplay(gCurHolster)->isMelee);
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
			if (SlotDisplay(gCurHolster)) {
				hol = SlotDisplay(gCurHolster)->WeapMesh;
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
			if (SlotDisplay(gCurHolster)) {
				hol = SlotDisplay(gCurHolster)->WeapMesh;
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
			if (SlotDisplay(gCurHolster)) {
				itemName = SlotDisplay(gCurHolster)->HolsterName;
				storedName = SlotDisplay(gCurHolster)->WeaponName;
				mesh2 = SlotDisplay(gCurHolster)->WeapMesh;
				if (!isInMasterMode) {
					UInt32 WeapForm = SlotDisplay(gCurHolster)->ThisForm;
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
					if (!SlotDisplay(gCurHolster)->isMelee) {
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
		// A different root or tree than the holsters were resolved from: the
		// holster bones and boneTreeMap indices belong to the old skeleton,
		// so no zone can be reached until they are resolved again.
		const void* root = (*g_player)->unkF0->rootNode;
		if (gResolvedBoneTree && (root != gResolvedRoot || boneTree != gResolvedBoneTree)) {
			_MESSAGE("[%s] Holster skeleton changed: root %p->%p tree %p->%p transforms %d->%d",
				TraceTime(), gResolvedRoot, root, gResolvedBoneTree,
				static_cast<const void*>(boneTree), gResolvedTransforms, boneTree->numTransforms);
			gResolvedRoot = root;
			gResolvedBoneTree = boneTree;
			gResolvedTransforms = boneTree->numTransforms;
			s_skeletonRebuildPending = true;
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

		TraceNearestHolster(gLeftHand, leftSamples.data(), leftSampleCount);
		TraceNearestHolster(gRightHand, rightSamples.data(), rightSampleCount);

		const UInt32 previousLeft = gLeftHand.zone;
		const UInt32 previousRight = gRightHand.zone;
		gLeftHand.trace.position = leftPosition;
		gRightHand.trace.position = rightPosition;
		gLeftHand.anchorAvailable = leftAvailable;
		gRightHand.anchorAvailable = rightAvailable;
		gLeftHand.zone = leftAvailable ?
			Interaction::SelectStableZone(
				previousLeft,
				leftSamples.data(),
				leftSampleCount) :
			Interaction::kNoSlot;
		gRightHand.zone = rightAvailable ?
			Interaction::SelectStableZone(
				previousRight,
				rightSamples.data(),
				rightSampleCount) :
			Interaction::kNoSlot;

		const bool leftEntered =
			gLeftHand.zone != Interaction::kNoSlot &&
			gLeftHand.zone != previousLeft;
		const bool rightEntered =
			gRightHand.zone != Interaction::kNoSlot &&
			gRightHand.zone != previousRight;

		if (!_isConfigModeActive) {
			// Update the non-preferred hand first so a simultaneous entry
			// leaves the configured hand as the legacy current holster.
			if (gIsRightHanded) {
				if (leftEntered) {
					gCurHolster = gLeftHand.zone;
				}
				if (rightEntered) {
					gCurHolster = gRightHand.zone;
				}
			} else {
				if (rightEntered) {
					gCurHolster = gRightHand.zone;
				}
				if (leftEntered) {
					gCurHolster = gLeftHand.zone;
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
			gLeftHand.zone != Interaction::kNoSlot ||
			gRightHand.zone != Interaction::kNoSlot;
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
								const std::string n = "OrbMesh" + std::to_string(handle) + "_" + std::to_string(i);
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

	void hideMagazine(NiNode* original, bool IsWMelee) {
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

	// Name of the power armor piece whose authored poses a slot uses.
	std::string PowerArmorPieceName(UInt32 handle) {
		const int equipSlot = (handle > 4 || handle < 3) ? 11 : (handle == 3 ? 14 : 15);
		TESForm* equippedItem = (*g_player)->equipData->slots[equipSlot].item;
		const char* name = equippedItem ? equippedItem->GetFullName() : nullptr;
		return name ? name : "";
	}

	// The authored bone-local pose of a weapon in a slot: its own saved
	// offset, else the slot's default for melee or other weapons.
	bool ResolveHolsterPose(UInt32 handle, const MyWeapon& weapon, NiTransform& pose) {
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

	void StripCollision(NiAVObject* object, int depth) {
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

	bool IsBlacklisted(UInt32 handle, const char* weapName) {
		const auto& list = gInPowerArmor ? c_WeaponBlacklistPA[handle] : c_WeaponBlacklist[handle];
		return std::any_of(list.begin(), list.end(), [weapName](const std::string& entry) { return entry == weapName; });
	}

	void RemoveHolsterArtNode(UInt32 handle) {
		const std::string artName = "HolsterArt" + std::to_string(handle);
		NiNode* art = getChildNode(artName.c_str(), (*g_player)->unkF0->rootNode);
		if (art) {
			art->flags |= 0x1;
			art->m_localTransform.scale = 0;
			VRSceneGraph::RemoveChild(art->m_parent, art);
		}
	}

	NiNode* attachWeaponClone(MyWeapon& record, NiNode* source, UInt32 handle,
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

	std::string ChooseArtFile(UInt32 handle, const std::string& weapon) {
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

	void attachHolsterArt(MyWeapon& record, UInt32 handle) {
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
						playSwordSFX(*registered->second);
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

	// Takes the record it reports on: a drawn weapon's record may already
	// have left WeaponRegisteredObjects.
	void playSwordSFX(const MyWeapon& weapon) {
		BSFixedString weapname = weapon.WeaponName;
		bool isMelee = weapon.isMelee;
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

	bool VH_CALL AddHolster(std::uint32_t holsterIndex, void* weaponRef) override
	{
		if (holsterIndex < 1 || holsterIndex > 7) {
			_MESSAGE("AddHolster: Invalid holster index %u", holsterIndex);
			return false;
		}
		if (!weaponRef) {
			_MESSAGE("AddHolster: Invalid weaponRef");
			return false;
		}
		if (holsteredWeapNames[holsterIndex] != "Empty") {
			_MESSAGE("AddHolster: Holster %u is occupied", holsterIndex);
			return false;
		}
		auto* refr = reinterpret_cast<TESObjectREFR*>(weaponRef);
		if (!Holsters::DepositReference(holsterIndex, refr)) {
			_MESSAGE("AddHolster: Holster %u did not accept reference %08X", holsterIndex, refr->formID);
			return false;
		}
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



