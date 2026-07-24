#include "Assets/Importers/Policy/HIKARI_ModelImportPolicy.h"

#include <algorithm>
#include <string>

#include <json.hpp>

#include "Core/Text/HIKARI_AsciiCase.h"

namespace HIKARI::ASSETS::IMPORT_POLICY {

    namespace {

        const nlohmann::json& ClusterSettingsOrEmpty(
            const nlohmann::json& settings) {

            static const nlohmann::json empty =
                nlohmann::json::object();
            if (settings.contains("clusterGeometry") &&
                settings["clusterGeometry"].is_object()) {
                return settings["clusterGeometry"];
            }
            return empty;
        }

        ModelGeometryCookProfile ParseGeometryCookProfile(
            const nlohmann::json& settings) {

            const nlohmann::json& cluster =
                ClusterSettingsOrEmpty(settings);
            const std::string value = cluster.value(
                "profile",
                settings.value(
                    "geometryProfile",
                    settings.value("clusterGeometryProfile", "Scene")));
            return value == "Character" || value == "character"
                ? ModelGeometryCookProfile::Character
                : ModelGeometryCookProfile::Scene;
        }

        bool ReadClusterBool(
            const nlohmann::json& settings,
            const nlohmann::json& cluster,
            const char* key,
            bool fallback) {

            return cluster.contains(key)
                ? cluster.value(key, fallback)
                : settings.value(key, fallback);
        }

        uint32_t ReadClusterUint(
            const nlohmann::json& settings,
            const nlohmann::json& cluster,
            const char* key,
            uint32_t fallback,
            uint32_t minimum,
            uint32_t maximum) {

            const uint32_t value = cluster.contains(key)
                ? cluster.value(key, fallback)
                : settings.value(key, fallback);
            return (std::max)(minimum, (std::min)(value, maximum));
        }

        float ReadClusterFloat(
            const nlohmann::json& settings,
            const nlohmann::json& cluster,
            const char* key,
            float fallback,
            float minimum,
            float maximum) {

            const float value = cluster.contains(key)
                ? cluster.value(key, fallback)
                : settings.value(key, fallback);
            return (std::max)(minimum, (std::min)(value, maximum));
        }

        float ApplyLodQualityBiasToRatio(
            float ratio,
            float qualityBias) {

            const float safeBias =
                (std::max)(0.50f, (std::min)(qualityBias, 4.0f));
            const float reduction = (1.0f - ratio) / safeBias;
            return (std::max)(
                0.05f,
                (std::min)(1.0f - reduction, 0.95f));
        }

        float ApplyLodQualityBiasToError(
            float error,
            float qualityBias) {

            const float safeBias =
                (std::max)(0.50f, (std::min)(qualityBias, 4.0f));
            return (std::max)(
                0.0001f,
                (std::min)(error / safeBias, 0.12f));
        }

        ModelClusterCookOptions BuildClusterOptions(
            const nlohmann::json& settings,
            ModelGeometryCookProfile profile) {

            const nlohmann::json& cluster =
                ClusterSettingsOrEmpty(settings);

            ModelClusterCookOptions options{};
            options.profile = profile;
            options.largeSurfaceTargetExtent =
                profile == ModelGeometryCookProfile::Character
                    ? 1.25f
                    : 3.0f;
            options.buildClusterGeometry = ReadClusterBool(
                settings,
                cluster,
                "enabled",
                true);
            options.maxLodCount = ReadClusterUint(
                settings,
                cluster,
                "maxLodCount",
                options.maxLodCount,
                1u,
                5u);
            options.lodQualityBias = ReadClusterFloat(
                settings,
                cluster,
                "lodQualityBias",
                options.lodQualityBias,
                0.50f,
                4.0f);
            options.partitionLargeSurfaces = ReadClusterBool(
                settings,
                cluster,
                "partitionLargeSurfaces",
                options.partitionLargeSurfaces);
            options.largeSurfaceTargetExtent = ReadClusterFloat(
                settings,
                cluster,
                "largeSurfaceTargetExtent",
                options.largeSurfaceTargetExtent,
                profile == ModelGeometryCookProfile::Character
                    ? 1.0f
                    : 2.0f,
                64.0f);
            options.lockPartitionBorders = ReadClusterBool(
                settings,
                cluster,
                "lockPartitionBorders",
                options.lockPartitionBorders);
            return options;
        }

