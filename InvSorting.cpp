#include "InvSorting.h"
#include "Holsters.h"
#include "NativeEquipment.h"

#include <algorithm>
#include <map>
#include <vector>

namespace Holsters {
	RelocAddr<std::uint64_t*> g_ActorEquipManager(0x5a38bf8);
	RelocAddr<_ActorEquipItem> ActorEquipItem(0x140CD10);

	namespace {
		// About ten seconds at 90 Hz for a temporary reference's 3D to load.
		constexpr int kStoredVisualFrames = 900;
		constexpr std::size_t kMaximumStoredVisuals = 8;

		struct StoredVisual {
			TESObjectREFR* reference = nullptr;
			TESForm* form = nullptr;
			TESObjectWEAP* weapon = nullptr;
			int handle = 0;
			int framesLeft = 0;
		};
		std::vector<StoredVisual> s_storedVisuals;

		// Worn bits of a stack (one per equipped copy), as the engine stores them.
		[[nodiscard]] int WornCopies(const BGSInventoryItem::Stack* stack) {
			if (!stack) {
				return 0;
			}
			const unsigned bits = stack->flags & BGSInventoryItem::Stack::kFlagEquipped;
			return static_cast<int>((bits & 1u) + ((bits >> 1) & 1u) + ((bits >> 2) & 1u));
		}

		[[nodiscard]] TBO_InstanceData* StackInstance(ExtraDataList* extra) {
			if (!extra) {
				return nullptr;
			}
			auto* eid = DYNAMIC_CAST(extra->GetByType(kExtraData_InstanceData), BSExtraData, ExtraInstanceData);
			return eid ? eid->instanceData : nullptr;
		}

		// The holster tag of a stack: its ETDD name when it has instance data
		// and a display name, otherwise the base form's name.
		void StackName(TESForm* form, ExtraDataList* extra, std::string& name, UInt16& length) {
			ExtraTextDisplayData* etdd = nullptr;
			if (extra && StackInstance(extra)) {
				etdd = DYNAMIC_CAST(extra->GetByType(kExtraData_TextDisplayData), BSExtraData, ExtraTextDisplayData);
			}
			if (etdd && etdd->name.c_str()) {
				name = etdd->name.c_str();
				length = etdd->nameLength;
				return;
			}
			const char* full = form ? form->GetFullName() : nullptr;
			name = full ? full : "";
			length = static_cast<UInt16>(name.size());
		}

		// Visits every weapon stack of the player's inventory under its read
		// lock; the visitor returns false to stop.
		template <class Visitor>
		void VisitWeaponStacks(Visitor&& visitor) {
			if (!*g_player) {
				return;
			}
			BGSInventoryList* inventory = (*g_player)->inventoryList;
			if (!inventory) {
				return;
			}
			inventory->inventoryLock.LockForRead();
			bool keepGoing = true;
			for (size_t position = 0; keepGoing && position < inventory->items.count; position++) {
				BGSInventoryItem item = inventory->items[position];
				if (!item.form || item.form->formType != kFormType_WEAP || !item.stack) {
					continue;
				}
				int stackID = 0;
				for (auto* stack = item.stack; keepGoing && stack; stack = stack->next, ++stackID) {
					keepGoing = visitor(item, stack, stackID);
				}
			}
			inventory->inventoryLock.Unlock();
		}

		WeaponStack MakeStack(const BGSInventoryItem& item, BGSInventoryItem::Stack* stack, int stackID) {
			WeaponStack result;
			result.found = true;
			result.form = item.form;
			result.weapon = static_cast<TESObjectWEAP*>(item.form);
			result.item = item;
			result.stackID = stackID;
			result.instance = StackInstance(stack->extraData);
			StackName(item.form, stack->extraData, result.name, result.nameLength);
			result.melee = IsMeleeWeapon(result.weapon);
			return result;
		}

		[[nodiscard]] int UnwornCopiesNamed(const std::string& name) {
			int copies = 0;
			VisitWeaponStacks([&](const BGSInventoryItem& item, BGSInventoryItem::Stack* stack, int) {
				std::string stackName;
				UInt16 length = 0;
				StackName(item.form, stack->extraData, stackName, length);
				if (stackName == name) {
					copies += (std::max)(0, static_cast<int>(stack->count) - WornCopies(stack));
				}
				return true;
			});
			return copies;
		}

		void WriteTag(int handle, const char* name, UInt16 length) {
			if (handle < 1 || handle > 7 || !HolsterContainers[handle]) {
				return;
			}
			BGSInventoryList* holsterINV = HolsterContainers[handle]->inventoryList;
			if (!holsterINV) {
				return;
			}
			holsterINV->inventoryLock.LockForReadAndWrite();
			if (ExtraTextDisplayData* etdd = FindHolster(holsterINV)) {
				etdd->name = name;
				etdd->nameLength = length;
				etdd->type = -2;
				holsteredWeapNames[handle] = name;
			}
			holsterINV->inventoryLock.Unlock();
		}

