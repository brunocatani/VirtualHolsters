#pragma once

#include <Windows.h>
#include <ShlObj.h>
#include <array>
#include <filesystem>
#include <string>
#include <string_view>

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
}
