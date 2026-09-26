#include "Assets/ContentLibrary/HIKARI_ContentLibraryInternal.h"
#include "Assets/Material/HIKARI_MaterialDependencies.h"
#include "Assets/Material/Serialization/HIKARI_MaterialSourceJson.h"
#include "Assets/Vfx/Serialization/HIKARI_VfxSourceJson.h"

namespace HIKARI::ASSETS::LIBRARY::INTERNAL {
bool LoadSource(const std::filesystem::path& path, AssetType type, Source& out, std::string& message) {
    if (type == AssetType::Texture) {
        out = std::monostate{};
        return true;
    }
    if (type == AssetType::Material) {
        MaterialAssetData value;
        if (!MATERIAL_SERIALIZATION::LoadMaterialSourceJson(path, value, message))
            return false;
        out = std::move(value);
        return true;
    }
    if (type == AssetType::VfxEffect) {
        VFX::VfxAssetData value;
        if (!VFX::SERIALIZATION::LoadVfxSourceJson(path, value, message))
            return false;
        out = std::move(value);
        return true;
    }
    message = "Content package has an unsupported source type";
    return false;
}

bool SaveSource(const std::filesystem::path& path, const Source& source, std::string& message) {
    if (const auto* value = std::get_if<MaterialAssetData>(&source))
        return MATERIAL_SERIALIZATION::SaveMaterialSourceJson(path, *value, message);
    if (const auto* value = std::get_if<VFX::VfxAssetData>(&source))
        return VFX::SERIALIZATION::SaveVfxSourceJson(path, *value, message);
    message = "Binary sources must be copied without conversion";
    return false;
}

void VisitReferences(Source& source, const std::function<void(AssetGuid&)>& visit) {
    if (auto* material = std::get_if<MaterialAssetData>(&source)) {
        VisitMaterialAssetReferences(
            *material, [&](AssetGuid& guid, MaterialDependencyKind, std::string) { visit(guid); }, true);
    } else if (auto* vfx = std::get_if<VFX::VfxAssetData>(&source)) {
        for (auto& emitter : vfx->emitters) {
            visit(emitter.renderCommon.colorTextureAssetGuid);
            visit(emitter.renderCommon.materialAssetGuid);
        }
    }
}
} // namespace HIKARI::ASSETS::LIBRARY::INTERNAL
