#include "NativeEquipment.h"

#include <F4SE/F4SE.h>
#include <RE/Bethesda/Actor.h>
#include <RE/Bethesda/PlayerCharacter.h>
#include <RE/Bethesda/Settings.h>
#include <RE/NetImmerse/NiNode.h>

#include <cstdio>
#include <cstring>

namespace Holsters::NativeEquipment
{
	namespace
	{
		constexpr int kMaximumDepth = 24;
		constexpr std::uint32_t kNodeBudget = 4096;
		constexpr std::size_t kMaximumEquippedItems = 32;

		struct Search
		{
			const char* token{};
			std::uint32_t budget{ kNodeBudget };
			RE::NiAVObject* inside{};
			RE::NiAVObject* outside{};
			bool insideAmbiguous{};
			bool outsideAmbiguous{};
		};

		[[nodiscard]] bool Visible(const RE::NiAVObject* node) noexcept
		{
			return node && !node->GetAppCulled() && node->local.scale > 0.0001F;
		}

		void Visit(RE::NiAVObject* node, const int depth, bool insideWeapon, Search& search) noexcept
		{
			if (!node || depth < 0 || search.budget == 0 || !Visible(node)) {
				return;
			}
			--search.budget;
			const char* name = node->name.c_str();
			if (!insideWeapon && name && std::strcmp(name, "Weapon") == 0) {
				insideWeapon = true;
			}
			if (name && std::strstr(name, search.token)) {
				auto& slot = insideWeapon ? search.inside : search.outside;
				auto& ambiguous = insideWeapon ? search.insideAmbiguous : search.outsideAmbiguous;
				if (slot) {
					ambiguous = true;
				} else {
					slot = node;
				}
				// The model root owns its own parts; do not descend for a
				// second copy of the token inside the same model.
				return;
			}
			auto* asNode = node->IsNode();
			if (!asNode) {
				return;
			}
			auto& children = asNode->children;
			for (decltype(children.capacity()) i = 0; i < children.capacity() && search.budget > 0; ++i) {
				Visit(children[i].get(), depth - 1, insideWeapon, search);
			}
		}
	}

	std::array<Equipped, 2> ReadEquippedWeapons() noexcept
	{
		std::array<Equipped, 2> result{};
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* middle = player && player->currentProcess ? player->currentProcess->middleHigh : nullptr;
		if (!middle || middle->equippedItems.size() > kMaximumEquippedItems) {
			return result;
		}
		for (const auto& entry : middle->equippedItems) {
			const auto index = entry.equipIndex.index;
			if (index >= result.size() || !entry.item.object || !entry.item.object->IsWeapon()) {
				continue;
			}
			if (result[index].formId) {
				// Two records on one index are ambiguous: report neither.
				return {};
			}
			result[index].formId = entry.item.object->GetFormID();
			result[index].instance = reinterpret_cast<std::uintptr_t>(entry.item.instanceData.get());
		}
		return result;
	}

	void* FindEquippedModel(const std::uint32_t index, const std::uint32_t formId) noexcept
	{
		if (index > 1 || !formId) {
			return nullptr;
		}
		auto* player = RE::PlayerCharacter::GetSingleton();
		auto* skeleton = player ? player->firstPerson3D.get() : nullptr;
		if (!skeleton) {
			return nullptr;
		}
		char token[16]{};
		std::snprintf(token, sizeof(token), "(%08X)", formId);
		Search search{};
		search.token = token;
		Visit(skeleton, kMaximumDepth, false, search);
		if (index == 0) {
			return search.insideAmbiguous ? nullptr : search.inside;
		}
		return search.outsideAmbiguous ? nullptr : search.outside;
	}

	void* WandNode(const bool isLeft) noexcept
	{
		auto* player = RE::PlayerCharacter::GetSingleton();
		if (!player) {
			return nullptr;
		}
		// The primary wand is the game's main hand; left-handed mode swaps it.
		// Settings live for the process; look the name up once, not per frame.
		static const RE::Setting* setting = RE::GetINISetting("bLeftHandedMode:VR");
		const bool leftHanded = setting && setting->GetBinary();
		// PlayerCharacter VR node table (player + 0x6E0): primaryWandNode at
		// +0x6F0, SecondaryWandNode at +0x768, as ROCK and FRIK read them.
		const std::uintptr_t offset = (isLeft == leftHanded) ? 0x6F0 : 0x768;
		auto* node = *reinterpret_cast<RE::NiNode**>(reinterpret_cast<std::uintptr_t>(player) + offset);
		return node && node->parent ? node : nullptr;
	}
}
