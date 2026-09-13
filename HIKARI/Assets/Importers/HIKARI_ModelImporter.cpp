#include "HIKARI_ModelImporter.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <unordered_set>
#include <utility>
#include <vector>

#include <json.hpp>

#include "Assets/Collision/HIKARI_ModelCollisionArtifact.h"
#include "Assets/Geometry/HIKARI_ClusteredGeometryCooker.h"
#include "Assets/Geometry/HIKARI_ClusteredGeometryValidator.h"
#include "Assets/Geometry/HIKARI_HcmeshFormat.h"
#include "Assets/Formats/HIKARI_HmodelFormat.h"
#include "Assets/Importers/Policy/HIKARI_ModelImportPolicy.h"
#include "Assets/Lighting/HIKARI_LightmapUvGeneration.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Assets/Models/Loading/HIKARI_ModelSourceLoader.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Assets/Tasks/HIKARI_AssetTaskService.h"
#include "Core/IO/HIKARI_FileReplacementTransaction.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"
#include "Project/Paths/HIKARI_ProjectPath.h"
#include "HIKARI_TextureImportBackend_DirectXTex.h"

namespace HIKARI {

    namespace JsonMath = SERIALIZATION::JSON::MATH;

    namespace {

        std::filesystem::path MakeSourceMetaPath(
            const std::filesystem::path& sourceMetaRoot,
            const std::filesystem::path& relativeSource) {

            if (sourceMetaRoot.empty() || relativeSource.empty()) {
                return {};
            }

            std::filesystem::path metaPath = sourceMetaRoot / relativeSource.parent_path();
            metaPath /= relativeSource.filename().string() + ".hikari.asset.json";
            return metaPath.lexically_normal();
        }

        std::filesystem::path MakeArtifactManifestPath(
            const std::filesystem::path& libraryRoot,
            const std::string& guid) {

            if (libraryRoot.empty() || guid.empty()) {
                return {};
            }
            return (libraryRoot / "AssetDatabase" / "Artifacts" / (guid + ".artifact.json")).lexically_normal();
        }

        const char* ToString(AlphaMode alphaMode) {
            switch (alphaMode) {
            case AlphaMode::Mask: return "Mask";
            case AlphaMode::Blend: return "Blend";
            case AlphaMode::Opaque:
            default: return "Opaque";
            }
        }

        void AppendGltfSourceBufferDependencies(
            const std::filesystem::path& absoluteSource,
            const std::filesystem::path& projectRoot,
            std::vector<AssetDependencyDesc>& dependencies) {

            nlohmann::json source{};
            if (!SERIALIZATION::JSON::ReadJsonFile(
                    absoluteSource,
                    source) ||
                !source.is_object() ||
                !source.contains("buffers") ||
                !source["buffers"].is_array()) {
                return;
            }

            std::unordered_set<std::string> uniquePaths{};
            for (const AssetDependencyDesc& dependency : dependencies) {
                if (!dependency.path.empty()) {
                    uniquePaths.insert(dependency.path);
                }
            }

            for (const nlohmann::json& buffer : source["buffers"]) {
                if (!buffer.is_object()) {
                    continue;
                }
                const std::string uri = buffer.value("uri", "");
                if (uri.empty() ||
                    uri.starts_with("data:") ||
                    uri.find("://") != std::string::npos) {
                    continue;
                }

                const std::filesystem::path uriPath{ uri };
                if (uriPath.is_absolute()) {
                    continue;
                }
                const std::filesystem::path absoluteDependency =
                    (absoluteSource.parent_path() / uriPath).
                        lexically_normal();
                const std::filesystem::path projectRelative =
                    absoluteDependency.lexically_relative(
                        projectRoot.lexically_normal());
                if (projectRelative.empty() ||
                    *projectRelative.begin() == "..") {
                    continue;
                }

                const std::string dependencyPath =
                    projectRelative.generic_string();
                if (!uniquePaths.insert(dependencyPath).second) {
                    continue;
                }
                AssetDependencyDesc dependency{};
                dependency.path = dependencyPath;
                dependency.role = "SourceBuffer";
                dependencies.push_back(std::move(dependency));
            }
        }

        struct TextureCookDiagnostic {
            int index = -1;
            std::string name{};
            std::string originalPath{};
            std::string cookedPath{};
            std::string guid{};
            bool htexReady = false;
            bool dependencyResolved = false;
            bool sourceHasMeaningfulAlpha = false;
            bool sourceHasTranslucentAlpha = false;
            bool sourceHasCutoutAlpha = false;
            float sourceAlphaNonOpaqueRatio = 0.0f;
            float sourceAlphaTranslucentRatio = 0.0f;
            float sourceAlphaCutoutRatio = 0.0f;
        };

        struct MaterialAlphaPolicyStats {
            uint32_t opaqueCount = 0;
            uint32_t maskCount = 0;
            uint32_t blendCount = 0;
            uint32_t normalizedToOpaqueCount = 0;
            uint32_t normalizedToMaskCount = 0;
            uint32_t normalizedToBlendCount = 0;
            uint32_t opticalOpacityFallbackCount = 0;
            uint32_t opaqueDoubleSidedPreservedCount = 0;
        };

        nlohmann::json SlotToJson(
            const TextureSlot& slot,
            const std::vector<TextureCookDiagnostic>& textureDiagnostics) {

            nlohmann::json json{
                { "textureIndex", slot.textureIndex },
                { "texCoord", slot.texCoord },
                { "uvScale", JsonMath::ToJsonArray(slot.uvScale) },
                { "uvOffset", JsonMath::ToJsonArray(slot.uvOffset) },
                { "uvRotation", slot.uvRotation },
                { "scale", slot.scale },
                { "strength", slot.strength },
            };

            if (slot.textureIndex >= 0 &&
                static_cast<size_t>(slot.textureIndex) < textureDiagnostics.size()) {
                const TextureCookDiagnostic& texture = textureDiagnostics[static_cast<size_t>(slot.textureIndex)];
                json["name"] = texture.name;
                json["sourcePath"] = texture.originalPath;
                json["runtimePath"] = texture.cookedPath;
                json["guid"] = texture.guid;
                json["htexReady"] = texture.htexReady;
                json["sourceHasMeaningfulAlpha"] = texture.sourceHasMeaningfulAlpha;
                json["sourceHasTranslucentAlpha"] = texture.sourceHasTranslucentAlpha;
                json["sourceHasCutoutAlpha"] = texture.sourceHasCutoutAlpha;
                json["sourceAlphaNonOpaqueRatio"] = texture.sourceAlphaNonOpaqueRatio;
                json["sourceAlphaTranslucentRatio"] = texture.sourceAlphaTranslucentRatio;
                json["sourceAlphaCutoutRatio"] = texture.sourceAlphaCutoutRatio;
            }
            return json;
        }

