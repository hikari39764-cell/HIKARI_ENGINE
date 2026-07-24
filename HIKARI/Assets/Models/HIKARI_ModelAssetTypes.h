#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/HIKARI_Transform3D.h"

namespace HIKARI {

    enum class VertexLayoutKind {
        StaticPNTT,
        SkinnedPNTTJW,
    };

    enum class AlphaMode {
        Opaque,
        Mask,
        Blend,
    };

    struct Bounds {
        MATH::Vec3 min{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 max{ 0.0f, 0.0f, 0.0f };
    };

    struct TextureAsset3D {
        std::string name;
        std::string sourcePath;
        std::string resolvedPath;
    };

    struct TextureSlot {
        int textureIndex = -1;
        int texCoord = 0;
        MATH::Vec2 uvScale{ 1.0f, 1.0f };
        MATH::Vec2 uvOffset{ 0.0f, 0.0f };
        float uvRotation = 0.0f;
        float scale = 1.0f;
        float strength = 1.0f;
    };

    struct MaterialAsset {
        std::string name;
        MATH::Vec4 baseColorFactor{ 1, 1, 1, 1 };
        float metallicFactor = 0.0f;
        float roughnessFactor = 1.0f;
        float specularFactor = 1.0f;
        MATH::Vec3 specularColorFactor{ 1.0f, 1.0f, 1.0f };
        MATH::Vec3 emissiveFactor{ 0, 0, 0 };
        float emissiveStrength = 1.0f;
        TextureSlot baseColorTexture;
        TextureSlot normalTexture;
        TextureSlot metallicRoughnessTexture;
        TextureSlot occlusionTexture;
        TextureSlot emissiveTexture;
        TextureSlot specularTexture;
        TextureSlot specularColorTexture;
        AlphaMode alphaMode = AlphaMode::Opaque;
        float alphaCutoff = 0.5f;
        bool doubleSided = false;
        std::string shaderProfileId = "PBR";
        std::string defaultMaterialFxProfileId;
        uint32_t featureBits = 0;
    };

    struct Vertex3D {
        MATH::Vec3 position{};
        MATH::Vec3 normal{};
        MATH::Vec4 tangent{};
        MATH::Vec2 uv0{};
        MATH::Vec2 uv1{};
        MATH::Vec4 color0{ 1, 1, 1, 1 };
    };

    struct SkinnedVertex3D {
        MATH::Vec3 position{};
        MATH::Vec3 normal{};
        MATH::Vec4 tangent{};
        MATH::Vec2 uv0{};
        MATH::Vec2 uv1{};
        MATH::Vec4 color0{ 1, 1, 1, 1 };
        uint16_t joints[4]{};
        float weights[4]{};
    };

    struct MeshPrimitive {
        std::string name;
        VertexLayoutKind layout = VertexLayoutKind::StaticPNTT;
        std::vector<Vertex3D> staticVertices;
        std::vector<SkinnedVertex3D> skinnedVertices;
        std::vector<uint32_t> indices;
        uint32_t materialIndex = 0;
        Bounds bounds{};
        bool hasMorphTargets = false;
    };

    struct ModelImportDiagnostics {
        std::string sourceFormat;
        uint32_t objectCount = 0;
        uint32_t groupCount = 0;
        uint32_t triangulatedPolygonCount = 0;
        uint32_t missingNormalGeneratedCount = 0;
        uint32_t missingTangentGeneratedCount = 0;
        uint32_t unresolvedTextureCount = 0;
        uint32_t clusteredStaticPrimitiveCount = 0;
        uint32_t fallbackPrimitiveCount = 0;
        uint32_t skippedMorphPrimitiveCount = 0;
        uint32_t unsupportedPrimitiveModeCount = 0;
        uint32_t unsupportedFeatureCount = 0;
        std::vector<std::string> unsupportedExtensions{};
        std::vector<std::string> messages{};
    };

    struct MeshAsset {
        std::string name;
        std::vector<MeshPrimitive> primitives;
        Bounds bounds{};
    };

    struct ModelNode {
        std::string name;
        int parent = -1;
        std::vector<int> children;
        Transform3D localTransform;
        bool hasLocalMatrix = false;
        MATH::Mat4 localMatrix;
        MATH::Mat4 globalBindMatrix;
        int meshIndex = -1;
        int skinIndex = -1;
    };

} // namespace HIKARI
