#pragma once

#include <Windows.h>
#include <ShlObj.h>
#include <array>
#include <filesystem>
#include <string>
#include <string_view>

// Runtime configuration lives in Documents/My Games/Fallout4VR/Mods_Config/
// VirtualHolsters (INI, weapon offsets, blacklists, sound lists) instead of the
// game's Data folder, so installing or updating the mod never overwrites a
// user's settings.
namespace Holsters::config
{
	inline const std::string& directory()
	{
		// Resolve once on the first configuration load, before in-game edits.
		static const std::string result = []() -> std::string {
			try {
				std::array<wchar_t, MAX_PATH> documents{};
				if (FAILED(SHGetFolderPathW(nullptr, CSIDL_MYDOCUMENTS, nullptr,
						SHGFP_TYPE_CURRENT, documents.data()))) return {};
				return (std::filesystem::path(documents.data()) / "My Games" / "Fallout4VR" /
					"Mods_Config" / "VirtualHolsters").string();
			} catch (...) {
				return {};
			}
		}();
		return result;
	}

	inline std::string path(std::string_view relative)
	{
		const auto& root = directory();
		return root.empty() ? std::string{} : (std::filesystem::path(root) / relative).string();
	}

	// The mod ships default holster offsets, blacklists and sound lists in its
	// plugin folder; the runtime reads and saves only the Mods_Config copy.
	// Copies each shipped file the user does not have yet, once, and never
	// overwrites a user's file. Load time only. Returns the files copied, or
	// -1 when the folder could not be prepared.
	inline int seedMissingDefaults(std::string_view folder) noexcept
	{
		const auto target = path(folder);
		if (target.empty()) {
			return -1;
		}
		std::error_code error;
		const std::filesystem::path shipped = std::filesystem::path("Data") / "F4SE" / "Plugins" / folder;
		if (!std::filesystem::is_directory(shipped, error)) {
			return 0;
		}
		std::filesystem::create_directories(target, error);
		if (error) {
			return -1;
		}
		int copied = 0;
		for (const auto& entry : std::filesystem::directory_iterator(shipped, error)) {
			if (!entry.is_regular_file(error)) {
				continue;
			}
			const auto destination = std::filesystem::path(target) / entry.path().filename();
			if (std::filesystem::exists(destination, error)) {
				continue;
			}
			if (std::filesystem::copy_file(entry.path(), destination, error) && !error) {
				++copied;
			}
		}
		return copied;
	}
}
