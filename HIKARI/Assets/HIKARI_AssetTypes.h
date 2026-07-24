#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"

namespace HIKARI {

    enum class AssetType {
        Model,
        Scene,
        Sky,
        Texture,
        Material,
        Animation,
        Particle,
        VfxEffect,
        Sequence,
        AnimationStateMachine,
        Unknown
    };

    enum class CoordinateSystem {
        RightHanded_YUp,
        LeftHanded_YUp,
    };

    enum class ModelImporterKind {
        Builtin,
        Gltf,
        Assimp,
    };

    enum class NormalImportPolicy {
        Require,
        IfMissing,
        Always,
    };

    enum class TangentImportPolicy {
        None,
        IfMissing,
        Always,
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

    struct ModelImportOptions {
        float unitScale = 1.0f;
        CoordinateSystem coordinateSystem = CoordinateSystem::RightHanded_YUp;

        NormalImportPolicy generateNormals = NormalImportPolicy::IfMissing;
        TangentImportPolicy generateTangents = TangentImportPolicy::IfMissing;

        bool triangulate = true;
        bool flipUV = false;
        bool mergeMeshes = false;
        bool keepNodeHierarchy = true;

        bool loadMaterials = true;
        bool loadTextures = true;
        bool loadAnimations = true;
        bool loadSkins = true;

        ModelClusterCookOptions clusterGeometry{};
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

    struct AssetId {
        std::string value;

        bool operator==(const AssetId& rhs) const {
            return value == rhs.value;
        }

        bool operator!=(const AssetId& rhs) const {
            return !(*this == rhs);
        }
    };

    struct AssetDescriptor {
        virtual ~AssetDescriptor() = default;

        AssetId id{};
        AssetType type = AssetType::Unknown;
        std::string sourcePath{};
        uint32_t version = 2;
    };

    struct ModelAssetDescriptor final : AssetDescriptor {
        ModelImporterKind importer = ModelImporterKind::Gltf;
        bool preload = true;
        std::string clusteredGeometryPath{};
        std::string collisionGeometryPath{};
        ModelImportOptions importOptions{};
        std::vector<ModelMaterialOverrideDesc> materialOverrides{};
        ModelAnimationImportDesc animation{};
    };

    struct SkyAssetDescriptor final : AssetDescriptor {
        std::string meshAssetId{};
        std::string textureAssetId{};
        SkyMode preferredMode = SkyMode::Gradient;
        std::string irradiancePath{};
        std::string prefilteredPath{};
        std::string brdfLutPath{};
        uint32_t prefilteredMipCount = 1;
        bool hasIbl = false;
    };

    enum class TextureAssetDimension {
        Texture2D,
        TextureCube,
    };

    enum class TextureAssetColorSpace {
        Auto,
        Linear,
        Srgb,
    };

    enum class TextureUsage {
        Auto,
        BaseColor,
        Normal,
        MetallicRoughness,
        Occlusion,
        Emissive,
        Mask,
        UI,
        SkyCubemap,
        IblIrradiance,
        IblPrefiltered,
        BrdfLut,
    };

    enum class TextureCompression {
        Auto,
        None,
        BC1,
        BC3,
        BC4,
        BC5,
        BC6H,
        BC7,
    };

    enum class TextureMipPolicy {
        Auto,
        Generate,
        Preserve,
        None,
    };

    enum class CookedAssetFormat {
        Unknown,
        DDS,
        HTEX,
        HMODEL,
        HMESH,
        HCMESH,
        HCOLLISION,
        HMAT,
        HSKY,
        HIBL,
        HPAK,
    };

    struct TextureAssetDescriptor final : AssetDescriptor {
        TextureAssetDimension dimension = TextureAssetDimension::Texture2D;
        TextureAssetColorSpace colorSpace = TextureAssetColorSpace::Auto;
        TextureUsage usage = TextureUsage::Auto;
        TextureCompression compression = TextureCompression::Auto;
        TextureMipPolicy mipPolicy = TextureMipPolicy::Auto;
    };

    struct MaterialAssetDescriptor final : AssetDescriptor {
        PbrMaterialAssetData data{};
    };

    struct VfxAssetDescriptor final : AssetDescriptor {
        bool preload = true;
        bool loopByDefault = false;
        float defaultScale = 1.0f;
    };

} // namespace HIKARI