        GEOMETRY::ClusteredGeometryCookSettings BuildClusteredGeometryCookSettings(
            const nlohmann::json& settings,
            ModelGeometryCookProfile profile) {

            const nlohmann::json& cluster =
                ClusterSettingsOrEmpty(settings);
            GEOMETRY::ClusteredGeometryCookSettings cook{};
            cook.maxTrianglesPerCluster = 64u;
            cook.maxVerticesPerCluster = 128u;
            cook.maxClustersPerPage = 64u;

            if (profile == ModelGeometryCookProfile::Character) {
                cook.maxSurfaceLodCount = 5u;
                cook.lod1TriangleRatio = 0.50f;
                cook.lod2TriangleRatio = 0.50f;
                cook.lod3TriangleRatio = 0.30f;
                cook.lod4TriangleRatio = 0.20f;
                cook.lod1TargetError = 0.004f;
                cook.lod2TargetError = 0.016f;
                cook.lod3TargetError = 0.045f;
                cook.lod4TargetError = 0.080f;
                cook.lod0MinScreenRadius = 0.12f;
                cook.lod1MinScreenRadius = 0.060f;
                cook.lod2MinScreenRadius = 0.040f;
                cook.lod3MinScreenRadius = 0.022f;
                cook.surfacePartitionPolicy =
                    GEOMETRY::SurfacePartitionPolicy::CharacterStatic;
                cook.partitionLargeStaticSurfaces = true;
                cook.largeSurfacePartitionMinTriangles = 192u;
                cook.largeSurfacePartitionMinTrianglesPerChunk = 96u;
                cook.largeSurfacePartitionMaxDepth = 5u;
                cook.largeSurfacePartitionMaxExtent = 1.25f;
                cook.lockPartitionBorders = true;
                cook.minPartitionClusterEstimate = 4u;
                cook.subdivideLargeStaticTriangles = false;
            } else {
                cook.maxSurfaceLodCount = 5u;
                cook.maxClustersPerPage = 64u;
                cook.maxTrianglesPerCluster = 64u;
                cook.maxVerticesPerCluster = 128u;
                cook.minTrianglesPerCluster = 32u;
                cook.lod1TriangleRatio = 0.30f;
                cook.lod2TriangleRatio = 0.30f;
                cook.lod3TriangleRatio = 0.14f;
                cook.lod4TriangleRatio = 0.10f;
                cook.lod1TargetError = 0.008f;
                cook.lod2TargetError = 0.024f;
                cook.lod3TargetError = 0.060f;
                cook.lod4TargetError = 0.080f;
                cook.lod0MinScreenRadius = 0.16f;
                cook.lod1MinScreenRadius = 0.095f;
                cook.lod2MinScreenRadius = 0.060f;
                cook.lod3MinScreenRadius = 0.034f;
                cook.surfacePartitionPolicy =
                    GEOMETRY::SurfacePartitionPolicy::SceneStatic;
                cook.partitionLargeStaticSurfaces = true;
                cook.largeSurfacePartitionMinTriangles = 384u;
                cook.largeSurfacePartitionMinTrianglesPerChunk = 256u;
                cook.largeSurfacePartitionMaxDepth = 5u;
                cook.largeSurfacePartitionMaxExtent = 4.0f;
                cook.lockPartitionBorders = true;
                cook.balancePlanarStaticSurfaces = true;
                cook.minPartitionClusterEstimate = 16u;
                cook.planarStaticSurfaceMinPartitionExtent = 5.0f;
                cook.planarStaticSurfaceMinTrianglesPerChunk = 1024u;
                cook.planarStaticSurfaceMaxDepth = 4u;
                cook.subdivideLargeStaticTriangles = false;
                cook.largeStaticTriangleMaxEdgeLength = 3.0f;
                cook.largeStaticTriangleMaxSubdivisions = 4u;
                cook.largeStaticTriangleMaxGeneratedTriangles = 16384u;
                cook.meshletConeWeight = 0.35f;
                cook.meshletSplitFactor = 1.0f;
                cook.compactUnderfilledClusterGroups = true;
                cook.minClusterOccupancyRatio = 0.75f;
                cook.maxNormalBucketClusterOverhead = 1.10f;
                cook.clusterMergeNormalMinDot = 0.20f;
                cook.normalBucketCoherentGroupMinDot = 0.35f;
                cook.normalBucketQualityBonusRatio = 0.15f;
            }

            const float qualityBias = ReadClusterFloat(
                settings,
                cluster,
                "lodQualityBias",
                1.0f,
                0.50f,
                4.0f);
            cook.maxSurfaceLodCount = ReadClusterUint(
                settings,
                cluster,
                "maxLodCount",
                cook.maxSurfaceLodCount,
                1u,
                5u);
            cook.partitionLargeStaticSurfaces = ReadClusterBool(
                settings,
                cluster,
                "partitionLargeSurfaces",
                cook.partitionLargeStaticSurfaces);
            cook.surfacePartitionPolicy =
                cook.partitionLargeStaticSurfaces
                    ? cook.surfacePartitionPolicy
                    : GEOMETRY::SurfacePartitionPolicy::Disabled;
            cook.largeSurfacePartitionMinTriangles = ReadClusterUint(
                settings,
                cluster,
                "partitionMinTriangles",
                cook.largeSurfacePartitionMinTriangles,
                32u,
                65536u);
            cook.largeSurfacePartitionMinTrianglesPerChunk =
                ReadClusterUint(
                    settings,
                    cluster,
                    "partitionMinTrianglesPerChunk",
                    cook.largeSurfacePartitionMinTrianglesPerChunk,
                    16u,
                    32768u);
            cook.minPartitionClusterEstimate = ReadClusterUint(
                settings,
                cluster,
                "partitionMinClusterEstimate",
                cook.minPartitionClusterEstimate,
                1u,
                128u);
            cook.largeSurfacePartitionMaxDepth = ReadClusterUint(
                settings,
                cluster,
                "partitionMaxDepth",
                cook.largeSurfacePartitionMaxDepth,
                1u,
                12u);
            cook.largeSurfacePartitionMaxExtent = ReadClusterFloat(
                settings,
                cluster,
                "largeSurfaceTargetExtent",
                cook.largeSurfacePartitionMaxExtent,
                profile == ModelGeometryCookProfile::Character
                    ? 1.0f
                    : 2.0f,
                64.0f);
            cook.lockPartitionBorders = ReadClusterBool(
                settings,
                cluster,
                "lockPartitionBorders",
                cook.lockPartitionBorders);
            cook.balancePlanarStaticSurfaces = ReadClusterBool(
                settings,
                cluster,
                "balancePlanarStaticSurfaces",
                cook.balancePlanarStaticSurfaces);
            cook.planarStaticSurfaceMinPartitionExtent = ReadClusterFloat(
                settings,
                cluster,
                "planarSurfaceMinPartitionExtent",
                cook.planarStaticSurfaceMinPartitionExtent,
                1.0f,
                64.0f);
            cook.planarStaticSurfaceMinTrianglesPerChunk =
                ReadClusterUint(
                    settings,
                    cluster,
                    "planarSurfaceMinTrianglesPerChunk",
                    cook.planarStaticSurfaceMinTrianglesPerChunk,
                    64u,
                    65536u);
            cook.planarStaticSurfaceMaxDepth = ReadClusterUint(
                settings,
                cluster,
                "planarSurfaceMaxDepth",
                cook.planarStaticSurfaceMaxDepth,
                1u,
                12u);
            const float defaultLargeTriangleEdge =
                profile == ModelGeometryCookProfile::Character
                    ? cook.largeStaticTriangleMaxEdgeLength
                    : (std::max)(
                        1.0f,
                        (std::min)(
                            cook.largeSurfacePartitionMaxExtent,
                            3.0f));
            cook.subdivideLargeStaticTriangles = ReadClusterBool(
                settings,
                cluster,
                "subdivideLargeTriangles",
                cook.subdivideLargeStaticTriangles);
            cook.largeStaticTriangleMaxEdgeLength = ReadClusterFloat(
                settings,
                cluster,
                "largeTriangleMaxEdgeLength",
                defaultLargeTriangleEdge,
                0.25f,
                32.0f);
            cook.largeStaticTriangleMaxSubdivisions = ReadClusterUint(
                settings,
                cluster,
                "largeTriangleMaxSubdivisions",
                cook.largeStaticTriangleMaxSubdivisions,
                1u,
                12u);
            cook.largeStaticTriangleMaxGeneratedTriangles =
                ReadClusterUint(
                    settings,
                    cluster,
                    "largeTriangleMaxGeneratedTriangles",
                    cook.largeStaticTriangleMaxGeneratedTriangles,
                    1024u,
                    1048576u);
            cook.maxTrianglesPerCluster = ReadClusterUint(
                settings,
                cluster,
                "maxTrianglesPerCluster",
                cook.maxTrianglesPerCluster,
                profile == ModelGeometryCookProfile::Scene ? 32u : 16u,
                RENDER3D::CLUSTER::kHcmeshMaxTrianglesPerCluster);
            cook.minTrianglesPerCluster = ReadClusterUint(
                settings,
                cluster,
                "minTrianglesPerCluster",
                cook.minTrianglesPerCluster,
                profile == ModelGeometryCookProfile::Scene ? 32u : 1u,
                cook.maxTrianglesPerCluster);
            cook.maxVerticesPerCluster = ReadClusterUint(
                settings,
                cluster,
                "maxVerticesPerCluster",
                cook.maxVerticesPerCluster,
                32u,
                RENDER3D::CLUSTER::kHcmeshMaxVerticesPerCluster);
            cook.meshletConeWeight = ReadClusterFloat(
                settings,
                cluster,
                "meshletConeWeight",
                cook.meshletConeWeight,
                0.0f,
                2.0f);
            cook.meshletSplitFactor = ReadClusterFloat(
                settings,
                cluster,
                "meshletSplitFactor",
                cook.meshletSplitFactor,
                0.0f,
                8.0f);
            cook.compactUnderfilledClusterGroups = ReadClusterBool(
                settings,
                cluster,
                "compactUnderfilledClusters",
                cook.compactUnderfilledClusterGroups);
            cook.minClusterOccupancyRatio = ReadClusterFloat(
                settings,
                cluster,
                "minClusterOccupancyRatio",
                cook.minClusterOccupancyRatio,
                0.25f,
                1.0f);
            cook.maxNormalBucketClusterOverhead = ReadClusterFloat(
                settings,
                cluster,
                "maxNormalBucketClusterOverhead",
                cook.maxNormalBucketClusterOverhead,
                1.0f,
                2.0f);
            cook.clusterMergeNormalMinDot = ReadClusterFloat(
                settings,
                cluster,
                "clusterMergeNormalMinDot",
                cook.clusterMergeNormalMinDot,
                -1.0f,
                0.99f);
            cook.normalBucketCoherentGroupMinDot = ReadClusterFloat(
                settings,
                cluster,
                "normalBucketCoherentGroupMinDot",
                cook.normalBucketCoherentGroupMinDot,
                -1.0f,
                0.99f);
            cook.normalBucketQualityBonusRatio = ReadClusterFloat(
                settings,
                cluster,
                "normalBucketQualityBonusRatio",
                cook.normalBucketQualityBonusRatio,
                0.0f,
                1.0f);

            cook.lod1TriangleRatio = ApplyLodQualityBiasToRatio(
                cook.lod1TriangleRatio,
                qualityBias);
            cook.lod2TriangleRatio = ApplyLodQualityBiasToRatio(
                cook.lod2TriangleRatio,
                qualityBias);
            cook.lod3TriangleRatio = ApplyLodQualityBiasToRatio(
                cook.lod3TriangleRatio,
                qualityBias);
            cook.lod4TriangleRatio = ApplyLodQualityBiasToRatio(
                cook.lod4TriangleRatio,
                qualityBias);
            cook.lod1TargetError = ApplyLodQualityBiasToError(
                cook.lod1TargetError,
                qualityBias);
            cook.lod2TargetError = ApplyLodQualityBiasToError(
                cook.lod2TargetError,
                qualityBias);
            cook.lod3TargetError = ApplyLodQualityBiasToError(
                cook.lod3TargetError,
                qualityBias);
            cook.lod4TargetError = ApplyLodQualityBiasToError(
                cook.lod4TargetError,
                qualityBias);
            return cook;
        }

