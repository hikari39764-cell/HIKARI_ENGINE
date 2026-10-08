#pragma once
#include "Assets/RenderStyle/HIKARI_RenderStyleAsset.h"
#include "Editor/Documents/HIKARI_ValidatedAssetDocument.h"
namespace HIKARI::EDITOR {
struct RenderStyleDocumentTraits {
    using Asset = RENDERSTYLE::RenderStyleAsset;
    static constexpr AssetType type = AssetType::RenderStyle;
    static constexpr const char* title = "Render Style";
    static constexpr const char* directory = "RenderStyles";
    static constexpr const char* stem = "RenderStyle";
    static constexpr const char* extension = ".hrenderstyle";
    static auto Write(const Asset& a) { return RENDERSTYLE::WriteRenderStyleAsset(a); }
    static bool Normalize(Asset& a, std::string& e) { return RENDERSTYLE::ReadRenderStyleAsset(Write(a), a, e); }
    static bool Load(const std::filesystem::path& p, Asset& a, std::string& e) {
        return RENDERSTYLE::LoadRenderStyleAsset(p, a, e);
    }
    static bool Save(const std::filesystem::path& p, const Asset& a, std::string& e) {
        return RENDERSTYLE::SaveRenderStyleAsset(p, a, e);
    }
};
using RenderStyleEditorDocument = ValidatedAssetDocument<RenderStyleDocumentTraits>;
} // namespace HIKARI::EDITOR