        nlohmann::json BuildModelDiagnostics(
            const ModelAsset& model,
            const std::vector<TextureCookDiagnostic>& textureDiagnostics,
            int htexReferenceCount,
            int fallbackTextureCount,
            const RENDER3D::CLUSTER::ClusteredGeometryBuildReport* clusteredReport,
            const ASSETS::GEOMETRY::ClusteredGeometryValidationResult* clusteredValidation,
            ModelGeometryCookProfile clusterProfile,
            const ASSETS::GEOMETRY::ClusteredGeometryCookSettings* clusterSettings,
            bool hcmeshReady,
            const std::string& hcmeshMessage,
            const MaterialAlphaPolicyStats& alphaPolicyStats) {

            int primitiveCount = 0;
            int staticVertexCount = 0;
            int skinnedVertexCount = 0;
            int indexCount = 0;
            for (const MeshAsset& mesh : model.meshes) {
                primitiveCount += static_cast<int>(mesh.primitives.size());
                for (const MeshPrimitive& primitive : mesh.primitives) {
                    staticVertexCount += static_cast<int>(primitive.staticVertices.size());
                    skinnedVertexCount += static_cast<int>(primitive.skinnedVertices.size());
                    indexCount += static_cast<int>(primitive.indices.size());
                }
            }

            nlohmann::json textures = nlohmann::json::array();
            for (const TextureCookDiagnostic& texture : textureDiagnostics) {
                textures.push_back({
                    { "index", texture.index },
                    { "name", texture.name },
                    { "sourcePath", texture.originalPath },
                    { "runtimePath", texture.cookedPath },
                    { "guid", texture.guid },
                    { "htexReady", texture.htexReady },
                    { "dependencyResolved", texture.dependencyResolved },
                    { "sourceHasMeaningfulAlpha", texture.sourceHasMeaningfulAlpha },
                    { "sourceHasTranslucentAlpha", texture.sourceHasTranslucentAlpha },
                    { "sourceHasCutoutAlpha", texture.sourceHasCutoutAlpha },
                    { "sourceAlphaNonOpaqueRatio", texture.sourceAlphaNonOpaqueRatio },
                    { "sourceAlphaTranslucentRatio", texture.sourceAlphaTranslucentRatio },
                    { "sourceAlphaCutoutRatio", texture.sourceAlphaCutoutRatio },
                });
            }

            nlohmann::json materials = nlohmann::json::array();
            for (const MaterialAsset& material : model.materials) {
                materials.push_back({
                    { "name", material.name },
                    { "baseColorFactor",
                        JsonMath::ToJsonArray(
                            material.baseColorFactor) },
                    { "metallicFactor", material.metallicFactor },
                    { "roughnessFactor", material.roughnessFactor },
                    { "specularFactor", material.specularFactor },
                    { "specularColorFactor",
                        JsonMath::ToJsonArray(
                            material.specularColorFactor) },
                    { "emissiveFactor",
                        JsonMath::ToJsonArray(
                            material.emissiveFactor) },
                    { "emissiveStrength", material.emissiveStrength },
                    { "alphaMode", ToString(material.alphaMode) },
                    { "doubleSided", material.doubleSided },
                    { "featureBits", material.featureBits },
                    { "slots", {
                        { "baseColor", SlotToJson(material.baseColorTexture, textureDiagnostics) },
                        { "normal", SlotToJson(material.normalTexture, textureDiagnostics) },
                        { "metallicRoughness", SlotToJson(material.metallicRoughnessTexture, textureDiagnostics) },
                        { "occlusion", SlotToJson(material.occlusionTexture, textureDiagnostics) },
                        { "emissive", SlotToJson(material.emissiveTexture, textureDiagnostics) },
                        { "specular", SlotToJson(material.specularTexture, textureDiagnostics) },
                        { "specularColor", SlotToJson(material.specularColorTexture, textureDiagnostics) },
                    } },
                });
            }

            nlohmann::json importMessages = nlohmann::json::array();
            for (const std::string& message : model.importDiagnostics.messages) {
                importMessages.push_back(message);
            }
            nlohmann::json unsupportedExtensions = nlohmann::json::array();
            for (const std::string& extension : model.importDiagnostics.unsupportedExtensions) {
                unsupportedExtensions.push_back(extension);
            }

            nlohmann::json formatReport = {
                { "sourceFormat", model.importDiagnostics.sourceFormat },
                { "objectCount", model.importDiagnostics.objectCount },
                { "groupCount", model.importDiagnostics.groupCount },
                { "triangulatedPolygonCount", model.importDiagnostics.triangulatedPolygonCount },
                { "missingNormalGeneratedCount", model.importDiagnostics.missingNormalGeneratedCount },
                { "missingTangentGeneratedCount", model.importDiagnostics.missingTangentGeneratedCount },
                { "unresolvedTextureCount", model.importDiagnostics.unresolvedTextureCount },
                { "unsupportedFeatureCount", model.importDiagnostics.unsupportedFeatureCount },
                { "unsupportedPrimitiveModeCount", model.importDiagnostics.unsupportedPrimitiveModeCount },
                { "unsupportedExtensions", unsupportedExtensions },
                { "skippedMorphPrimitiveCount", model.importDiagnostics.skippedMorphPrimitiveCount },
                { "clusteredStaticPrimitiveCount", model.importDiagnostics.clusteredStaticPrimitiveCount },
                { "fallbackPrimitiveCount", model.importDiagnostics.fallbackPrimitiveCount },
                { "lightmapUvAuthoredValidCount", model.importDiagnostics.lightmapUvAuthoredValidCount },
                { "lightmapUvMissingCount", model.importDiagnostics.lightmapUvMissingCount },
                { "lightmapUvInvalidCount", model.importDiagnostics.lightmapUvInvalidCount },
                { "lightmapUvGeneratedCount", model.importDiagnostics.lightmapUvGeneratedCount },
                { "lightmapUvGenerationFailedCount", model.importDiagnostics.lightmapUvGenerationFailedCount },
            };

            nlohmann::json alphaPolicyReport = {
                { "opaque", alphaPolicyStats.opaqueCount },
                { "mask", alphaPolicyStats.maskCount },
                { "blend", alphaPolicyStats.blendCount },
                { "normalizedToOpaque", alphaPolicyStats.normalizedToOpaqueCount },
                { "normalizedToMask", alphaPolicyStats.normalizedToMaskCount },
                { "normalizedToBlend", alphaPolicyStats.normalizedToBlendCount },
                { "opticalOpacityFallback", alphaPolicyStats.opticalOpacityFallbackCount },
                { "opaqueDoubleSidedPreserved", alphaPolicyStats.opaqueDoubleSidedPreservedCount },
            };

            nlohmann::json diagnostics{
                { "format", "HMODEL" },
                { "summary", {
                    { "nodes", model.nodes.size() },
                    { "meshes", model.meshes.size() },
                    { "primitives", primitiveCount },
                    { "materials", model.materials.size() },
                    { "textures", model.textures.size() },
                    { "skins", model.skins.size() },
                    { "animations", model.animations.size() },
                    { "staticVertices", staticVertexCount },
                    { "skinnedVertices", skinnedVertexCount },
                    { "indices", indexCount },
                    { "htexRefs", htexReferenceCount },
                    { "fallbackTextures", fallbackTextureCount },
                    { "objectCount", model.importDiagnostics.objectCount },
                    { "groupCount", model.importDiagnostics.groupCount },
                    { "triangulatedPolygonCount", model.importDiagnostics.triangulatedPolygonCount },
                    { "missingNormalGeneratedCount", model.importDiagnostics.missingNormalGeneratedCount },
                    { "missingTangentGeneratedCount", model.importDiagnostics.missingTangentGeneratedCount },
                    { "unresolvedTextureCount", model.importDiagnostics.unresolvedTextureCount },
                    { "clusteredStaticPrimitiveCount", model.importDiagnostics.clusteredStaticPrimitiveCount },
                    { "fallbackPrimitiveCount", model.importDiagnostics.fallbackPrimitiveCount },
                    { "skippedMorphPrimitives", model.importDiagnostics.skippedMorphPrimitiveCount },
                    { "unsupportedPrimitiveModes", model.importDiagnostics.unsupportedPrimitiveModeCount },
                    { "unsupportedFeatures", model.importDiagnostics.unsupportedFeatureCount },
                    { "lightmapUvAuthoredValid", model.importDiagnostics.lightmapUvAuthoredValidCount },
                    { "lightmapUvMissing", model.importDiagnostics.lightmapUvMissingCount },
                    { "lightmapUvInvalid", model.importDiagnostics.lightmapUvInvalidCount },
                    { "lightmapUvGenerated", model.importDiagnostics.lightmapUvGeneratedCount },
                    { "lightmapUvGenerationFailed", model.importDiagnostics.lightmapUvGenerationFailedCount },
                } },
                { "importMessages", std::move(importMessages) },
                { "formatReport", std::move(formatReport) },
                { "materialAlphaPolicy", std::move(alphaPolicyReport) },
                { "textures", std::move(textures) },
                { "materials", std::move(materials) },
            };

            nlohmann::json clusterJson{
                { "format", "HCMESH" },
                { "profile", ASSETS::IMPORT_POLICY::ToString(clusterProfile) },
                { "ready", hcmeshReady },
                { "message", hcmeshMessage },
            };
            if (clusterSettings != nullptr) {
                clusterJson["cookSettings"] = {
                    { "maxTrianglesPerCluster", clusterSettings->maxTrianglesPerCluster },
                    { "maxVerticesPerCluster", clusterSettings->maxVerticesPerCluster },
                    { "maxClustersPerPage", clusterSettings->maxClustersPerPage },
                    { "maxSurfaceLodCount", clusterSettings->maxSurfaceLodCount },
                    { "lodTriangleRatios", nlohmann::json::array({
                        clusterSettings->lod1TriangleRatio,
                        clusterSettings->lod2TriangleRatio,
                        clusterSettings->lod3TriangleRatio,
                        clusterSettings->lod4TriangleRatio,
                    }) },
                    { "lodTargetErrors", nlohmann::json::array({
                        clusterSettings->lod1TargetError,
                        clusterSettings->lod2TargetError,
                        clusterSettings->lod3TargetError,
                        clusterSettings->lod4TargetError,
                    }) },
                    { "lodMinScreenRadii", nlohmann::json::array({
                        clusterSettings->lod0MinScreenRadius,
                        clusterSettings->lod1MinScreenRadius,
                        clusterSettings->lod2MinScreenRadius,
                        clusterSettings->lod3MinScreenRadius,
                    }) },
                    { "surfacePartitionPolicy", ASSETS::IMPORT_POLICY::ToString(clusterSettings->surfacePartitionPolicy) },
                    { "partitionLargeStaticSurfaces", clusterSettings->partitionLargeStaticSurfaces },
                    { "largeSurfacePartitionMaxExtent", clusterSettings->largeSurfacePartitionMaxExtent },
                    { "largeSurfacePartitionMinTriangles", clusterSettings->largeSurfacePartitionMinTriangles },
                    { "largeSurfacePartitionMinTrianglesPerChunk", clusterSettings->largeSurfacePartitionMinTrianglesPerChunk },
                    { "largeSurfacePartitionMaxDepth", clusterSettings->largeSurfacePartitionMaxDepth },
                    { "lockPartitionBorders", clusterSettings->lockPartitionBorders },
                    { "balancePlanarStaticSurfaces", clusterSettings->balancePlanarStaticSurfaces },
                    { "partitionMinClusterEstimate", clusterSettings->minPartitionClusterEstimate },
                    { "planarSurfaceMinPartitionExtent", clusterSettings->planarStaticSurfaceMinPartitionExtent },
                    { "planarSurfaceMinTrianglesPerChunk", clusterSettings->planarStaticSurfaceMinTrianglesPerChunk },
                    { "planarSurfaceMaxDepth", clusterSettings->planarStaticSurfaceMaxDepth },
                    { "subdivideLargeTriangles", clusterSettings->subdivideLargeStaticTriangles },
                    { "largeTriangleMaxEdgeLength", clusterSettings->largeStaticTriangleMaxEdgeLength },
                    { "largeTriangleMaxSubdivisions", clusterSettings->largeStaticTriangleMaxSubdivisions },
                    { "largeTriangleMaxGeneratedTriangles", clusterSettings->largeStaticTriangleMaxGeneratedTriangles },
                    { "maxTriangleInflationRatio", clusterSettings->maxTriangleInflationRatio },
                    { "maxVertexInflationRatio", clusterSettings->maxVertexInflationRatio },
                    { "minAverageTrianglesPerClusterWarning", clusterSettings->minAverageTrianglesPerClusterWarning },
                    { "meshletConeWeight", clusterSettings->meshletConeWeight },
                    { "meshletSplitFactor", clusterSettings->meshletSplitFactor },
                    { "compactUnderfilledClusters", clusterSettings->compactUnderfilledClusterGroups },
                    { "minClusterOccupancyRatio", clusterSettings->minClusterOccupancyRatio },
                    { "maxNormalBucketClusterOverhead", clusterSettings->maxNormalBucketClusterOverhead },
                    { "clusterMergeNormalMinDot", clusterSettings->clusterMergeNormalMinDot },
                    { "normalBucketCoherentGroupMinDot", clusterSettings->normalBucketCoherentGroupMinDot },
                    { "normalBucketQualityBonusRatio", clusterSettings->normalBucketQualityBonusRatio },
                    { "buildNormalCone", clusterSettings->buildNormalCone },
                };
            }
            if (clusteredReport != nullptr) {
                nlohmann::json messages = nlohmann::json::array();
                for (const std::string& message : clusteredReport->messages) {
                    messages.push_back(message);
                }
                clusterJson["summary"] = {
                    { "sourceStaticTriangles", clusteredReport->sourceStaticTriangleCount },
                    { "sourceStaticVertices", clusteredReport->sourceStaticVertexCount },
                    { "surfaces", clusteredReport->surfaceCount },
                    { "surfaceLodRanges", clusteredReport->surfaceLodRangeCount },
                    { "surfaceSections", clusteredReport->surfaceSectionCount },
                    { "clusters", clusteredReport->clusterCount },
                    { "pages", clusteredReport->pageCount },
                    { "triangles", clusteredReport->triangleCount },
                    { "vertices", clusteredReport->vertexCount },
                    { "maxVerticesPerCluster", clusteredReport->maxVerticesPerCluster },
                    { "normalConeValidClusters", clusteredReport->normalConeValidClusterCount },
                    { "normalConeInvalidClusters", clusteredReport->normalConeInvalidClusterCount },
                    { "normalConeValidRatio", clusteredReport->clusterCount > 0u
                        ? static_cast<double>(clusteredReport->normalConeValidClusterCount) / static_cast<double>(clusteredReport->clusterCount)
                        : 0.0 },
                    { "normalConeCutoffLeZero", clusteredReport->normalConeCutoffLeZeroCount },
                    { "normalConeCutoffGeOne", clusteredReport->normalConeCutoffGeOneCount },
                    { "normalConeAxisInvalid", clusteredReport->normalConeAxisInvalidCount },
                    { "avgTrianglesPerCluster", clusteredReport->clusterCount > 0u
                        ? static_cast<double>(clusteredReport->triangleCount) / static_cast<double>(clusteredReport->clusterCount)
                        : 0.0 },
                    { "avgVerticesPerCluster", clusteredReport->clusterCount > 0u
                        ? static_cast<double>(clusteredReport->vertexCount) / static_cast<double>(clusteredReport->clusterCount)
                        : 0.0 },
                    { "skippedSkinnedPrimitives", clusteredReport->skippedSkinnedPrimitiveCount },
                    { "skippedMorphPrimitives", clusteredReport->skippedMorphPrimitiveCount },
                    { "skippedInvalidPrimitives", clusteredReport->skippedInvalidPrimitiveCount },
                    { "unsupportedPrimitiveModes", clusteredReport->unsupportedPrimitiveModeCount },
                    { "unsupportedFeatures", clusteredReport->unsupportedFeatureCount },
                    { "partitionedSurfaces", clusteredReport->partitionedSurfaceCount },
                    { "partitionedSurfaceChunks", clusteredReport->partitionedSurfaceChunkCount },
                    { "rejectedPartitionedSurfaces", clusteredReport->rejectedPartitionedSurfaceCount },
                    { "normalPartitionedSurfaces", clusteredReport->normalPartitionedSurfaceCount },
                    { "normalPartitionedSurfaceChunks", clusteredReport->normalPartitionedChunkCount },
                    { "acceptedNormalBucketGroups", clusteredReport->acceptedNormalBucketGroupCount },
                    { "rejectedNormalBucketGroups", clusteredReport->rejectedNormalBucketGroupCount },
                    { "compactedClusterGroups", clusteredReport->compactedClusterGroupCount },
                    { "mergedClusterGroups", clusteredReport->mergedClusterGroupCount },
                    { "planarPartitionedSurfaces", clusteredReport->planarPartitionedSurfaceCount },
                    { "planarPartitionedSurfaceChunks", clusteredReport->planarPartitionedChunkCount },
                    { "planarPartitionCoarsenedSurfaces", clusteredReport->planarPartitionCoarsenedSurfaceCount },
                    { "subdividedSurfaces", clusteredReport->subdividedSurfaceCount },
                    { "subdividedSourceTriangles", clusteredReport->subdividedSourceTriangleCount },
                    { "subdividedOutputTriangles", clusteredReport->subdividedOutputTriangleCount },
                    { "planarSubdivisionSkippedSurfaces", clusteredReport->planarSubdivisionSkippedSurfaceCount },
                    { "planarSubdivisionCoarsenedSurfaces", clusteredReport->planarSubdivisionCoarsenedSurfaceCount },
                    { "singleTriangleClusters", clusteredReport->singleTriangleClusterCount },
                    { "lowTriangleClusters", clusteredReport->lowTriangleClusterCount },
                    { "maxTrianglesPerClusterObserved", clusteredReport->maxTrianglesPerClusterObserved },
                    { "packedGeometryBytes", clusteredReport->packedGeometryByteSize },
                    { "packedMetadataBytes", clusteredReport->packedMetadataByteSize },
                    { "packedTotalBytes", clusteredReport->packedTotalByteSize },
                    { "fallbackIndexBytes", clusteredReport->fallbackIndexByteSize },
                    { "meshletPrimitiveBytes", clusteredReport->meshletPrimitiveByteSize },
                    { "packedVertexPositionBytes", clusteredReport->packedVertexPositionByteSize },
                    { "packedVertexAttributeBytes", clusteredReport->packedVertexAttributeByteSize },
                    { "packedSkinVertexBytes", clusteredReport->packedSkinVertexByteSize },
                    { "averageTrianglesPerCluster", clusteredReport->averageTrianglesPerCluster },
                    { "triangleInflationRatio", clusteredReport->triangleInflationRatio },
                    { "vertexInflationRatio", clusteredReport->vertexInflationRatio },
                    { "triangleBudgetExceeded", clusteredReport->triangleBudgetExceeded },
                    { "vertexBudgetExceeded", clusteredReport->vertexBudgetExceeded },
                    { "clusterOccupancyWarning", clusteredReport->clusterOccupancyWarning },
                    { "normalConeCutoffMin", clusteredReport->normalConeCutoffMin },
                    { "normalConeCutoffAverage", clusteredReport->normalConeCutoffAverage },
                    { "normalConeCutoffMax", clusteredReport->normalConeCutoffMax },
                };
                clusterJson["messages"] = std::move(messages);
            }
            if (clusteredValidation != nullptr) {
                nlohmann::json validationMessages = nlohmann::json::array();
                for (const std::string& message : clusteredValidation->messages) {
                    validationMessages.push_back(message);
                }
                clusterJson["validation"] = {
                    { "valid", clusteredValidation->valid },
                    { "invalidSurfaces", clusteredValidation->invalidSurfaceCount },
                    { "invalidClusters", clusteredValidation->invalidClusterCount },
                    { "invalidPages", clusteredValidation->invalidPageCount },
                    { "invalidBounds", clusteredValidation->invalidBoundsCount },
                    { "invalidMaterials", clusteredValidation->invalidMaterialCount },
                    { "messages", std::move(validationMessages) },
                };
            }
            diagnostics["clusteredGeometry"] = std::move(clusterJson);
            return diagnostics;
        }

