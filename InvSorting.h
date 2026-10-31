#pragma once
#include "f4se/GameRTTI.h"
#include "f4se\GameExtraData.h"
#include "f4se\GameFormComponents.h"
#include "f4se\GameForms.h"
#include "utils.h"

#include <array>
#include <cstdint>
#include <string>

namespace Holsters {
	// One inventory weapon stack, captured on the frame thread and used in the
	// same frame. A holster slot persists only its weapon's display name in
	// the slot container's ETDD; the stack is resolved again on every use.
	struct WeaponStack {
		bool found = false;
		TESForm* form = nullptr;
		TESObjectWEAP* weapon = nullptr;
		BGSInventoryItem item{};
		int stackID = -1;
		TBO_InstanceData* instance = nullptr;
		std::string name;
		UInt16 nameLength = 0;
		bool melee = false;
	};

	// The worn stack holding the exact equipped weapon (form and, when the
	// stack carries instance data, the same instance).
	WeaponStack FindEquippedStack(UInt32 formId, std::uintptr_t instance);
	// A stack named `name` that still has an unworn copy.
	WeaponStack FindStoredStack(const std::string& name);
	// Whether another slot may hold a weapon named `name`: tags of one name
	// never exceed that name's unworn copies (the holstered weapon counts as
	// unworn once it leaves the hand).
	bool CanTagAnother(const std::string& name, bool sourceIsWorn);
	void SetHolsterTag(int handle, const std::string& name, UInt16 nameLength);
	void resetHolsterName(int handle);
	// Clears every tag whose weapon is no longer available unworn, e.g. after
	// the Pip-Boy equipped it. busy[slot] skips slots with a transfer running.
	void ReconcileHolsterTags(const std::array<bool, 8>& busy);

	bool NativeUnequip(const WeaponStack& stack);
	bool NativeEquip(const WeaponStack& stack);
	bool IsMeleeWeapon(TESObjectWEAP* weapon);

	// Holster visuals for stored weapons without an equipped model (load,
	// power armor, armor changes): a temporary world reference whose 3D is
	// cloned once it loads, then deleted. Serviced once per frame.
	void QueueStoredVisual(const WeaponStack& stack, int handle);
	void CancelStoredVisual(int handle);
	void ServiceStoredVisuals();

	void addHolster(int handle, std::string weapName, TESForm* baseForm, TESObjectREFR* object, bool isMeleeW);
	ExtraTextDisplayData* FindHolster(BGSInventoryList* inventory);
	void GameLoadInvSort();
}
