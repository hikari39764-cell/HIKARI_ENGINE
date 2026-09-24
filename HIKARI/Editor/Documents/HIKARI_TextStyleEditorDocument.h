#pragma once
#include "Assets/Text/HIKARI_TextStyleAsset.h"
#include "Editor/Documents/HIKARI_ValidatedAssetDocument.h"
namespace HIKARI::EDITOR {
struct TextStyleDocumentTraits {
    using Asset = TextStyleAsset;
    static constexpr AssetType type = AssetType::TextStyle;
    static constexpr const char* title = "Text Style";
    static constexpr const char* directory = "TextStyles";
    static constexpr const char* stem = "TextStyle";
    static constexpr const char* extension = ".htextstyle";
    static auto Write(const Asset& a) { return WriteTextStyleAsset(a); }
    static bool Normalize(Asset& a, std::string& e) { return ReadTextStyleAsset(Write(a), a, e); }
    static bool Load(const std::filesystem::path& p, Asset& a, std::string& e) { return LoadTextStyleAsset(p, a, e); }
    static bool Save(const std::filesystem::path& p, const Asset& a, std::string& e) {
        return SaveTextStyleAsset(p, a, e);
    }
};
using TextStyleEditorDocument = ValidatedAssetDocument<TextStyleDocumentTraits>;
} // namespace HIKARI::EDITOR