        bool ReadTextureAlphaDiagnostics(
            const std::filesystem::path& projectRoot,
            const std::string& textureGuid,
            TextureCookDiagnostic& inOutDiagnostic) {

            if (textureGuid.empty()) {
                return false;
            }

            const std::filesystem::path reportPath =
                projectRoot / "Library" / "Imported" / textureGuid / "import_report.json";
            nlohmann::json report;
            if (!SERIALIZATION::JSON::ReadJsonFile(
                    reportPath,
                    report) ||
                !report.is_object()) {
                return false;
            }

            if (!report.contains("diagnostics") || !report["diagnostics"].is_object()) {
                return false;
            }
            const nlohmann::json& diagnostics = report["diagnostics"];
            if (!diagnostics.contains("texture") || !diagnostics["texture"].is_object()) {
                return false;
            }
            const nlohmann::json& texture = diagnostics["texture"];
            if (!texture.contains("sourceHasMeaningfulAlpha") ||
                !texture.contains("sourceHasTranslucentAlpha") ||
                !texture.contains("sourceHasCutoutAlpha") ||
                !texture.contains("sourceAlphaTranslucentRatio") ||
                !texture.contains("sourceAlphaCutoutRatio")) {
                return false;
            }
            inOutDiagnostic.sourceHasMeaningfulAlpha =
                texture.value("sourceHasMeaningfulAlpha", false);
            inOutDiagnostic.sourceHasTranslucentAlpha =
                texture.value("sourceHasTranslucentAlpha", false);
            inOutDiagnostic.sourceHasCutoutAlpha =
                texture.value("sourceHasCutoutAlpha", false);
            inOutDiagnostic.sourceAlphaNonOpaqueRatio =
                texture.value("sourceAlphaNonOpaqueRatio", 0.0f);
            inOutDiagnostic.sourceAlphaTranslucentRatio =
                texture.value("sourceAlphaTranslucentRatio", 0.0f);
            inOutDiagnostic.sourceAlphaCutoutRatio =
                texture.value("sourceAlphaCutoutRatio", 0.0f);
            return true;
        }

