#include "Render3D/Procedural/HIKARI_ProceduralModelFactory.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>

#include "Assets/Geometry/HIKARI_ClusteredGeometryCooker.h"
#include "Assets/Geometry/HIKARI_HcmeshFormat.h"
#include "Core/HIKARI_Logger.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Render3D/Procedural/HIKARI_ProceduralMeshBuilder.h"

namespace HIKARI::PROCEDURAL {
    namespace {
        struct ProceduralModelKey {
            ProceduralMeshKind kind = ProceduralMeshKind::Box;
            float width = 1.0f;
            float height = 1.0f;
            float depth = 1.0f;
            uint32_t segmentsX = 10;
            uint32_t segmentsY = 10;
            uint32_t segmentsZ = 1;
            uint32_t sphereSlices = 32;
            uint32_t sphereStacks = 16;
            bool doubleSided = false;
            bool generateTangents = true;

            bool operator==(const ProceduralModelKey& rhs) const = default;
        };

        struct KeyHasher {
            size_t operator()(const ProceduralModelKey& key) const noexcept {
                size_t seed = static_cast<size_t>(key.kind);
                const auto mix = [&seed](size_t value) {
                    seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
                };
                mix(std::hash<float>{}(key.width));
                mix(std::hash<float>{}(key.height));
                mix(std::hash<float>{}(key.depth));
                mix(key.segmentsX);
                mix(key.segmentsY);
                mix(key.segmentsZ);
                mix(key.sphereSlices);
                mix(key.sphereStacks);
                mix(key.doubleSided ? 1u : 0u);
                mix(key.generateTangents ? 1u : 0u);
                return seed;
            }
        };

        std::unordered_map<
            ProceduralModelKey,
            std::unique_ptr<ModelAsset>,
            KeyHasher> gCache;
        std::unordered_map<std::string, std::string> gClusterPathCache;
        ProceduralModelDebugStats gStats{};

        constexpr uint64_t kFnv64OffsetBasis = 14695981039346656037ull;
        constexpr uint64_t kFnv64Prime = 1099511628211ull;

        ProceduralModelKey MakeKey(
            const ProceduralMeshSettings& rawSettings) {

            const ProceduralMeshSettings settings =
                SanitizeProceduralMeshSettings(rawSettings);
            return {
                settings.kind,
                settings.width,
                settings.height,
                settings.depth,
                settings.segmentsX,
                settings.segmentsY,
                settings.segmentsZ,
                settings.sphereSlices,
                settings.sphereStacks,
                settings.doubleSided,
                settings.generateTangents
            };
        }

        ProceduralMeshSettings MakeSettings(
            const ProceduralModelKey& key) {
            return {
                key.kind,
                key.width,
                key.height,
                key.depth,
                key.segmentsX,
                key.segmentsY,
                key.segmentsZ,
                key.sphereSlices,
                key.sphereStacks,
                key.doubleSided,
                key.generateTangents
            };
        }

        void HashAppendBytes(
            uint64_t& hash,
            const void* data,
            size_t size) {

            const auto* bytes = static_cast<const uint8_t*>(data);
            for (size_t i = 0; i < size; ++i) {
                hash ^= static_cast<uint64_t>(bytes[i]);
                hash *= kFnv64Prime;
            }
        }

        void HashAppendU32(uint64_t& hash, uint32_t value) {
            HashAppendBytes(hash, &value, sizeof(value));
        }

        void HashAppendFloat(uint64_t& hash, float value) {
            uint32_t bits = 0;
            static_assert(sizeof(bits) == sizeof(value));
            std::memcpy(&bits, &value, sizeof(bits));
            HashAppendU32(hash, bits);
        }

