#pragma once
#include "Assets/Ui/HIKARI_UiAnimationAsset.h"
#include "Editor/Documents/HIKARI_ValidatedAssetDocument.h"
namespace HIKARI::EDITOR {
struct UiAnimationDocumentTraits {
    using Asset = UiAnimationAsset;
    static constexpr AssetType type = AssetType::UiAnimation;
    static constexpr const char* title = "UI Animation";
    static constexpr const char* directory = "UI/Animations";
    static constexpr const char* stem = "UiAnimation";
    static constexpr const char* extension = ".huianim";
    static auto Write(const Asset& a) { return WriteUiAnimationAsset(a); }
    static bool Normalize(Asset& a, std::string& e) { return ReadUiAnimationAsset(Write(a), a, e); }
    static bool Load(const std::filesystem::path& p, Asset& a, std::string& e) { return LoadUiAnimationAsset(p, a, e); }
    static bool Save(const std::filesystem::path& p, const Asset& a, std::string& e) {
        return SaveUiAnimationAsset(p, a, e);
    }
};
using UiAnimationEditorDocument = ValidatedAssetDocument<UiAnimationDocumentTraits>;
} // namespace HIKARI::EDITOR
