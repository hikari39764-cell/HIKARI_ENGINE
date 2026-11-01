#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "RenderContracts/Asset/HIKARI_AssetTypeEnum.h"
#include "Core/HIKARI_AssetGuid.h"
#include "RenderContracts/Format/HIKARI_CookedAssetFormat.h"
#include "RenderContracts/Lighting/HIKARI_SkyMode.h"
#include "RenderContracts/Material/HIKARI_MaterialAssetData.h"
#include "RenderContracts/Material/HIKARI_MaterialProgramFormat.h"
#include "RenderContracts/Model/HIKARI_ModelMaterialBindingAsset.h"
#include "RenderContracts/Texture/HIKARI_TextureTypes.h"
#include "RenderContracts/Vfx/HIKARI_ParticleMaterialMode.h"

namespace HIKARI {

namespace ASSETS::SHADER {
struct ShaderCookedArtifact;
}

namespace VFX {
struct VfxProgram;
}

enum class ModelImporterKind {
    Builtin,
    Gltf,
    Assimp,
};

enum class ModelGeometryCookProfile {
    Scene,
    Character,
};

struct ModelClusterCookOptions {
    bool buildClusterGeometry = true;
    ModelGeometryCookProfile profile = ModelGeometryCookProfile::Scene;
    uint32_t maxLodCount = 5;
    float lodQualityBias = 1.0f;
    bool partitionLargeSurfaces = true;
    float largeSurfaceTargetExtent = 3.0f;
    bool lockPartitionBorders = true;
};

struct AnimationClipAlias {
    std::string name;
    std::string sourceName;
};

struct ModelMaterialOverrideDesc {
    std::string targetMaterialName;
    std::string shaderProfileId;
    std::string materialFxProfileId;
    bool doubleSided = false;
};

struct ModelAnimationImportDesc {
    std::string defaultClip;
    std::vector<AnimationClipAlias> clips;
};

struct AssetDescriptor {
    virtual ~AssetDescriptor() = default;

    AssetGuid id{};
    AssetType type = AssetType::Unknown;
    std::string sourcePath{};
    uint32_t version = 2;
};

struct ModelAssetDescriptor final : AssetDescriptor {
    ModelImporterKind importer = ModelImporterKind::Gltf;
    bool preload = true;
    std::string clusteredGeometryPath{};
    std::string collisionGeometryPath{};
    std::vector<ModelMaterialOverrideDesc> materialOverrides{};
    ModelAnimationImportDesc animation{};
};

struct SkyAssetDescriptor final : AssetDescriptor {
    std::string meshAssetId{};
    std::string textureAssetId{};
    SkyMode preferredMode = SkyMode::Cubemap;
    std::string irradiancePath{};
    std::string prefilteredPath{};
    std::string brdfLutPath{};
    uint32_t prefilteredMipCount = 1;
    bool hasIbl = false;
};

struct TextureAssetDescriptor final : AssetDescriptor {
    TextureAssetDimension dimension = TextureAssetDimension::Texture2D;
    TextureAssetColorSpace colorSpace = TextureAssetColorSpace::Auto;
    TextureUsage usage = TextureUsage::Auto;
    TextureCompression compression = TextureCompression::Auto;
    TextureMipPolicy mipPolicy = TextureMipPolicy::Auto;
    uint32_t width = 0u;
    uint32_t height = 0u;
    TextureAssetAlphaClass alphaClass = TextureAssetAlphaClass::Unknown;
};

struct MaterialAssetDescriptor final : AssetDescriptor {
    MaterialAssetData sourceData{};
    MaterialCompileResult compileResult{};
    // 既存 runtime builder が v4 へ移行するまで保持する投影 adapter。
    PbrMaterialAssetData data{};
};

struct ModelMaterialBindingAssetDescriptor final : AssetDescriptor {
    ASSETS::MODELS::ModelMaterialBindingAssetData data{};
};

// VFX の描画資源（貼図・材質）を最初のフレームで先に用意するための、放出器ごとの材質の参照。
// VFX プログラムの中身は Assets のもので、読み込む側が写して渡し、描画側はこれだけを読む。
struct VfxPrewarmMaterial {
    RENDER3D::PARTICLES::ParticleMaterialMode materialMode = RENDER3D::PARTICLES::ParticleMaterialMode::StandardUnlit;
    std::string colorTextureAssetGuid{};
    std::string materialAssetGuid{};
    std::shared_ptr<const MaterialAssetDescriptor> effectMaterial{};
    std::string effectMaterialKey{};
};

struct VfxAssetDescriptor final : AssetDescriptor {
    std::shared_ptr<const VFX::VfxProgram> program{};
    std::vector<VfxPrewarmMaterial> prewarm{};
};

struct ShaderAssetDescriptor final : AssetDescriptor {
    std::shared_ptr<const ASSETS::SHADER::ShaderCookedArtifact> program{};
};

} // namespace HIKARI
