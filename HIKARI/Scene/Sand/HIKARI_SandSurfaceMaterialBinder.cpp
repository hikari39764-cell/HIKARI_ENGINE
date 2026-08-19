#include "Scene/Sand/HIKARI_SandSurfaceMaterialBinder.h"

#include <memory>
#include <string>
#include <string_view>

#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/Material/HIKARI_MaterialBuiltInAssets.h"
#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Core/HIKARI_Logger.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Material/HIKARI_MaterialRuntimeBuilder.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"

namespace HIKARI::SAND {

    bool EnsureSandSurfaceMaterialOverride(
        ModelComponent& model,
        const AssetRegistry& assetRegistry) noexcept {

        const auto* sandMaterial =
            assetRegistry.FindAs<MaterialAssetDescriptor>(
                AssetId{ std::string(
                    ASSETS::MATERIAL::kBuiltInSandMaterialAssetId) });
        if (sandMaterial == nullptr) {
            return false;
        }

        const Material* current =
            model.GetRuntimeMaterialOverride(0u);
        if (current != nullptr &&
            current->GetBaseColorTexturePath().find(
                "TerrainLayer_Atlas_Color") != std::string::npos) {
            return true;
        }

        MaterialRuntimeBuilder builder{};
        auto& programRegistry =
            RENDER3D::MATERIAL::GetRuntimeMaterialProgramRegistry();
        auto runtimeMaterial = std::make_unique<Material>();
        if (!builder.BuildRuntimeMaterial(
                sandMaterial->sourceData,
                sandMaterial->compileResult,
                assetRegistry,
                programRegistry,
                *runtimeMaterial,
                sandMaterial->id.value)) {
            HIKARI_LOG_WARN(
                "[MaterialRuntime][Terrain] failed to build "
                "built-in stylized layer material.");
            return false;
        }

        model.SetRuntimeMaterialOverride(
            0u,
            std::move(runtimeMaterial));
        HIKARI_LOG_INFO(
            "[MaterialRuntime][Terrain] built-in stylized layer "
            "material override applied.");
        return true;
    }

} // namespace HIKARI::SAND
