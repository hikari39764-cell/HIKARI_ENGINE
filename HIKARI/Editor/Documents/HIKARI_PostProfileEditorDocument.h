#pragma once
#include "Assets/Post/HIKARI_PostAsset.h"
#include "Editor/Documents/HIKARI_ValidatedAssetDocument.h"
namespace HIKARI::EDITOR {
struct PostProfileDocumentTraits {
    using Asset = POST::PostProfileAsset;
    static constexpr AssetType type = AssetType::PostProfile;
    static constexpr const char* title = "Post Profile";
    static constexpr const char* directory = "PostProfiles";
    static constexpr const char* stem = "PostProfile";
    static constexpr const char* extension = ".hpostprofile";
    static auto Write(const Asset& a) { return POST::WritePostProfileAsset(a); }
    static bool Normalize(Asset& a, std::string& e) { return POST::ReadPostProfileAsset(Write(a), a, e); }
    static bool Load(const std::filesystem::path& p, Asset& a, std::string& e) {
        return POST::LoadPostProfileAsset(p, a, e);
    }
    static bool Save(const std::filesystem::path& p, const Asset& a, std::string& e) {
        return POST::SavePostProfileAsset(p, a, e);
    }
};
using PostProfileEditorDocument = ValidatedAssetDocument<PostProfileDocumentTraits>;
} // namespace HIKARI::EDITOR