        uint64_t HashProceduralModelKey(
            const ProceduralModelKey& key) {

            uint64_t hash = kFnv64OffsetBasis;
            HashAppendU32(hash, static_cast<uint32_t>(key.kind));
            HashAppendFloat(hash, key.width);
            HashAppendFloat(hash, key.height);
            HashAppendFloat(hash, key.depth);
            HashAppendU32(hash, key.segmentsX);
            HashAppendU32(hash, key.segmentsY);
            HashAppendU32(hash, key.segmentsZ);
            HashAppendU32(hash, key.sphereSlices);
            HashAppendU32(hash, key.sphereStacks);
            HashAppendU32(hash, key.doubleSided ? 1u : 0u);
            HashAppendU32(hash, key.generateTangents ? 1u : 0u);
            return hash;
        }

        std::string MakeClusterGuidValue(
            const ProceduralModelKey& key) {

            std::ostringstream oss;
            oss << "procedural-"
                << std::hex
                << std::setw(16)
                << std::setfill('0')
                << HashProceduralModelKey(key);
            return oss.str();
        }

        std::filesystem::path ResolveClusterCachePath(
            const ProceduralModelKey& key,
            const std::filesystem::path& projectRoot) {

            const std::filesystem::path libraryRoot = projectRoot.empty()
                ? std::filesystem::path("Library")
                : projectRoot / "Library";
            const std::string fileName =
                MakeClusterGuidValue(key) +
                "-hcmesh" +
                std::to_string(ASSETS::GEOMETRY::kHcmeshVersion) +
                ".hcmesh";
            return (libraryRoot / "Generated" /
                "ProceduralCluster" / fileName).lexically_normal();
        }

        ASSETS::GEOMETRY::ClusteredGeometryCookSettings
            MakeProceduralClusteredGeometryCookSettings() {

            ASSETS::GEOMETRY::ClusteredGeometryCookSettings settings{};
            settings.maxSurfaceLodCount = 1u;
            settings.buildSurfaceLods = false;
            settings.partitionLargeStaticSurfaces = true;
            settings.surfacePartitionPolicy =
                ASSETS::GEOMETRY::SurfacePartitionPolicy::SceneStatic;
            return settings;
        }

        bool CookClusteredGeometry(
            const ProceduralMeshSettings& settings,
            const ProceduralModelKey& key,
            const std::filesystem::path& outputPath) {

            const ModelAsset* model = GetOrCreateModel(settings);
            if (model == nullptr) {
                HIKARI_LOG_WARN(
                    "[ProceduralModelFactory] failed to build procedural model for clustered geometry.");
                return false;
            }

            RENDER3D::CLUSTER::ClusteredGeometryAsset clusteredGeometry{};
            RENDER3D::CLUSTER::ClusteredGeometryBuildReport report{};
            AssetGuid sourceGuid{};
            sourceGuid.value = MakeClusterGuidValue(key);
            if (!ASSETS::GEOMETRY::CookClusteredGeometryFromModel(
                    *model,
                    sourceGuid,
                    MakeProceduralClusteredGeometryCookSettings(),
                    clusteredGeometry,
                    report)) {
                HIKARI_LOG_WARN(
                    "[ProceduralModelFactory] clustered geometry cook failed: " +
                    sourceGuid.value);
                return false;
            }

            std::string message;
            if (!ASSETS::GEOMETRY::WriteHcmeshFile(
                    outputPath,
                    clusteredGeometry,
                    message)) {
                HIKARI_LOG_WARN(
                    "[ProceduralModelFactory] clustered geometry write failed: " +
                    message);
                return false;
            }
            HIKARI_LOG_INFO(
                "[ProceduralModelFactory] cooked clustered geometry: " +
                outputPath.generic_string());
            return true;
        }

        void ExpandBounds(
            Bounds& bounds,
            const MATH::Vec3& position,
            bool& initialized) {

            if (!initialized) {
                bounds.min = position;
                bounds.max = position;
                initialized = true;
                return;
            }
            bounds.min.x = (std::min)(bounds.min.x, position.x);
            bounds.min.y = (std::min)(bounds.min.y, position.y);
            bounds.min.z = (std::min)(bounds.min.z, position.z);
            bounds.max.x = (std::max)(bounds.max.x, position.x);
            bounds.max.y = (std::max)(bounds.max.y, position.y);
            bounds.max.z = (std::max)(bounds.max.z, position.z);
        }