        void ApplyTextureAlphaSettings(
            const TextureImportSettings& settings,
            TextureCookDiagnostic& inOutDiagnostic) {

            inOutDiagnostic.sourceHasMeaningfulAlpha = settings.sourceHasMeaningfulAlpha;
            inOutDiagnostic.sourceHasTranslucentAlpha = settings.sourceHasTranslucentAlpha;
            inOutDiagnostic.sourceHasCutoutAlpha = settings.sourceHasCutoutAlpha;
            inOutDiagnostic.sourceAlphaNonOpaqueRatio = settings.sourceAlphaNonOpaqueRatio;
            inOutDiagnostic.sourceAlphaTranslucentRatio = settings.sourceAlphaTranslucentRatio;
            inOutDiagnostic.sourceAlphaCutoutRatio = settings.sourceAlphaCutoutRatio;
        }

        bool IsCutoutDominantAlpha(const TextureCookDiagnostic& texture) {
            if (!texture.sourceHasCutoutAlpha) {
                return false;
            }

            const float cutoutRatio = texture.sourceAlphaCutoutRatio;
            const float translucentRatio = texture.sourceAlphaTranslucentRatio;
            if (cutoutRatio <= 0.0f && translucentRatio <= 0.0f) {
                return !texture.sourceHasTranslucentAlpha;
            }

            // BLEND と記録された抜き材質を、実際の alpha 分布に合わせて MASK へ寄せる。
            return cutoutRatio >= translucentRatio;
        }

        bool InspectSourceTextureAlpha(
            const std::filesystem::path& absoluteTexturePath,
            TextureCookDiagnostic& inOutDiagnostic) {

            if (absoluteTexturePath.empty()) {
                return false;
            }

            TextureImportSettings settings{};
            settings.usage = TextureUsage::BaseColor;
            std::string message{};
            if (!InspectTextureAlphaWithDirectXTex(absoluteTexturePath, settings, message)) {
                return false;
            }

            ApplyTextureAlphaSettings(settings, inOutDiagnostic);
            return true;
        }