		TESObjectREFR* CreateTemporaryReference(TESObjectWEAP* weapon, BGSInventoryItem item, int stackID) {
			BGSInventoryItem::Stack* stack = item.stack;
			for (int i = 0; stack && i < stackID; ++i) {
				stack = stack->next;
			}
			if (!stack || !weapon) {
				return nullptr;
			}
			NEW_REFR_DATA refrData;
			refrData.location = (*g_player)->pos;
			refrData.location.z = refrData.location.z + 1000;
			refrData.direction = (*g_player)->rot;
			refrData.interior = (*g_player)->parentCell;
			refrData.world = Offsets::TESObjectREFR_GetWorldSpace(*g_player);
			refrData.extra = stack->extraData;
			refrData.object = weapon;
			std::size_t handle = 0;
			void* newHandle = Offsets::TESDataHandler_CreateReferenceAtLocation(*g_dataHandler, &handle, &refrData);
			std::uintptr_t newRefr = 0x0;
			Offsets::BSPointerHandleManagerInterface_GetSmartPointer(newHandle, &newRefr);
			return reinterpret_cast<TESObjectREFR*>(newRefr);
		}
	}

	bool IsMeleeWeapon(TESObjectWEAP* weapon) {
		if (!weapon) {
			return false;
		}
		constexpr UInt32 kMeleeKeywords[2] = { 0x4A0A4, 0x4A0A45 };
		for (UInt32 i = 0; i < weapon->keyword.numKeywords; i++) {
			const auto* keyword = weapon->keyword.keywords[i];
			if (keyword && (keyword->formID == kMeleeKeywords[0] || keyword->formID == kMeleeKeywords[1])) {
				return true;
			}
		}
		return false;
	}

	WeaponStack FindEquippedStack(const UInt32 formId, const std::uintptr_t instance) {
		WeaponStack result;
		if (!formId) {
			return result;
		}
		VisitWeaponStacks([&](const BGSInventoryItem& item, BGSInventoryItem::Stack* stack, int stackID) {
			if (item.form->formID != formId || WornCopies(stack) == 0) {
				return true;
			}
			const auto stackInstance = reinterpret_cast<std::uintptr_t>(StackInstance(stack->extraData));
			if (instance && stackInstance && stackInstance != instance) {
				return true;
			}
			result = MakeStack(item, stack, stackID);
			return false;
		});
		return result;
	}

	WeaponStack FindStoredStack(const std::string& name) {
		WeaponStack result;
		if (name.empty() || name == "Empty") {
			return result;
		}
		VisitWeaponStacks([&](const BGSInventoryItem& item, BGSInventoryItem::Stack* stack, int stackID) {
			if (static_cast<int>(stack->count) <= WornCopies(stack)) {
				return true;
			}
			std::string stackName;
			UInt16 length = 0;
			StackName(item.form, stack->extraData, stackName, length);
			if (stackName != name) {
				return true;
			}
			result = MakeStack(item, stack, stackID);
			return false;
		});
		return result;
	}

	bool CanTagAnother(const std::string& name, const bool sourceIsWorn) {
		int tags = 0;
		for (int i = 1; i < 8; i++) {
			if (holsteredWeapNames[i] == name) {
				++tags;
			}
		}
		return tags < UnwornCopiesNamed(name) + (sourceIsWorn ? 1 : 0);
	}

	void SetHolsterTag(const int handle, const std::string& name, const UInt16 nameLength) {
		WriteTag(handle, name.c_str(), nameLength ? nameLength : static_cast<UInt16>(name.size()));
	}

	void resetHolsterName(const int handle) {
		WriteTag(handle, "Empty", 5);
	}

	void ReconcileHolsterTags(const std::array<bool, 8>& busy) {
		std::map<std::string, int> available;
		std::map<std::string, int> used;
		for (int i = 1; i < 8; i++) {
			const std::string name = holsteredWeapNames[i];
			if (name == "Empty") {
				continue;
			}
			auto found = available.find(name);
			if (found == available.end()) {
				found = available.emplace(name, UnwornCopiesNamed(name)).first;
			}
			if (busy[i] || used[name] < found->second) {
				++used[name];
				continue;
			}
			_MESSAGE("Holster %d released '%s': no unworn copy remains", i, name.c_str());
			nDestoryWeapon(i, true, false);
			resetHolsterName(i);
		}
	}