        void FinalizeBounds(
            MeshPrimitive& primitive,
            MeshAsset& mesh,
            ModelAsset& model) {

            bool initialized = false;
            Bounds bounds{};
            for (const Vertex3D& vertex : primitive.staticVertices) {
                ExpandBounds(bounds, vertex.position, initialized);
            }
            primitive.bounds = initialized ? bounds : Bounds{};
            mesh.bounds = primitive.bounds;
            model.bounds = primitive.bounds;
        }

        std::unique_ptr<ModelAsset> BuildModel(
            const ProceduralModelKey& key) {

            auto model = std::make_unique<ModelAsset>();
            const std::string proceduralId = MakeClusterGuidValue(key);
            model->SetName(proceduralId);
            model->SetSourcePath("procedural://" + proceduralId);
            model->SetState(ModelAsset::State::Loaded);

            MaterialAsset material{};
            material.name = "Procedural Default";
            material.shaderProfileId = "PBR";
            material.doubleSided = key.doubleSided;
            material.baseColorFactor = { 1, 1, 1, 1 };
            model->materials.push_back(material);

            MeshPrimitive primitive{};
            primitive.name = "Procedural Primitive";
            primitive.layout = VertexLayoutKind::StaticPNTT;
            primitive.materialIndex = 0;
            BuildProceduralMesh(MakeSettings(key), primitive);

            MeshAsset mesh{};
            mesh.name = "Procedural Mesh";
            FinalizeBounds(primitive, mesh, *model);
            gStats.generatedVertexCount += primitive.staticVertices.size();
            gStats.generatedIndexCount += primitive.indices.size();
            mesh.primitives.push_back(std::move(primitive));
            model->meshes.push_back(std::move(mesh));

            ModelNode node{};
            node.name = "Procedural Root";
            node.meshIndex = 0;
            model->nodes.push_back(node);
            model->defaultSceneRootNode = 0;
            ++gStats.generatedModelCount;
            return model;
        }
    }

    const ModelAsset* GetOrCreateModel(
        const ProceduralMeshSettings& settings) {

        const ProceduralModelKey key = MakeKey(settings);
        const auto found = gCache.find(key);
        if (found != gCache.end()) {
            ++gStats.cacheHitCount;
            return found->second.get();
        }
        ++gStats.cacheMissCount;
        auto model = BuildModel(key);
        const ModelAsset* raw = model.get();
        gCache.emplace(key, std::move(model));
        return raw;
    }

    std::string GetOrCreateClusteredGeometryPath(
        const ProceduralMeshSettings& settings,
        const std::filesystem::path& projectRoot) {

        const ProceduralModelKey key = MakeKey(settings);
        const std::filesystem::path path =
            ResolveClusterCachePath(key, projectRoot);
        const std::string normalizedPath = path.generic_string();
        const auto cached = gClusterPathCache.find(normalizedPath);
        if (cached != gClusterPathCache.end()) {
            return cached->second;
        }

        std::error_code existsError{};
        if (std::filesystem::exists(path, existsError) && !existsError) {
            gClusterPathCache.emplace(normalizedPath, normalizedPath);
            return normalizedPath;
        }
        if (!CookClusteredGeometry(settings, key, path)) {
            return {};
        }
        gClusterPathCache.emplace(normalizedPath, normalizedPath);
        return normalizedPath;
    }

    void ClearCache() {
        gCache.clear();
        gClusterPathCache.clear();
        gStats = {};
    }

    const ProceduralModelDebugStats& GetDebugStats() {
        return gStats;
    }

} // namespace HIKARI::PROCEDURAL