        bool ResolveTextureToHtex(
            const std::filesystem::path& projectRoot,
            const std::filesystem::path& sourceMetaRoot,
            const std::filesystem::path& libraryRoot,
            TextureAsset3D& texture,
            AssetDependencyDesc& outDependency,
            bool& outHasDependency,
            TextureCookDiagnostic& inOutDiagnostic) {

            outHasDependency = false;
            if (texture.sourcePath.empty()) {
                return false;
            }

            const std::filesystem::path absoluteTexturePath = PROJECT_PATHS::ResolveProjectPath(projectRoot, texture.sourcePath);
            const std::filesystem::path relativeTexturePath = PROJECT_PATHS::MakeProjectRelativePath(projectRoot, absoluteTexturePath);
            const std::filesystem::path metaPath = MakeSourceMetaPath(sourceMetaRoot, relativeTexturePath);
            if (metaPath.empty()) {
                return false;
            }

            nlohmann::json metaJson;
            if (!SERIALIZATION::JSON::ReadJsonFile(
                    metaPath,
                    metaJson) ||
                !metaJson.is_object()) {
                return false;
            }

            const std::string guid = metaJson.value("guid", "");
            if (guid.empty()) {
                return false;
            }
            outDependency.guid.value = guid;
            outDependency.path = relativeTexturePath.generic_string();
            outDependency.role = "Texture";
            outHasDependency = true;
            inOutDiagnostic.guid = guid;
            inOutDiagnostic.dependencyResolved = true;
            if (!ReadTextureAlphaDiagnostics(projectRoot, guid, inOutDiagnostic)) {
                    // 古い texture report の場合でも、model cook は baseColor alpha を見落とさない。
                InspectSourceTextureAlpha(absoluteTexturePath, inOutDiagnostic);
            }
            nlohmann::json manifestJson;
            const std::filesystem::path artifactManifestPath = MakeArtifactManifestPath(libraryRoot, guid);
            if (artifactManifestPath.empty() ||
                !SERIALIZATION::JSON::ReadJsonFile(
                    artifactManifestPath,
                    manifestJson) ||
                !manifestJson.is_object()) {
                return false;
            }

            if (!manifestJson.contains("artifacts") || !manifestJson["artifacts"].is_array()) {
                return false;
            }

            // テクスチャ meta の MainTexture が HTEX なら、モデル内参照を実行時向けに差し替える。
            for (const auto& artifact : manifestJson["artifacts"]) {
                if (!artifact.is_object()) {
                    continue;
                }

                AssetArtifactDesc descriptor{};
                descriptor.role = artifact.value("role", "");
                descriptor.format = artifact.value("format", "");
                descriptor.path = artifact.value("path", "");
                if (ASSETS::SEMANTICS::MatchesAssetArtifact(
                    descriptor,
                    ASSETS::SEMANTICS::AssetArtifactKind::MainTexture) &&
                    !descriptor.path.empty()) {
                    texture.sourcePath = descriptor.path;
                    return true;
                }
            }
            return false;
        }

        bool HasMaterialThinSurfaceCookHint(const MaterialAsset& material) {
            return
                MATERIAL_POLICY::HasThinTransparentSurfaceHint(material) ||
                SURFACE_POLICY::HasThinSurfaceCue(material.name) ||
                SURFACE_POLICY::HasThinSurfaceCue(material.shaderProfileId) ||
                SURFACE_POLICY::HasThinSurfaceCue(material.defaultMaterialFxProfileId);
        }

        bool HasMaterialOpticalTransparencyCookHint(
            const MaterialAsset& material) {
            return
                MATERIAL_POLICY::HasOpticalTransparencyCue(
                    material.name) ||
                MATERIAL_POLICY::HasOpticalTransparencyCue(
                    material.shaderProfileId) ||
                MATERIAL_POLICY::HasOpticalTransparencyCue(
                    material.defaultMaterialFxProfileId);
        }

        std::vector<bool> BuildMaterialOpticalTransparencyUsageHints(
            const ModelAsset& model) {

            std::vector<bool> hints(model.materials.size(), false);
            for (const ModelNode& node : model.nodes) {
                if (node.meshIndex < 0 ||
                    static_cast<size_t>(node.meshIndex) >=
                        model.meshes.size()) {
                    continue;
                }

                const MeshAsset& mesh =
                    model.meshes[static_cast<size_t>(node.meshIndex)];
                const bool nodeHasOpticalCue =
                    MATERIAL_POLICY::HasOpticalTransparencyCue(node.name);
                const bool meshHasOpticalCue =
                    MATERIAL_POLICY::HasOpticalTransparencyCue(mesh.name);
                for (const MeshPrimitive& primitive : mesh.primitives) {
                    if (primitive.materialIndex >= hints.size()) {
                        continue;
                    }
                    const bool primitiveHasOpticalCue =
                        MATERIAL_POLICY::HasOpticalTransparencyCue(
                            primitive.name);
                    if (nodeHasOpticalCue ||
                        meshHasOpticalCue ||
                        primitiveHasOpticalCue) {
                        hints[primitive.materialIndex] = true;
                    }
                }
            }
            return hints;
        }

        void SetMaterialAlphaMode(MaterialAsset& material, AlphaMode mode) {
            const bool sourceDoubleSided = material.doubleSided;

            material.alphaMode = mode;
            material.featureBits &= ~MATERIAL_FEATURES::AlphaMask;
            material.featureBits &= ~MATERIAL_FEATURES::ThinTransparentSurface;

            if (mode == AlphaMode::Mask) {
                material.featureBits |= MATERIAL_FEATURES::AlphaMask;
                material.doubleSided = true;
                return;
            }

            if (mode == AlphaMode::Blend) {
                material.featureBits |= MATERIAL_FEATURES::ThinTransparentSurface;
                material.doubleSided = true;
                return;
            }

            material.doubleSided = sourceDoubleSided;
        }

        MaterialAlphaPolicyStats ApplyCanonicalBaseColorAlphaMaterialPolicy(
            ModelAsset& model,
            const std::vector<TextureCookDiagnostic>& textureDiagnostics) {

            MaterialAlphaPolicyStats stats{};
            const std::vector<bool> opticalUsageHints =
                BuildMaterialOpticalTransparencyUsageHints(model);

            for (size_t materialIndex = 0;
                 materialIndex < model.materials.size();
                 ++materialIndex) {
                MaterialAsset& material = model.materials[materialIndex];
                const AlphaMode originalMode = material.alphaMode;
                const bool originalDoubleSided = material.doubleSided;

                const int textureIndex = material.baseColorTexture.textureIndex;
                const TextureCookDiagnostic* texture = nullptr;
                if (textureIndex >= 0 &&
                    static_cast<size_t>(textureIndex) < textureDiagnostics.size()) {
                    texture = &textureDiagnostics[static_cast<size_t>(textureIndex)];
                }

                const bool factorAlpha = material.baseColorFactor.w < 0.999f;
                const bool textureAlpha = texture != nullptr && texture->sourceHasMeaningfulAlpha;
                const bool textureCutout = textureAlpha && IsCutoutDominantAlpha(*texture);
                const bool textureTranslucent =
                    textureAlpha &&
                    texture->sourceHasTranslucentAlpha &&
                    !textureCutout;
                const bool explicitThinCue = HasMaterialThinSurfaceCookHint(material);
                const bool opticalTransparencyCue =
                    HasMaterialOpticalTransparencyCookHint(
                        material) ||
                    opticalUsageHints[materialIndex];
                const bool sourceRequestedMask = originalMode == AlphaMode::Mask;
                const bool sourceRequestedBlend = originalMode == AlphaMode::Blend;
                const bool needsOpticalOpacityFallback =
                    sourceRequestedBlend &&
                    opticalTransparencyCue &&
                    !factorAlpha &&
                    !textureAlpha;

                const bool cutoutSurface =
                    textureCutout &&
                    (sourceRequestedMask || sourceRequestedBlend || explicitThinCue);
                const bool translucentSurface =
                    factorAlpha ||
                    (textureTranslucent &&
                        (sourceRequestedBlend || explicitThinCue)) ||
                    (sourceRequestedBlend &&
                        opticalTransparencyCue);

                AlphaMode resolvedMode = AlphaMode::Opaque;
                if (cutoutSurface && !factorAlpha) {
                    resolvedMode = AlphaMode::Mask;
                } else if (translucentSurface) {
                    resolvedMode = AlphaMode::Blend;
                }

                SetMaterialAlphaMode(material, resolvedMode);
                if (resolvedMode == AlphaMode::Blend &&
                    needsOpticalOpacityFallback) {
                    material.baseColorFactor.w =
                        MATERIAL_POLICY::
                            kOpticalTransparencyFallbackOpacity;
                    ++stats.opticalOpacityFallbackCount;
                }

                if (resolvedMode == AlphaMode::Opaque) {
                    ++stats.opaqueCount;
                    if (originalMode != AlphaMode::Opaque) {
                        ++stats.normalizedToOpaqueCount;
                    }
                    if (originalDoubleSided && material.doubleSided) {
                        ++stats.opaqueDoubleSidedPreservedCount;
                    }
                } else if (resolvedMode == AlphaMode::Mask) {
                    ++stats.maskCount;
                    if (originalMode != AlphaMode::Mask) {
                        ++stats.normalizedToMaskCount;
                    }
                } else {
                    ++stats.blendCount;
                    if (originalMode != AlphaMode::Blend) {
                        ++stats.normalizedToBlendCount;
                    }
                }
            }

            return stats;
        }

