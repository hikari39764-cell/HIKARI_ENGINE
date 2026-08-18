#include "Render3D/Procedural/HIKARI_ProceduralModelFactory.h"

#include <algorithm>
#include <array>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "Assets/Geometry/HIKARI_ClusteredGeometryCooker.h"
#include "Assets/Geometry/HIKARI_HcmeshFormat.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Assets/Models/HIKARI_ModelMaterialSlot.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Hashing/HIKARI_FnvHash.h"
#include "Render3D/Procedural/HIKARI_ProceduralMeshBuilder.h"
#include "Render3D/Resources/HIKARI_ClusterGeometryResourceSystem.h"

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
            float uvTilesX = 0.0f;
            float uvTilesY = 0.0f;

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
                mix(std::hash<float>{}(key.uvTilesX));
                mix(std::hash<float>{}(key.uvTilesY));
                return seed;
            }
        };

        std::unordered_map<
            ProceduralModelKey,
            std::unique_ptr<ModelAsset>,
            KeyHasher> gCache;
        std::unordered_map<std::string, std::string> gClusterPathCache;
        ProceduralModelDebugStats gStats{};
        // 頂点属性の意味が変わった場合、旧 hcmesh を再利用しない。
        // v2: GridPlane の uv1 を正規化地形座標として生成する。
        // v3: response subdivision と独立した uv0 tile 数を含める。
        constexpr uint32_t kProceduralGeometryRevision = 3u;
        constexpr std::array<ProceduralMeshKind, 6>
            kDefaultWarmUpKinds{{
                ProceduralMeshKind::Box,
                ProceduralMeshKind::Plane,
                ProceduralMeshKind::GridPlane,
                ProceduralMeshKind::Sphere,
                ProceduralMeshKind::Cylinder,
                ProceduralMeshKind::Capsule,
            }};
        std::filesystem::path gWarmUpProjectRoot{};
        size_t gWarmUpIndex = 0u;
        uint64_t gWarmUpGeneration = 1u;
        std::unordered_set<std::string> gPendingClusterWarmUps{};

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
                settings.generateTangents,
                settings.uvTilesX,
                settings.uvTilesY
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
                key.generateTangents,
                key.uvTilesX,
                key.uvTilesY
            };
        }

        void HashAppendBytes(
            uint64_t& hash,
            const void* data,
            size_t size) {

            hash = HASH::Fnv1aBytes(hash, data, size);
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

            uint64_t hash = HASH::kFnv1a64OffsetBasis;
            HashAppendU32(hash, kProceduralGeometryRevision);
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
            HashAppendFloat(hash, key.uvTilesX);
            HashAppendFloat(hash, key.uvTilesY);
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

        bool CookClusteredGeometryFromModel(
            const ModelAsset& model,
            const ProceduralModelKey& key,
            const std::filesystem::path& outputPath,
            bool publishPackedSource,
            RENDER3D::CLUSTER::ClusterGeometryPackedBytes* outPacked) {

            RENDER3D::CLUSTER::ClusteredGeometryAsset clusteredGeometry{};
            RENDER3D::CLUSTER::ClusteredGeometryBuildReport report{};
            AssetGuid sourceGuid{};
            sourceGuid.value = MakeClusterGuidValue(key);
            if (!ASSETS::GEOMETRY::CookClusteredGeometryFromModel(
                    model,
                    sourceGuid,
                    MakeProceduralClusteredGeometryCookSettings(),
                    clusteredGeometry,
                    report)) {
                HIKARI_LOG_WARN(
                    "[ProceduralModelFactory] clustered geometry cook failed: " +
                    sourceGuid.value);
                return false;
            }

            // ディスク書き込みに先立って packed データをメモリ登録し、
            // 初回レンダリングがファイルを読まないようにする。
            RENDER3D::CLUSTER::ClusterGeometryPackedBytes packed{};
            std::string packMessage{};
            if (RENDER3D::CLUSTER::PackClusterGeometryForGpu(
                    clusteredGeometry,
                    {},
                    packed,
                    &packMessage)) {
                if (publishPackedSource) {
                    RENDER3D::RegisterClusterGeometryPackedSource(
                        outputPath.generic_string(),
                        std::move(packed));
                } else if (outPacked != nullptr) {
                    *outPacked = std::move(packed);
                }
            } else {
                HIKARI_LOG_WARN(
                    "[ProceduralModelFactory] clustered geometry pack failed: " +
                    packMessage);
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
            const ProceduralModelKey& key,
            bool recordStats) {

            auto model = std::make_unique<ModelAsset>();
            const std::string proceduralId = MakeClusterGuidValue(key);
            model->SetName(proceduralId);
            model->SetSourcePath("procedural://" + proceduralId);
            model->SetState(ModelAsset::State::Loaded);

            MaterialAsset material{};
            material.name = "Procedural Default";
            material.slotId = ASSETS::MODELS::MakeMaterialSlotId(
                ASSETS::MODELS::MaterialSlotImporterSemantic::Procedural,
                material.name,
                0u);
            material.shaderProfileId = "PBR";
            material.doubleSided = key.doubleSided;
            material.baseColorFactor = { 1, 1, 1, 1 };

            // プロシージャル形状の既定マテリアルは無地の白。
            // チェッカー(デバッグ)は ProceduralMeshComponent の
            // マテリアル選択で明示的に選ぶ。
            model->materials.push_back(material);

            MeshPrimitive primitive{};
            primitive.name = "Procedural Primitive";
            primitive.layout = VertexLayoutKind::StaticPNTT;
            primitive.materialIndex = 0;
            BuildProceduralMesh(MakeSettings(key), primitive);

            MeshAsset mesh{};
            mesh.name = "Procedural Mesh";
            FinalizeBounds(primitive, mesh, *model);
            if (recordStats) {
                gStats.generatedVertexCount += primitive.staticVertices.size();
                gStats.generatedIndexCount += primitive.indices.size();
            }
            mesh.primitives.push_back(std::move(primitive));
            model->meshes.push_back(std::move(mesh));

            ModelNode node{};
            node.name = "Procedural Root";
            node.meshIndex = 0;
            model->nodes.push_back(node);
            model->defaultSceneRootNode = 0;
            if (recordStats) {
                ++gStats.generatedModelCount;
            }
            return model;
        }

        void RecordGeneratedModelStats(const ModelAsset& model) {
            ++gStats.generatedModelCount;
            for (const MeshAsset& mesh : model.meshes) {
                for (const MeshPrimitive& primitive : mesh.primitives) {
                    gStats.generatedVertexCount +=
                        primitive.staticVertices.size();
                    gStats.generatedIndexCount += primitive.indices.size();
                }
            }
        }

        struct ClusterWarmUpJob {
            ProceduralModelKey key{};
            std::filesystem::path path{};
            std::string normalizedPath{};
            uint64_t generation = 0u;
            bool cookClusteredGeometry = true;
        };

        struct ClusterWarmUpResult {
            ProceduralModelKey key{};
            std::filesystem::path path{};
            std::string normalizedPath{};
            uint64_t generation = 0u;
            std::unique_ptr<ModelAsset> model{};
            RENDER3D::CLUSTER::ClusterGeometryPackedBytes packed{};
            bool packedValid = false;
            bool success = false;
        };

        class ClusterWarmUpWorker final {
        public:
            ClusterWarmUpWorker()
                : thread_([this]() { Run(); }) {
            }

            ~ClusterWarmUpWorker() {
                {
                    std::scoped_lock lock(mutex_);
                    stop_ = true;
                    jobs_.clear();
                }
                condition_.notify_one();
                if (thread_.joinable()) {
                    thread_.join();
                }
            }

            void Enqueue(ClusterWarmUpJob job) {
                {
                    std::scoped_lock lock(mutex_);
                    jobs_.push_back(std::move(job));
                }
                condition_.notify_one();
            }

            void Drain(std::vector<ClusterWarmUpResult>& outResults) {
                std::scoped_lock lock(mutex_);
                while (!results_.empty()) {
                    outResults.push_back(std::move(results_.front()));
                    results_.pop_front();
                }
            }

        private:
            void Run() {
                for (;;) {
                    ClusterWarmUpJob job{};
                    {
                        std::unique_lock lock(mutex_);
                        condition_.wait(lock, [this]() {
                            return stop_ || !jobs_.empty();
                        });
                        if (stop_ && jobs_.empty()) {
                            return;
                        }
                        job = std::move(jobs_.front());
                        jobs_.pop_front();
                    }

                    ClusterWarmUpResult result{};
                    result.key = job.key;
                    result.path = job.path;
                    result.normalizedPath = job.normalizedPath;
                    result.generation = job.generation;
                    result.model = BuildModel(job.key, false);
                    if (result.model != nullptr) {
                        if (job.cookClusteredGeometry) {
                            result.success = CookClusteredGeometryFromModel(
                                *result.model,
                                job.key,
                                job.path,
                                false,
                                &result.packed);
                            result.packedValid = result.success &&
                                (!result.packed.geometryBytes.empty() ||
                                    !result.packed.metadataBytes.empty());
                        } else {
                            result.success = true;
                        }
                    }
                    {
                        std::scoped_lock lock(mutex_);
                        results_.push_back(std::move(result));
                    }
                }
            }

            std::mutex mutex_{};
            std::condition_variable condition_{};
            std::deque<ClusterWarmUpJob> jobs_{};
            std::deque<ClusterWarmUpResult> results_{};
            bool stop_ = false;
            std::thread thread_{};
        };

        std::unique_ptr<ClusterWarmUpWorker>&
            GetClusterWarmUpWorkerStorage() {

            static std::unique_ptr<ClusterWarmUpWorker> worker{};
            return worker;
        }

        ClusterWarmUpWorker& GetClusterWarmUpWorker() {
            auto& worker = GetClusterWarmUpWorkerStorage();
            if (!worker) {
                worker = std::make_unique<ClusterWarmUpWorker>();
            }
            return *worker;
        }

        bool CookClusteredGeometry(
            const ProceduralMeshSettings& settings,
            const ProceduralModelKey& key,
            const std::filesystem::path& outputPath) {

            const ModelAsset* model = GetOrCreateModel(settings);
            return model != nullptr && CookClusteredGeometryFromModel(
                *model,
                key,
                outputPath,
                true,
                nullptr);
        }
    }

    const ModelAsset* GetOrCreateModel(
        const ProceduralMeshSettings& settings) {

        PumpClusteredGeometryWarmUps();
        const ProceduralModelKey key = MakeKey(settings);
        const auto found = gCache.find(key);
        if (found != gCache.end()) {
            ++gStats.cacheHitCount;
            return found->second.get();
        }
        ++gStats.cacheMissCount;
        auto model = BuildModel(key, true);
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

    void RequestClusteredGeometryWarmUp(
        const ProceduralMeshSettings& settings,
        const std::filesystem::path& projectRoot) {

        PumpClusteredGeometryWarmUps();
        if (projectRoot.empty()) {
            return;
        }

        const ProceduralModelKey key = MakeKey(settings);
        const std::filesystem::path path =
            ResolveClusterCachePath(key, projectRoot);
        const std::string normalizedPath = path.generic_string();
        std::error_code existsError{};
        const bool pathExists =
            std::filesystem::exists(path, existsError) && !existsError;
        const bool modelReady = gCache.find(key) != gCache.end();
        if (modelReady &&
            (pathExists ||
                gClusterPathCache.find(normalizedPath) !=
                    gClusterPathCache.end())) {
            gClusterPathCache.emplace(normalizedPath, normalizedPath);
            return;
        }
        if (!gPendingClusterWarmUps.emplace(normalizedPath).second) {
            return;
        }

        GetClusterWarmUpWorker().Enqueue(ClusterWarmUpJob{
            key,
            path,
            normalizedPath,
            gWarmUpGeneration,
            !pathExists,
        });
    }

    bool TryGetWarmedClusteredGeometry(
        const ProceduralMeshSettings& settings,
        const std::filesystem::path& projectRoot,
        const ModelAsset*& outModel,
        std::string& outPath) {

        outModel = nullptr;
        outPath.clear();
        PumpClusteredGeometryWarmUps();
        if (projectRoot.empty()) {
            return false;
        }

        const ProceduralModelKey key = MakeKey(settings);
        const auto model = gCache.find(key);
        if (model == gCache.end() || model->second == nullptr) {
            return false;
        }

        const std::filesystem::path path =
            ResolveClusterCachePath(key, projectRoot);
        const std::string normalizedPath = path.generic_string();
        const auto cachedPath = gClusterPathCache.find(normalizedPath);
        if (cachedPath == gClusterPathCache.end()) {
            std::error_code existsError{};
            if (!std::filesystem::exists(path, existsError) || existsError) {
                return false;
            }
            gClusterPathCache.emplace(normalizedPath, normalizedPath);
        }
        outModel = model->second.get();
        outPath = normalizedPath;
        return true;
    }

    void PumpClusteredGeometryWarmUps() {
        std::vector<ClusterWarmUpResult> results{};
        auto& worker = GetClusterWarmUpWorkerStorage();
        if (!worker) {
            return;
        }
        worker->Drain(results);
        for (ClusterWarmUpResult& result : results) {
            gPendingClusterWarmUps.erase(result.normalizedPath);
            if (result.generation != gWarmUpGeneration) {
                continue;
            }
            if (!result.success || result.model == nullptr) {
                HIKARI_LOG_WARN(
                    "[ProceduralModelFactory] background clustered geometry warm-up failed: " +
                    result.normalizedPath);
                continue;
            }

            if (gCache.find(result.key) == gCache.end()) {
                RecordGeneratedModelStats(*result.model);
                gCache.emplace(result.key, std::move(result.model));
            }
            if (result.packedValid) {
                RENDER3D::RegisterClusterGeometryPackedSource(
                    result.normalizedPath,
                    std::move(result.packed));
            }
            gClusterPathCache[result.normalizedPath] =
                result.normalizedPath;
            HIKARI_LOG_INFO(
                "[ProceduralModelFactory] background clustered geometry ready: " +
                result.normalizedPath);
        }
    }

    void WarmUpDefaultPrimitiveClusters(
        const std::filesystem::path& projectRoot) {

        if (projectRoot.empty()) {
            return;
        }
        const std::filesystem::path normalized =
            projectRoot.lexically_normal();
        if (gWarmUpProjectRoot != normalized) {
            gWarmUpProjectRoot = normalized;
            gWarmUpIndex = 0u;
        }
        if (gWarmUpIndex >= kDefaultWarmUpKinds.size()) {
            return;
        }

        ProceduralMeshSettings settings{};
        settings.kind = kDefaultWarmUpKinds[gWarmUpIndex];
        settings = SanitizeProceduralMeshSettings(settings);
        (void)GetOrCreateClusteredGeometryPath(
            settings,
            projectRoot);
        ++gWarmUpIndex;
    }

    void ClearCache() {
        ++gWarmUpGeneration;
        gPendingClusterWarmUps.clear();
        gCache.clear();
        gClusterPathCache.clear();
        gStats = {};
        gWarmUpProjectRoot.clear();
        gWarmUpIndex = 0u;
    }

    const ProceduralModelDebugStats& GetDebugStats() {
        return gStats;
    }

} // namespace HIKARI::PROCEDURAL