	bool NativeUnequip(const WeaponStack& stack) {
		if (!stack.found || !*g_player) {
			return false;
		}
		BGSObjectInstance instance(nullptr, nullptr);
		instance.object = stack.form;
		instance.instanceData = stack.instance;
		Offsets::UnEquipObject(*g_ActorEquipManager, *g_player, &instance, 1, nullptr, -1, false, true, true, false, 0);
		const auto equipped = NativeEquipment::ReadEquippedWeapons()[0];
		return equipped.formId != stack.form->formID ||
			(stack.instance && equipped.instance != reinterpret_cast<std::uintptr_t>(stack.instance));
	}

	bool NativeEquip(const WeaponStack& stack) {
		if (!stack.found || !*g_player || !stack.weapon) {
			return false;
		}
		BGSObjectInstance instance(nullptr, nullptr);
		instance.object = stack.form;
		instance.instanceData = stack.instance;
		BGSEquipSlot* equipSlot = stack.weapon->equipType.GetEquipSlot();
		Offsets::EquipObject(*g_ActorEquipManager, *g_player, &instance, stack.stackID, 1, equipSlot, true, false, true, true, false);
		return true;
	}

	void QueueStoredVisual(const WeaponStack& stack, const int handle) {
		if (!stack.found || handle < 1 || handle > 7) {
			return;
		}
		CancelStoredVisual(handle);
		if (s_storedVisuals.size() >= kMaximumStoredVisuals) {
			_WARNING("Holster %d visual skipped: %zu stored visuals already loading", handle, s_storedVisuals.size());
			return;
		}
		TESObjectREFR* reference = CreateTemporaryReference(stack.weapon, stack.item, stack.stackID);
		if (!reference) {
			_WARNING("Holster %d visual skipped: temporary reference was not created", handle);
			return;
		}
		s_storedVisuals.push_back({ reference, stack.form, stack.weapon, handle, kStoredVisualFrames });
	}

	void CancelStoredVisual(const int handle) {
		for (auto it = s_storedVisuals.begin(); it != s_storedVisuals.end();) {
			if (it->handle == handle) {
				Offsets::DeleteRef(it->reference);
				it = s_storedVisuals.erase(it);
			} else {
				++it;
			}
		}
	}

	void ServiceStoredVisuals() {
		// Registering a display cancels that slot's queued visuals, so take
		// the loaded ones out of the queue before registering any.
		std::vector<StoredVisual> ready;
		for (auto it = s_storedVisuals.begin(); it != s_storedVisuals.end();) {
			auto* loaded = it->reference ? it->reference->unkF0 : nullptr;
			if (loaded && loaded->rootNode && loaded->rootNode->GetAsNiNode()) {
				ready.push_back(*it);
				it = s_storedVisuals.erase(it);
				continue;
			}
			if (--it->framesLeft <= 0) {
				_WARNING("Holster %d visual abandoned: temporary reference 3D did not load", it->handle);
				Offsets::DeleteRef(it->reference);
				it = s_storedVisuals.erase(it);
				continue;
			}
			++it;
		}
		for (const auto& visual : ready) {
			NiNode* root = visual.reference->unkF0->rootNode->GetAsNiNode();
			nRegisterWeapon(visual.form->GetFullName(), visual.form, root, visual.handle,
				IsMeleeWeapon(visual.weapon), nullptr);
			Offsets::DeleteRef(visual.reference);
		}
	}

	void addHolster(int handle, std::string weapName, TESForm* baseForm, TESObjectREFR* object, bool isMeleeW) {
		if (handle < 1 || handle > 7 ||
			!isHolsterSlotEnabled(handle) ||
			!HolsterContainers[handle] ||
			!baseForm ||
			!object ||
			!object->GetObjectRootNode()) {
			return;
		}
		nRegisterWeapon(baseForm->GetFullName(), baseForm, object->GetObjectRootNode()->GetAsNiNode(), handle, isMeleeW, nullptr);
		SetHolsterTag(handle, weapName, static_cast<UInt16>(weapName.size()));
	}

	ExtraTextDisplayData* FindHolster(BGSInventoryList* inventory) {
		for (size_t invPos = 0; invPos < inventory->items.count; invPos++) {
			BGSInventoryItem item = inventory->items[invPos];
			TESForm* form = item.form;
			BGSInventoryItem::Stack* stack = item.stack;
			if (form && form->formType == kFormType_WEAP && stack && stack->extraData) {
				return DYNAMIC_CAST(stack->extraData->GetByType(kExtraData_TextDisplayData), BSExtraData, ExtraTextDisplayData);
			}
		}
		return nullptr;
	}