        bool NearlyEqual(float lhs, float rhs) {
            return std::abs(lhs - rhs) <= 1.0e-5f;
        }

        bool HaveMatchingUvSemantics(
            const TextureSlot& lhs,
            const TextureSlot& rhs) {

            return
                lhs.texCoord == rhs.texCoord &&
                NearlyEqual(lhs.uvScale.x, rhs.uvScale.x) &&
                NearlyEqual(lhs.uvScale.y, rhs.uvScale.y) &&
                NearlyEqual(lhs.uvOffset.x, rhs.uvOffset.x) &&
                NearlyEqual(lhs.uvOffset.y, rhs.uvOffset.y) &&
                NearlyEqual(lhs.uvRotation, rhs.uvRotation);
        }

        const TextureAsset3D* FindTexture(
            const ModelAsset& model,
            const TextureSlot& slot) {

            if (slot.textureIndex < 0 ||
                static_cast<size_t>(slot.textureIndex) >=
                    model.textures.size()) {
                return nullptr;
            }
            return &model.textures[
                static_cast<size_t>(slot.textureIndex)];
        }

        std::filesystem::path ResolveTextureSourcePath(
            const ModelAsset& model,
            const TextureSlot& slot,
            const std::filesystem::path& projectRoot) {

            const TextureAsset3D* texture =
                FindTexture(model, slot);
            if (texture == nullptr ||
                texture->sourcePath.empty()) {
                return {};
            }
            return PROJECT_PATHS::ResolveProjectPath(
                projectRoot,
                texture->sourcePath);
        }

        void BuildCanonicalMetallicRoughnessTextures(
            ModelAsset& model,
            const AssetImportContext& context,
            std::vector<AssetArtifactDesc>& artifacts) {

            const size_t materialCount = (std::min)(
                model.materials.size(),
                model.materialImportSources.size());
            for (size_t materialIndex = 0u;
                materialIndex < materialCount;
                ++materialIndex) {

                MaterialAsset& material =
                    model.materials[materialIndex];
                if (material.metallicRoughnessTexture.textureIndex >= 0) {
                    continue;
                }

                const MaterialImportSourceSlots& sources =
                    model.materialImportSources[materialIndex];
                const TextureSlot* canonicalSlot = nullptr;
                const TextureSlot* sourceSlots[] = {
                    &sources.metalnessTexture,
                    &sources.roughnessTexture,
                    &sources.glossinessTexture,
                };
                bool incompatibleUvSemantics = false;
                for (const TextureSlot* slot : sourceSlots) {
                    if (slot->textureIndex < 0) {
                        continue;
                    }
                    if (canonicalSlot == nullptr) {
                        canonicalSlot = slot;
                    } else if (!HaveMatchingUvSemantics(
                            *canonicalSlot,
                            *slot)) {
                        incompatibleUvSemantics = true;
                        break;
                    }
                }
                if (canonicalSlot == nullptr) {
                    continue;
                }
                if (incompatibleUvSemantics) {
                    ++model.importDiagnostics.unsupportedFeatureCount;
                    model.importDiagnostics.messages.push_back(
                        "[MaterialImport] separate metallic and roughness maps use different UV semantics; canonical packing was skipped. material=" +
                        material.name);
                    continue;
                }

                MetallicRoughnessPackRequest request{};
                request.metalnessSource =
                    ResolveTextureSourcePath(
                        model,
                        sources.metalnessTexture,
                        context.projectRoot);
                request.roughnessSource =
                    ResolveTextureSourcePath(
                        model,
                        sources.roughnessTexture,
                        context.projectRoot);
                if (request.roughnessSource.empty()) {
                    request.glossinessSource =
                        ResolveTextureSourcePath(
                            model,
                            sources.glossinessTexture,
                            context.projectRoot);
                } else if (
                    sources.glossinessTexture.textureIndex >= 0) {
                    model.importDiagnostics.messages.push_back(
                        "[MaterialImport] roughness map takes precedence over glossiness map. material=" +
                        material.name);
                }

                const std::filesystem::path finalPath =
                    context.importedDirectory /
                    "MaterialTextures" /
                    ("material_" +
                        std::to_string(materialIndex) +
                        "_metallic_roughness.dds");
                const std::filesystem::path stagedPath =
                    context.publication ? context.StageFile(finalPath) : finalPath.parent_path() /
                    (finalPath.stem().string() +
                        ".importing.dds");
                std::error_code removeEc{};
                std::filesystem::remove(stagedPath, removeEc);

                std::string packMessage{};
                if (!WritePackedMetallicRoughnessDdsWithDirectXTex(
                        request,
                        stagedPath,
                        packMessage)) {
                    if (context.publication) context.publication->DiscardFile(finalPath);
                    ++model.importDiagnostics.unsupportedFeatureCount;
                    model.importDiagnostics.messages.push_back(
                        "[MaterialImport] failed to pack metallic-roughness texture. material=" +
                        material.name +
                        " reason=" + packMessage);
                    continue;
                }

                std::string commitMessage{};
                if (!context.publication && !IO::CommitStagedFile(
                        stagedPath,
                        finalPath,
                        commitMessage)) {
                    std::error_code cleanupEc{};
                    std::filesystem::remove(
                        stagedPath,
                        cleanupEc);
                    ++model.importDiagnostics.unsupportedFeatureCount;
                    model.importDiagnostics.messages.push_back(
                        "[MaterialImport] failed to install packed metallic-roughness texture. material=" +
                        material.name +
                        " reason=" + commitMessage);
                    continue;
                }

                TextureAsset3D packedTexture{};
                packedTexture.name =
                    material.name +
                    "_MetallicRoughness";
                packedTexture.sourcePath =
                    PROJECT_PATHS::MakeProjectRelativeString(
                        context.projectRoot,
                        finalPath);
                material.metallicRoughnessTexture =
                    *canonicalSlot;
                material.metallicRoughnessTexture.textureIndex =
                    static_cast<int>(model.textures.size());
                model.textures.push_back(
                    std::move(packedTexture));

                artifacts.push_back(
                    ASSETS::SEMANTICS::MakeAssetArtifact(
                        ASSETS::SEMANTICS::AssetArtifactKind::
                            ModelMaterialTexture,
                        PROJECT_PATHS::MakeProjectRelativeString(
                            context.projectRoot,
                            finalPath)));
                model.importDiagnostics.messages.push_back(
                    "[MaterialImport] packed canonical metallic-roughness texture. material=" +
                    material.name);
            }
        }

        float FiniteOr(float value, float fallback) {
            return std::isfinite(value) ? value : fallback;
        }

        void ApplyCanonicalSpecularPolicy(ModelAsset& model) {
            for (MaterialAsset& material : model.materials) {
                material.featureBits &=
                    ~MATERIAL_FEATURES::SpecularGlossCompatibility;
                // 旧生成物の応答補正は再インポートで除去する。テクスチャの有無から
                // 材質ワークフローを推測して粗さや反射率を上書きしない。
                material.roughnessFactor = std::clamp(
                    FiniteOr(material.roughnessFactor, 1.0f),
                    0.04f,
                    1.0f);
                material.metallicFactor = std::clamp(
                    FiniteOr(material.metallicFactor, 0.0f),
                    0.0f,
                    1.0f);
                material.specularFactor = std::clamp(
                    FiniteOr(material.specularFactor, 1.0f),
                    0.0f,
                    1.0f);
                // KHR_materials_specular の色係数には上限 1 がない。
                // ソース値を保持し、最終反射率の上限は共有 PBR 評価で適用する。
                for (float* channel : { &material.specularColorFactor.x,
                    &material.specularColorFactor.y, &material.specularColorFactor.z }) {
                    *channel = (std::max)(0.0f, FiniteOr(*channel, 1.0f));
                }
            }
        }

    }

    ASSETS::SEMANTICS::AssetImporterKind
        ModelImporter::GetImporterKind() const noexcept {
        return ASSETS::SEMANTICS::AssetImporterKind::Model;
    }

