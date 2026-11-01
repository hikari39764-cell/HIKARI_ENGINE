#include "Scene/Terrain/HIKARI_TerrainGrassModelFactory.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>

#include "Assets/Geometry/HIKARI_ClusteredGeometryCooker.h"
#include "RenderContracts/Geometry/HIKARI_HcmeshFormat.h"
#include "RenderContracts/Model/HIKARI_ModelAsset.h"
#include "RenderContracts/Model/HIKARI_ModelBounds.h"
#include "RenderContracts/Model/HIKARI_ModelMaterialSlot.h"
#include "Core/Hashing/HIKARI_FnvHash.h"
#include "Assets/Geometry/HIKARI_ProceduralModelFactory.h"
#include "Render3D/Resources/HIKARI_ClusterGeometryResourceSystem.h"

namespace HIKARI::TERRAIN {
namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = kPi * 2.0f;

struct VariantProfile {
    uint32_t bladeCount;
    float radius;
    float height;
    float width;
    float bend;
    float angleBias;
    float green;
};

constexpr std::array<VariantProfile, kTerrainGrassModelVariantCount> kVariantProfiles{{
    {5u, 0.135f, 0.94f, 0.92f, 0.075f, 0.10f, 0.92f},
    {6u, 0.165f, 1.02f, 1.00f, 0.105f, -0.06f, 1.00f},
    {7u, 0.190f, 0.88f, 0.86f, 0.120f, 0.16f, 0.84f},
    {8u, 0.220f, 1.08f, 0.96f, 0.145f, -0.14f, 0.96f},
}};

std::array<float, 3> VariantGreen(uint32_t variantIndex) noexcept {
    const VariantProfile& profile = kVariantProfiles[variantIndex % kTerrainGrassModelVariantCount];
    return {
        0.22f * profile.green,
        0.58f * profile.green,
        0.16f * (0.92f + 0.04f * static_cast<float>(variantIndex % 3u)),
    };
}

MATH::Vec3 CrossLocal(const MATH::Vec3& left, const MATH::Vec3& right) noexcept {
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

MATH::Vec3 NormalizeOrUp(const MATH::Vec3& value) noexcept {
    const float lengthSquared = value.x * value.x + value.y * value.y + value.z * value.z;
    if (!std::isfinite(lengthSquared) || lengthSquared <= 1.0e-8f) {
        return {0.0f, 1.0f, 0.0f};
    }
    const float inverseLength = 1.0f / std::sqrt(lengthSquared);
    return {
        value.x * inverseLength,
        value.y * inverseLength,
        value.z * inverseLength,
    };
}

Vertex3D MakeVertex(const MATH::Vec3& position, const MATH::Vec3& normal, const MATH::Vec3& tangent,
                    const MATH::Vec2& uv) noexcept {
    Vertex3D vertex{};
    vertex.position = position;
    vertex.normal = normal;
    vertex.tangent = {tangent.x, tangent.y, tangent.z, 1.0f};
    vertex.uv0 = uv;
    vertex.uv1 = {0.0f, 0.0f};
    vertex.color0 = {1.0f, 1.0f, 1.0f, 1.0f};
    return vertex;
}

void AppendRibbon(MeshPrimitive& primitive, float angle, float radius, float height, float width, float bend) {
    const MATH::Vec3 forward{std::cos(angle), 0.0f, std::sin(angle)};
    const MATH::Vec3 side{-std::sin(angle), 0.0f, std::cos(angle)};
    const MATH::Vec3 root{forward.x * radius, 0.0f, forward.z * radius};
    const MATH::Vec3 tipCenter{root.x + forward.x * bend, height, root.z + forward.z * bend};
    const float halfWidth = width * 0.5f;
    const float tipHalfWidth = width * 0.085f;
    const MATH::Vec3 bottomLeft{root.x - side.x * halfWidth, root.y, root.z - side.z * halfWidth};
    const MATH::Vec3 bottomRight{root.x + side.x * halfWidth, root.y, root.z + side.z * halfWidth};
    const MATH::Vec3 topLeft{tipCenter.x - side.x * tipHalfWidth, tipCenter.y, tipCenter.z - side.z * tipHalfWidth};
    const MATH::Vec3 topRight{tipCenter.x + side.x * tipHalfWidth, tipCenter.y, tipCenter.z + side.z * tipHalfWidth};
    const MATH::Vec3 normal = NormalizeOrUp(CrossLocal(bottomRight - bottomLeft, topLeft - bottomLeft));
    const MATH::Vec3 tangent = NormalizeOrUp(side);
    const uint32_t base = static_cast<uint32_t>(primitive.staticVertices.size());
    primitive.staticVertices.push_back(MakeVertex(bottomLeft, normal, tangent, {0.0f, 0.0f}));
    primitive.staticVertices.push_back(MakeVertex(bottomRight, normal, tangent, {1.0f, 0.0f}));
    primitive.staticVertices.push_back(MakeVertex(topLeft, normal, tangent, {0.0f, 1.0f}));
    primitive.staticVertices.push_back(MakeVertex(topRight, normal, tangent, {1.0f, 1.0f}));
    primitive.indices.insert(primitive.indices.end(), {
                                                          base,
                                                          base + 1u,
                                                          base + 2u,
                                                          base + 2u,
                                                          base + 1u,
                                                          base + 3u,
                                                      });
}

std::unique_ptr<ModelAsset> BuildModel(uint32_t variantIndex) {
    const uint32_t variant = variantIndex % kTerrainGrassModelVariantCount;
    const VariantProfile& profile = kVariantProfiles[variant];
    auto model = std::make_unique<ModelAsset>();
    const std::string modelId = "terrain-grass-variant-" + std::to_string(variant);
    model->SetName(modelId);
    model->SetSourcePath("generated://terrain/grass/" + modelId);
    model->SetState(ModelAsset::State::Loaded);

    MaterialAsset material{};
    material.name = "Terrain Grass";
    material.slotId = ASSETS::MODELS::MakeMaterialSlotId(ASSETS::MODELS::MaterialSlotImporterSemantic::Procedural,
                                                         material.name, variant);
    const std::array<float, 3> green = VariantGreen(variant);
    material.baseColorFactor = {green[0], green[1], green[2], 1.0f};
    material.metallicFactor = 0.0f;
    material.roughnessFactor = 0.94f;
    material.alphaMode = AlphaMode::Mask;
    material.alphaCutoff = 0.15f;
    material.doubleSided = true;
    material.shaderProfileId = "PBR";
    material.featureBits = MATERIAL_FEATURES::AlphaMask | MATERIAL_FEATURES::ThinTransparentSurface;
    model->materials.push_back(std::move(material));

    MeshPrimitive primitive{};
    primitive.name = "Terrain Grass Clump";
    primitive.layout = VertexLayoutKind::StaticPNTT;
    primitive.materialIndex = 0u;
    primitive.staticVertices.reserve(static_cast<std::size_t>(profile.bladeCount) * 8u);
    primitive.indices.reserve(static_cast<std::size_t>(profile.bladeCount) * 12u);

    for (uint32_t blade = 0u; blade < profile.bladeCount; ++blade) {
        const float ordinal = static_cast<float>(blade);
        const float angle = profile.angleBias + kTwoPi * ordinal / static_cast<float>(profile.bladeCount) +
                            0.045f * static_cast<float>((blade + variant) % 3u);
        const float radius = profile.radius * (0.72f + 0.10f * static_cast<float>((blade * 3u + variant) % 5u));
        const float height = profile.height * (0.87f + 0.075f * static_cast<float>((blade + variant) % 4u));
        // Reference blade width matches TerrainGrassSettings' default
        // 0.028 m. The clump radius remains art-directed separately.
        const float width = 0.028f * profile.width * (0.90f + 0.055f * static_cast<float>((blade * 2u + variant) % 4u));
        const float bend = profile.bend * (0.82f + 0.08f * static_cast<float>((blade + variant) % 3u));

        // Crossed ribbons give each clump volume without making each
        // blade a scene object. Each blade contributes four triangles.
        AppendRibbon(primitive, angle, radius, height, width, bend);
        AppendRibbon(primitive, angle + kPi * 0.5f, radius, height * 0.98f, width * 0.92f, bend * 0.88f);
    }

    MeshAsset mesh{};
    mesh.name = "Terrain Grass Clump Mesh";
    primitive.bounds = BOUNDS::ComputePrimitiveBounds(primitive);
    mesh.bounds = model->bounds = primitive.bounds;
    mesh.primitives.push_back(std::move(primitive));
    model->meshes.push_back(std::move(mesh));

    ModelNode node{};
    node.name = "Terrain Grass Root";
    node.meshIndex = 0;
    model->nodes.push_back(std::move(node));
    model->defaultSceneRootNode = 0;
    return model;
}

std::array<std::unique_ptr<ModelAsset>, kTerrainGrassModelVariantCount> gCache{};
std::array<std::string, kTerrainGrassModelVariantCount> gClusterPathCache{};
std::array<bool, kTerrainGrassModelVariantCount> gPackedSourceRegistered{};
std::mutex gCacheMutex{};

uint64_t HashGrassClusterKey(uint32_t variantIndex) noexcept {
    uint64_t hash = HASH::Fnv1aString("HIKARI.TerrainGrass.Cluster");
    hash = HASH::Fnv1aCombine(hash, kTerrainGrassClusterGeometryRevision);
    hash = HASH::Fnv1aCombine(hash, variantIndex % kTerrainGrassModelVariantCount);
    hash = HASH::Fnv1aCombine(hash, ASSETS::GEOMETRY::kHcmeshVersion);
    return hash;
}

std::string MakeGrassClusterGuid(uint32_t variantIndex) {
    const uint32_t variant = variantIndex % kTerrainGrassModelVariantCount;
    std::ostringstream stream{};
    stream << "terrain-grass-r" << kTerrainGrassClusterGeometryRevision << "-v" << variant << "-" << std::hex
           << std::setw(16) << std::setfill('0') << HashGrassClusterKey(variant);
    return stream.str();
}

ASSETS::GEOMETRY::ClusteredGeometryCookSettings MakeGrassClusterCookSettings() {
    ASSETS::GEOMETRY::ClusteredGeometryCookSettings settings{};
    settings.maxSurfaceLodCount = 1u;
    settings.buildSurfaceLods = false;
    settings.surfacePartitionPolicy = ASSETS::GEOMETRY::SurfacePartitionPolicy::Disabled;
    settings.partitionLargeStaticSurfaces = false;
    settings.minTrianglesPerCluster = 1u;
    settings.buildAdjacency = true;
    settings.buildNormalCone = true;
    settings.buildClusterPages = true;
    settings.buildPackedGeometry = true;
    settings.generateMissingNormals = true;
    settings.generateMissingTangents = true;
    return settings;
}

const ModelAsset* GetOrCreateModelUnlocked(uint32_t variantIndex) {
    const uint32_t variant = variantIndex % kTerrainGrassModelVariantCount;
    std::unique_ptr<ModelAsset>& model = gCache[variant];
    if (!model) {
        model = BuildModel(variant);
    }
    return model.get();
}

bool CookGrassClusteredGeometry(uint32_t variant, const std::filesystem::path& outputPath,
                                RENDER3D::CLUSTER::ClusterGeometryPackedBytes& outPacked) {
    const ModelAsset* model = GetOrCreateModelUnlocked(variant);
    return model != nullptr &&
           PROCEDURAL::CookGeneratedClusterGeometry(*model, MakeGrassClusterGuid(variant),
                                                    MakeGrassClusterCookSettings(), outputPath, outPacked);
}
} // namespace

const ModelAsset* GetOrCreateTerrainGrassModel(uint32_t variantIndex) {
    std::scoped_lock lock(gCacheMutex);
    return GetOrCreateModelUnlocked(variantIndex);
}

std::string GetOrCreateTerrainGrassClusteredGeometryPath(uint32_t variantIndex,
                                                         const std::filesystem::path& projectRoot) {
    const uint32_t variant = variantIndex % kTerrainGrassModelVariantCount;
    const std::filesystem::path path =
        PROCEDURAL::ResolveGeneratedClusterGeometryPath(projectRoot, "TerrainGrass", MakeGrassClusterGuid(variant));
    const std::string normalizedPath = path.generic_string();
    std::scoped_lock lock(gCacheMutex);

    if (!gClusterPathCache[variant].empty() && gClusterPathCache[variant] != normalizedPath) {
        if (gPackedSourceRegistered[variant]) {
            RENDER3D::UnregisterClusterGeometryPackedSource(gClusterPathCache[variant]);
            gPackedSourceRegistered[variant] = false;
        }
        gClusterPathCache[variant].clear();
    }
    gClusterPathCache[variant] = normalizedPath;

    std::error_code existsError{};
    if (std::filesystem::exists(path, existsError) && !existsError) {
        return normalizedPath;
    }

    if (gPackedSourceRegistered[variant]) {
        RENDER3D::UnregisterClusterGeometryPackedSource(normalizedPath);
        gPackedSourceRegistered[variant] = false;
    }
    RENDER3D::CLUSTER::ClusterGeometryPackedBytes packed{};
    if (!CookGrassClusteredGeometry(variant, path, packed)) {
        gClusterPathCache[variant].clear();
        return {};
    }
    if (!packed.geometryBytes.empty()) {
        RENDER3D::RegisterClusterGeometryPackedSource(normalizedPath, std::move(packed));
        gPackedSourceRegistered[variant] = true;
    }
    return normalizedPath;
}

void ClearTerrainGrassModelCache() noexcept {
    std::scoped_lock lock(gCacheMutex);
    for (uint32_t variant = 0u; variant < kTerrainGrassModelVariantCount; ++variant) {
        if (gPackedSourceRegistered[variant] && !gClusterPathCache[variant].empty()) {
            RENDER3D::UnregisterClusterGeometryPackedSource(gClusterPathCache[variant]);
        }
        gPackedSourceRegistered[variant] = false;
        gClusterPathCache[variant].clear();
        gCache[variant].reset();
    }
}

} // namespace HIKARI::TERRAIN