        ModelImporterKind ResolveModelImporterKind(
            const std::filesystem::path& sourcePath) {

            const std::string extension =
                TEXT::ToLowerAsciiCopy(sourcePath.extension().string());
            return extension == ".obj" || extension == ".fbx"
                ? ModelImporterKind::Assimp
                : ModelImporterKind::Gltf;
        }

    } // namespace

    ModelImportPolicy ResolveModelImportPolicy(
        const std::filesystem::path& sourcePath,
        std::string_view importSettingsJson) {

        nlohmann::json settings = nlohmann::json::parse(
            importSettingsJson,
            nullptr,
            false);
        if (!settings.is_object()) {
            settings = nlohmann::json::object();
        }

        ModelImportPolicy policy{};
        policy.cookModel = settings.value("cookModel", true);
        policy.loadMaterials = settings.value("loadMaterials", true);
        policy.loadTextures = settings.value("loadTextures", true);
        policy.importer = ResolveModelImporterKind(sourcePath);

        const ModelGeometryCookProfile profile =
            ParseGeometryCookProfile(settings);
        policy.clusterOptions = BuildClusterOptions(settings, profile);
        policy.clusteredGeometryCookSettings =
            BuildClusteredGeometryCookSettings(settings, profile);
        return policy;
    }

    std::string MakeDefaultModelImportSettingsJson(
        const std::filesystem::path& sourcePath) {

        return nlohmann::json{
            { "sourceFormat", sourcePath.extension().string() },
            { "cookModel", true },
            { "outputFormat", "HMODEL" },
            { "meshFormat", "HCMESH" },
            { "loadMaterials", true },
            { "loadTextures", true },
            { "clusterGeometry", {
                { "enabled", true },
                { "profile", "Scene" },
                { "maxLodCount", 5 },
                { "lodQualityBias", 1.0f },
                { "partitionLargeSurfaces", true },
                { "largeSurfaceTargetExtent", 4.0f },
                { "lockPartitionBorders", true },
                { "balancePlanarStaticSurfaces", true },
                { "partitionMinClusterEstimate", 16 },
                { "planarSurfaceMinPartitionExtent", 5.0f },
                { "planarSurfaceMinTrianglesPerChunk", 1024 },
                { "planarSurfaceMaxDepth", 4 },
                { "subdivideLargeTriangles", false },
                { "largeTriangleMaxEdgeLength", 3.0f },
                { "largeTriangleMaxSubdivisions", 4 },
                { "largeTriangleMaxGeneratedTriangles", 16384 },
                { "meshletConeWeight", 0.35f },
                { "meshletSplitFactor", 1.0f },
                { "compactUnderfilledClusters", true },
                { "minClusterOccupancyRatio", 0.75f },
                { "maxNormalBucketClusterOverhead", 1.10f },
                { "clusterMergeNormalMinDot", 0.20f },
                { "normalBucketCoherentGroupMinDot", 0.35f },
                { "normalBucketQualityBonusRatio", 0.15f },
            } },
        }.dump(2);
    }

    std::string_view ToString(
        ModelGeometryCookProfile profile) noexcept {

        return profile == ModelGeometryCookProfile::Character
            ? "Character"
            : "Scene";
    }

    std::string_view ToString(
        GEOMETRY::SurfacePartitionPolicy policy) noexcept {

        switch (policy) {
        case GEOMETRY::SurfacePartitionPolicy::Disabled:
            return "Disabled";
        case GEOMETRY::SurfacePartitionPolicy::CharacterStatic:
            return "CharacterStatic";
        case GEOMETRY::SurfacePartitionPolicy::SceneStatic:
        default:
            return "SceneStatic";
        }
    }

} // namespace HIKARI::ASSETS::IMPORT_POLICY