    AssetMeta ModelImporter::CreateDefaultMeta(
        const std::filesystem::path& sourcePath,
        const AssetGuid& guid) const {

        AssetMeta meta = ASSETS::SEMANTICS::MakeBaseAssetMeta(
            sourcePath,
            guid,
            GetImporterKind());
        meta.importSettingsJson =
            ASSETS::IMPORT_POLICY::MakeDefaultModelImportSettingsJson(
                sourcePath);
        return meta;
    }

    AssetImportResult ModelImporter::Import(
        const AssetRecord& record,
        const AssetImportContext& context) {

        AssetImportResult result{};
        const std::string taskItem =
            record.sourcePath.filename().string();
        const auto reportStage =
            [&](const char* stage,
                float normalized,
                bool determinate = true) {
                if (context.task != nullptr) {
                    context.task->ReportStage(
                        stage,
                        normalized,
                        determinate,
                        taskItem);
                }
            };
        const auto canceled = [&]() {
            return context.task != nullptr &&
                context.task->IsCancellationRequested();
        };
        if (!ASSETS::SEMANTICS::IsAssetSourceForImporter(record.sourcePath, GetImporterKind())) {
            const auto& semantics = GetSemantics();
            result.message = "[AssetImporter] HMODEL cook supports " +
                std::string(semantics.supportedSources) +
                " only. source=" +
                record.sourcePath.generic_string();
            HIKARI_LOG_ERROR(result.message);
            return result;
        }

        const ASSETS::IMPORT_POLICY::ModelImportPolicy importPolicy =
            ASSETS::IMPORT_POLICY::ResolveModelImportPolicy(
                record.sourcePath,
                record.meta.importSettingsJson);
        const ModelGeometryCookProfile clusterProfile = importPolicy.clusterOptions.profile;
        const bool buildClusterGeometry = importPolicy.clusterOptions.buildClusterGeometry;
        const ASSETS::GEOMETRY::ClusteredGeometryCookSettings& clusterSettings =
            importPolicy.clusteredGeometryCookSettings;
        const std::filesystem::path absoluteSource = PROJECT_PATHS::ResolveProjectPath(context.projectRoot, record.sourcePath);

        reportStage("Parsing model source", 0.08f, false);
        if (canceled()) {
            result.message = "[AssetImporter] model import canceled";
            return result;
        }
        ModelAsset model{};
        model.SetName(record.guid.value);
        model.SetSourcePath(absoluteSource.generic_string());
        if (!ASSETS::MODELS::LoadModelSource(model)) {
            result.message = "[AssetImporter] failed to read model source for HMODEL cook. source=" +
                record.sourcePath.generic_string();
            HIKARI_LOG_ERROR(result.message);
            return result;
        }

        model.SetName(record.guid.value);
        model.SetSourcePath(record.sourcePath.generic_string());
        if (importPolicy.importer == ModelImporterKind::Gltf) {
            AppendGltfSourceBufferDependencies(
                absoluteSource,
                context.projectRoot,
                result.dependencies);
        }

        BuildCanonicalMetallicRoughnessTextures(
            model,
            context,
            result.artifacts);

        int htexReferenceCount = 0;
        int fallbackTextureCount = 0;
        std::vector<TextureCookDiagnostic> textureDiagnostics;
        textureDiagnostics.reserve(model.textures.size());
        for (size_t i = 0; i < model.textures.size(); ++i) {
            if (canceled()) {
                result.message = "[AssetImporter] model import canceled";
                return result;
            }
            reportStage(
                "Resolving model texture dependencies",
                0.18f +
                    0.14f *
                    (model.textures.empty()
                        ? 1.0f
                        : static_cast<float>(i) /
                            static_cast<float>(model.textures.size())));
            TextureAsset3D& texture = model.textures[i];
            TextureCookDiagnostic diagnostic{};
            diagnostic.index = static_cast<int>(i);
            diagnostic.name = texture.name;
            diagnostic.originalPath = texture.sourcePath;

            AssetDependencyDesc dependency{};
            bool hasDependency = false;
                if (ResolveTextureToHtex(
                    context.projectRoot,
                    context.sourceMetaRoot,
                    context.libraryRoot,
                    texture,
                    dependency,
                    hasDependency,
                    diagnostic)) {
                ++htexReferenceCount;
                diagnostic.htexReady = true;
            } else if (!texture.sourcePath.empty()) {
                ++fallbackTextureCount;
                InspectSourceTextureAlpha(
                    PROJECT_PATHS::ResolveProjectPath(context.projectRoot, texture.sourcePath),
                    diagnostic);
            }

            if (hasDependency) {
                result.dependencies.push_back(std::move(dependency));
            }
            diagnostic.cookedPath = texture.sourcePath;
            textureDiagnostics.push_back(std::move(diagnostic));
        }

        const MaterialAlphaPolicyStats alphaPolicyStats =
            ApplyCanonicalBaseColorAlphaMaterialPolicy(model, textureDiagnostics);
        ApplyCanonicalSpecularPolicy(model);

        reportStage(
            "Preparing lightmap UVs",
            0.34f,
            false);
        ASSETS::LIGHTING::LightmapUvGenerationReport
            lightmapUvReport{};
        ASSETS::LIGHTING::LightmapUvGenerationCallbacks
            lightmapUvCallbacks{};
        lightmapUvCallbacks.isCancellationRequested =
            canceled;
        lightmapUvCallbacks.reportProgress =
            [&](uint32_t currentPrimitive,
                uint32_t primitiveCount,
                std::string_view primitiveName) {
                if (context.task == nullptr) {
                    return;
                }

                AssetTaskProgress progress{};
                progress.stage =
                    "Preparing lightmap UVs";
                progress.currentItem = taskItem;
                if (!primitiveName.empty()) {
                    progress.currentItem +=
                        " :: " +
                        std::string(primitiveName);
                }
                progress.determinate =
                    primitiveCount > 0u;
                progress.completedUnits =
                    currentPrimitive;
                progress.totalUnits =
                    primitiveCount;
                const float uvProgress =
                    primitiveCount > 0u
                    ? static_cast<float>(
                        currentPrimitive) /
                        static_cast<float>(
                            primitiveCount)
                    : 1.0f;
                progress.normalized =
                    0.34f + uvProgress * 0.02f;
                context.task->Report(
                    std::move(progress));
            };
        ASSETS::LIGHTING::PrepareModelLightmapUvs(
            model,
            importPolicy.lightmapUv,
            lightmapUvReport,
            &lightmapUvCallbacks);
        if (lightmapUvReport.canceled ||
            canceled()) {
            result.message =
                "[AssetImporter] model import canceled during lightmap UV generation";
            return result;
        }
        model.importDiagnostics.
            lightmapUvAuthoredValidCount =
                lightmapUvReport.authoredValidCount;
        model.importDiagnostics.lightmapUvMissingCount =
            lightmapUvReport.missingCount;
        model.importDiagnostics.lightmapUvInvalidCount =
            lightmapUvReport.invalidCount;
        model.importDiagnostics.lightmapUvGeneratedCount =
            lightmapUvReport.generatedCount;
        model.importDiagnostics.
            lightmapUvGenerationFailedCount =
                lightmapUvReport.generationFailedCount;
        model.importDiagnostics.messages.push_back(
            "[LightmapUV] mode=" +
            std::string(
                ASSETS::LIGHTING::ToString(
                    importPolicy.lightmapUv.mode)) +
            " authoredValid=" +
            std::to_string(
                lightmapUvReport.authoredValidCount) +
            " missing=" +
            std::to_string(
                lightmapUvReport.missingCount) +
            " invalid=" +
            std::to_string(
                lightmapUvReport.invalidCount) +
            " generated=" +
            std::to_string(
                lightmapUvReport.generatedCount) +
            " failed=" +
            std::to_string(
                lightmapUvReport.generationFailedCount));
        for (const std::string& message :
            lightmapUvReport.messages) {
            model.importDiagnostics.messages.push_back(
                "[LightmapUV] " + message);
        }

        const std::filesystem::path finalPath = context.importedDirectory / "model.hmodel";
        const std::filesystem::path tempPath = context.publication ? context.StageFile(finalPath)
            : context.importedDirectory / "model.importing.hmodel";

        reportStage("Writing HMODEL", 0.36f);
        std::error_code removeEc{};
        std::filesystem::remove(tempPath, removeEc);
        if (removeEc) {
            result.message = "[AssetImporter] failed to clear stale temporary HMODEL: " + tempPath.generic_string();
            HIKARI_LOG_ERROR(result.message);
            return result;
        }

        std::string hmodelMessage{};
        if (!WriteHmodelFile(tempPath, model, hmodelMessage)) {
            std::error_code cleanupEc{};
            std::filesystem::remove(tempPath, cleanupEc);
            result.message = hmodelMessage.empty() ? "[AssetImporter] HMODEL write failed" : hmodelMessage;
            HIKARI_LOG_ERROR(result.message);
            return result;
        }

        std::string hmodelCommitMessage{};
        if (!context.publication && !IO::CommitStagedFile(
            tempPath,
            finalPath,
            hmodelCommitMessage)) {
            std::error_code cleanupEc{};
            std::filesystem::remove(tempPath, cleanupEc);
            result.message =
                "[AssetImporter] failed to replace HMODEL artifact. temp=" +
                tempPath.generic_string() +
                " final=" + finalPath.generic_string() +
                " reason=" + hmodelCommitMessage;
            HIKARI_LOG_ERROR(result.message);
            return result;
        }

        bool hcmeshReady = false;
        std::string hcmeshMessage{};
        RENDER3D::CLUSTER::ClusteredGeometryBuildReport clusteredReport{};
        ASSETS::GEOMETRY::ClusteredGeometryValidationResult clusteredValidation{};
        const RENDER3D::CLUSTER::ClusteredGeometryBuildReport* clusteredReportPtr = nullptr;
        const ASSETS::GEOMETRY::ClusteredGeometryValidationResult* clusteredValidationPtr = nullptr;

        if (!buildClusterGeometry) {
            hcmeshMessage = "[AssetImporter] HCMESH cook disabled by model import settings";
        } else {
            if (canceled()) {
                result.message = "[AssetImporter] model import canceled";
                return result;
            }
            reportStage("Cooking clustered geometry", 0.52f, false);
            RENDER3D::CLUSTER::ClusteredGeometryAsset clusteredGeometry{};
            clusteredReportPtr = &clusteredReport;
            if (ASSETS::GEOMETRY::CookClusteredGeometryFromModel(
                model,
                record.guid,
                clusterSettings,
                clusteredGeometry,
                clusteredReport)) {
                clusteredValidation = ASSETS::GEOMETRY::ValidateClusteredGeometryAsset(clusteredGeometry);
                clusteredValidationPtr = &clusteredValidation;
                if (clusteredValidation.valid) {
                    reportStage("Writing HCMESH", 0.76f);
                    const std::filesystem::path finalHcmeshPath = context.importedDirectory / "clustered_mesh.hcmesh";
                    const std::filesystem::path tempHcmeshPath = context.publication ? context.StageFile(finalHcmeshPath)
                        : context.importedDirectory / "clustered_mesh.importing.hcmesh";
                    std::error_code removeHcmeshEc{};
                    std::filesystem::remove(tempHcmeshPath, removeHcmeshEc);
                    if (removeHcmeshEc) {
                        hcmeshMessage = "[AssetImporter] failed to clear stale temporary HCMESH: " +
                            tempHcmeshPath.generic_string();
                    } else if (ASSETS::GEOMETRY::WriteHcmeshFile(
                        tempHcmeshPath,
                        clusteredGeometry,
                        hcmeshMessage)) {
                        std::string commitMessage{};
                        if (context.publication || IO::CommitStagedFile(
                            tempHcmeshPath,
                            finalHcmeshPath,
                            commitMessage)) {
                            hcmeshReady = true;
                            result.artifacts.push_back(
                                ASSETS::SEMANTICS::MakeAssetArtifact(
                                    ASSETS::SEMANTICS::AssetArtifactKind::
                                        ClusteredGeometry,
                                    PROJECT_PATHS::MakeProjectRelativeString(
                                        context.projectRoot,
                                        finalHcmeshPath)));
                        } else {
                            std::error_code cleanupEc{};
                            std::filesystem::remove(
                                tempHcmeshPath,
                                cleanupEc);
                            hcmeshMessage =
                                "[AssetImporter] failed to replace HCMESH "
                                "artifact. temp=" +
                                tempHcmeshPath.generic_string() +
                                " final=" +
                                finalHcmeshPath.generic_string() +
                                " reason=" + commitMessage;
                        }
                    }
                } else {
                    hcmeshMessage = clusteredValidation.messages.empty()
                        ? "[AssetImporter] HCMESH validation failed"
                        : clusteredValidation.messages.front();
                }
            } else {
                hcmeshMessage = clusteredReport.messages.empty()
                    ? "[AssetImporter] HCMESH cook produced no clusterable primitive"
                    : clusteredReport.messages.front();
            }
        }

        if (!hcmeshReady && context.publication) {
            context.publication->DiscardFile(context.importedDirectory / "clustered_mesh.hcmesh");
        }
        if (canceled()) {
            result.message = "[AssetImporter] model import canceled";
            return result;
        }
        reportStage("Compiling model collision", 0.86f, false);
        const ASSETS::COLLISION::ModelCollisionArtifactResult
            collisionArtifact =
                ASSETS::COLLISION::BuildModelCollisionArtifact(
                    record,
                    context.projectRoot,
                    context.publication ? context.publication->StageDirectory(context.importedDirectory)
                        : context.importedDirectory);
        const bool hcollisionReady = collisionArtifact.ready;
        if (!collisionArtifact.success) {
            result.message =
                "[AssetImporter] model collision setup compile failed: " +
                collisionArtifact.message;
            HIKARI_LOG_ERROR(result.message);
            return result;
        }
        if (hcollisionReady) {
            const auto collisionPath = context.importedDirectory / collisionArtifact.path.filename();
            if (context.publication) context.StageFile(collisionPath);
            result.artifacts.push_back(ASSETS::SEMANTICS::MakeAssetArtifact(
                ASSETS::SEMANTICS::AssetArtifactKind::CollisionGeometry,
                PROJECT_PATHS::MakeProjectRelativePath(
                    context.projectRoot,
                    collisionPath).generic_string()));
        }
        if (collisionArtifact.removeExisting && context.publication) {
            context.publication->DeleteOnPublish(context.importedDirectory / "collision_geometry.hcollision");
        }

        result.success = true;
        reportStage("Building model import report", 0.96f);
        result.message = "[AssetImporter] Wrote HMODEL meshes=" +
            std::to_string(model.meshes.size()) +
            " materials=" + std::to_string(model.materials.size()) +
            " textures=" + std::to_string(model.textures.size()) +
            " htexRefs=" + std::to_string(htexReferenceCount) +
            " fallbackTextures=" + std::to_string(fallbackTextureCount) +
            " hcmesh=" + (hcmeshReady ? "ready" : "fallback") +
            " hcollision=" + (hcollisionReady ? "ready" : "none");
        nlohmann::json diagnostics = BuildModelDiagnostics(
            model,
            textureDiagnostics,
            htexReferenceCount,
            fallbackTextureCount,
            clusteredReportPtr,
            clusteredValidationPtr,
            clusterProfile,
            &clusterSettings,
            hcmeshReady,
            hcmeshMessage,
            alphaPolicyStats);
        diagnostics["collisionGeometry"] = {
            { "format", "HCOLLISION" },
            { "ready", hcollisionReady },
            { "shapeCount", collisionArtifact.shapeCount },
            { "message", collisionArtifact.message },
            { "authoring", "Model Collision Workspace" },
        };
        result.diagnosticsJson = diagnostics.dump(2);
        result.artifacts.push_back(ASSETS::SEMANTICS::MakeAssetArtifact(
            ASSETS::SEMANTICS::AssetArtifactKind::MainModel,
            PROJECT_PATHS::MakeProjectRelativeString(context.projectRoot, finalPath)));

        HIKARI_LOG_INFO(result.message + " source=" + record.sourcePath.generic_string() + " guid=" + record.guid.value);
        return result;
    }

} // namespace HIKARI