	void GameLoadInvSort() {
		bool isFirstRun = true;
		for (int i = 1; i < 8; i++) {
			if (!HolsterContainers[i]) {
				holsteredWeapNames[i] = "Empty";
				continue;
			}
			BGSInventoryList* holsterINV = HolsterContainers[i]->inventoryList;
			if (holsterINV) { // Holster Inventory Located - Proceed As Normal.
				holsterINV->inventoryLock.LockForReadAndWrite();
				ExtraTextDisplayData* CurrentETDD = FindHolster(holsterINV);
				if (CurrentETDD) {
					std::string name = CurrentETDD->name;
					std::string mess = "-------- ETDD Link Established with Holster0" + std::to_string(i);
					std::string mess2 = "------------ Holster Status: " + name;
					_MESSAGE(mess.c_str());
					_MESSAGE(mess2.c_str());
					if (isHolsterSlotEnabled(i)) {
						holsteredWeapNames[i] = CurrentETDD->name;
					}
					else {
						CurrentETDD->name = "Empty";
						CurrentETDD->nameLength = 5;
						CurrentETDD->type = -2;
						holsteredWeapNames[i] = "Empty";
						_MESSAGE("------------ Disabled holster association cleared");
					}
				}
				else {
					_MESSAGE("------------ Holster Status: Fail - Something Went Wrong!");
				}
				holsterINV->inventoryLock.Unlock();
				isFirstRun = false;
			}
			else {  // No Inventory List Found - This Signifies a Fist Run Scenario. Create VRH Template in Holsters Inventory.
				std::string mess = "-------- Unable To Establish ETDD Link with Holster0" + std::to_string(i);
				std::string mess2 = "------------ Holster0" + std::to_string(i) + ":: ETDD Config Start";
				_MESSAGE(mess.c_str());
				_MESSAGE(mess2.c_str());
				NEW_REFR_DATA* refrData = new NEW_REFR_DATA();
				refrData->location = (*g_player)->pos;
				refrData->direction = (*g_player)->rot;
				refrData->interior = (*g_player)->parentCell;
				refrData->world = Offsets::TESObjectREFR_GetWorldSpace(*g_player);
				ExtraDataList* extraData = (ExtraDataList*)Offsets::MemoryManager_Allocate(g_mainHeap, 0x28, 0, false);
				Offsets::ExtraDataList_ExtraDataList(extraData);
				extraData->m_refCount += 1;
				Offsets::ExtraDataList_setCount(extraData, 10);
				refrData->extra = extraData;
				TESObjectWEAP* holsterBase = DYNAMIC_CAST(GetFormFromFile(0x01C1F7, "VirtualHolsters.esp"), TESForm, TESObjectWEAP);  //Get The Holster Template Form
				refrData->object = holsterBase;
				void* weapon = new std::size_t;
				void* newHandle = Offsets::TESDataHandler_CreateReferenceAtLocation(*g_dataHandler, weapon, refrData);
				std::uintptr_t newRefr = 0x0;
				Offsets::BSPointerHandleManagerInterface_GetSmartPointer(newHandle, &newRefr);
				TESObjectREFR* currentRefr = (TESObjectREFR*)newRefr;                                                                 //New ObjectRef Created From Holster Template Form
				if (currentRefr) {
					TESBoundObject* boundObject = DYNAMIC_CAST(holsterBase, TESForm, TESBoundObject);
					ExtraDataList* list = currentRefr->extraDataList;
					if (boundObject && list) {
						Offsets::AddObjecttoContainer(HolsterContainers[i], boundObject, reinterpret_cast<ExtraDataList*>(&list), 1, nullptr, ITEM_REMOVE_REASON::kNone);  //ObjectRef added to Holsters Container
						holsterINV = HolsterContainers[i]->inventoryList;
						if (holsterINV) {
							Offsets::DeleteRef(currentRefr);                                                                                                               //Delete ObjectRef from gameworld - no longer needed
							holsterINV->inventoryLock.LockForReadAndWrite();
							ExtraTextDisplayData* CurrentETDD = FindHolster(holsterINV);
							if (CurrentETDD) {
								CurrentETDD->name = "Empty";
								CurrentETDD->nameLength = 5;
								CurrentETDD->type = -2;
								holsteredWeapNames[i] = "Empty";
								_MESSAGE("---------------- ETDD Link Established with Holster0%d (Empty)", i);
							}
							holsterINV->inventoryLock.Unlock();
						}
					}
				}
			}
		}
		if (isFirstRun) {
			return;
		}
		ReconcileHolsterTags({});
		for (int i = 1; i < 8; i++) {
			if (!isHolsterSlotEnabled(i) || holsteredWeapNames[i] == "Empty") {
				continue;
			}
			QueueStoredVisual(FindStoredStack(holsteredWeapNames[i]), i);
		}
	}
}
