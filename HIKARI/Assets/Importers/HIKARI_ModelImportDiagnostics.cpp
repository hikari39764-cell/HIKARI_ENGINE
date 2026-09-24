#include "Assets/Importers/HIKARI_ModelImportInternal.h"
#include "Assets/Importers/Policy/HIKARI_ModelImportPolicy.h"
#include "Core/Serialization/Json/HIKARI_JsonMath.h"
#include <utility>

namespace HIKARI::MODEL_IMPORT_INTERNAL {
namespace JsonMath = SERIALIZATION::JSON::MATH;
const char* ToString(AlphaMode alphaMode) {
    switch (alphaMode) {
    case AlphaMode::Mask: return "Mask";
    case AlphaMode::Blend: return "Blend";
    case AlphaMode::Opaque:
    default: return "Opaque";
    }
}

nlohmann::json SlotToJson(const TextureSlot& slot, const std::vector<TextureCookDiagnostic>& textureDiagnostics) {
    nlohmann::json json{
        {"textureIndex", slot.textureIndex},
        {"texCoord", slot.texCoord},
        {"uvScale", JsonMath::ToJsonArray(slot.uvScale)},
        {"uvOffset", JsonMath::ToJsonArray(slot.uvOffset)},
        {"uvRotation", slot.uvRotation},
        {"scale", slot.scale},
        {"strength", slot.strength},
    };

    if (slot.textureIndex >= 0 && static_cast<size_t>(slot.textureIndex) < textureDiagnostics.size()) {
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
    const ModelAsset& model, const std::vector<TextureCookDiagnostic>& textureDiagnostics, int htexReferenceCount,
    int fallbackTextureCount, const RENDER3D::CLUSTER::ClusteredGeometryBuildReport* clusteredReport,
    const ASSETS::GEOMETRY::ClusteredGeometryValidationResult* clusteredValidation,
    ModelGeometryCookProfile clusterProfile, const ASSETS::GEOMETRY::ClusteredGeometryCookSettings* clusterSettings,
    bool hcmeshReady, const std::string& hcmeshMessage, const MaterialAlphaPolicyStats& alphaPolicyStats) {
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
            {"index", texture.index},
            {"name", texture.name},
            {"sourcePath", texture.originalPath},
            {"runtimePath", texture.cookedPath},
            {"guid", texture.guid},
            {"htexReady", texture.htexReady},
            {"dependencyResolved", texture.dependencyResolved},
            {"sourceHasMeaningfulAlpha", texture.sourceHasMeaningfulAlpha},
            {"sourceHasTranslucentAlpha", texture.sourceHasTranslucentAlpha},
            {"sourceHasCutoutAlpha", texture.sourceHasCutoutAlpha},
            {"sourceAlphaNonOpaqueRatio", texture.sourceAlphaNonOpaqueRatio},
            {"sourceAlphaTranslucentRatio", texture.sourceAlphaTranslucentRatio},
            {"sourceAlphaCutoutRatio", texture.sourceAlphaCutoutRatio},
        });
    }

    nlohmann::json materials = nlohmann::json::array();
    for (const MaterialAsset& material : model.materials) {
        materials.push_back({
            {"name", material.name},
            {"baseColorFactor", JsonMath::ToJsonArray(material.baseColorFactor)},
            {"metallicFactor", material.metallicFactor},
            {"roughnessFactor", material.roughnessFactor},
            {"specularFactor", material.specularFactor},
            {"specularColorFactor", JsonMath::ToJsonArray(material.specularColorFactor)},
            {"emissiveFactor", JsonMath::ToJsonArray(material.emissiveFactor)},
            {"emissiveStrength", material.emissiveStrength},
            {"alphaMode", ToString(material.alphaMode)},
            {"doubleSided", material.doubleSided},
            {"featureBits", material.featureBits},
            {"slots",
             {
                 {"baseColor", SlotToJson(material.baseColorTexture, textureDiagnostics)},
                 {"normal", SlotToJson(material.normalTexture, textureDiagnostics)},
                 {"metallicRoughness", SlotToJson(material.metallicRoughnessTexture, textureDiagnostics)},
                 {"occlusion", SlotToJson(material.occlusionTexture, textureDiagnostics)},
                 {"emissive", SlotToJson(material.emissiveTexture, textureDiagnostics)},
                 {"specular", SlotToJson(material.specularTexture, textureDiagnostics)},
                 {"specularColor", SlotToJson(material.specularColorTexture, textureDiagnostics)},
             }},
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
        {"sourceFormat", model.importDiagnostics.sourceFormat},
        {"objectCount", model.importDiagnostics.objectCount},
        {"groupCount", model.importDiagnostics.groupCount},
        {"triangulatedPolygonCount", model.importDiagnostics.triangulatedPolygonCount},
        {"missingNormalGeneratedCount", model.importDiagnostics.missingNormalGeneratedCount},
        {"missingTangentGeneratedCount", model.importDiagnostics.missingTangentGeneratedCount},
        {"unresolvedTextureCount", model.importDiagnostics.unresolvedTextureCount},
        {"unsupportedFeatureCount", model.importDiagnostics.unsupportedFeatureCount},
        {"unsupportedPrimitiveModeCount", model.importDiagnostics.unsupportedPrimitiveModeCount},
        {"unsupportedExtensions", unsupportedExtensions},
        {"skippedMorphPrimitiveCount", model.importDiagnostics.skippedMorphPrimitiveCount},
        {"clusteredStaticPrimitiveCount", model.importDiagnostics.clusteredStaticPrimitiveCount},
        {"fallbackPrimitiveCount", model.importDiagnostics.fallbackPrimitiveCount},
        {"lightmapUvAuthoredValidCount", model.importDiagnostics.lightmapUvAuthoredValidCount},
        {"lightmapUvMissingCount", model.importDiagnostics.lightmapUvMissingCount},
        {"lightmapUvInvalidCount", model.importDiagnostics.lightmapUvInvalidCount},
        {"lightmapUvGeneratedCount", model.importDiagnostics.lightmapUvGeneratedCount},
        {"lightmapUvGenerationFailedCount", model.importDiagnostics.lightmapUvGenerationFailedCount},
    };

    nlohmann::json alphaPolicyReport = {
        {"opaque", alphaPolicyStats.opaqueCount},
        {"mask", alphaPolicyStats.maskCount},
        {"blend", alphaPolicyStats.blendCount},
        {"normalizedToOpaque", alphaPolicyStats.normalizedToOpaqueCount},
        {"normalizedToMask", alphaPolicyStats.normalizedToMaskCount},
        {"normalizedToBlend", alphaPolicyStats.normalizedToBlendCount},
        {"opticalOpacityFallback", alphaPolicyStats.opticalOpacityFallbackCount},
        {"opaqueDoubleSidedPreserved", alphaPolicyStats.opaqueDoubleSidedPreservedCount},
    };

    nlohmann::json diagnostics{
        {"format", "HMODEL"},
        {"summary",
         {
             {"nodes", model.nodes.size()},
             {"meshes", model.meshes.size()},
             {"primitives", primitiveCount},
             {"materials", model.materials.size()},
             {"textures", model.textures.size()},
             {"skins", model.skins.size()},
             {"animations", model.animations.size()},
             {"staticVertices", staticVertexCount},
             {"skinnedVertices", skinnedVertexCount},
             {"indices", indexCount},
             {"htexRefs", htexReferenceCount},
             {"fallbackTextures", fallbackTextureCount},
             {"objectCount", model.importDiagnostics.objectCount},
             {"groupCount", model.importDiagnostics.groupCount},
             {"triangulatedPolygonCount", model.importDiagnostics.triangulatedPolygonCount},
             {"missingNormalGeneratedCount", model.importDiagnostics.missingNormalGeneratedCount},
             {"missingTangentGeneratedCount", model.importDiagnostics.missingTangentGeneratedCount},
             {"unresolvedTextureCount", model.importDiagnostics.unresolvedTextureCount},
             {"clusteredStaticPrimitiveCount", model.importDiagnostics.clusteredStaticPrimitiveCount},
             {"fallbackPrimitiveCount", model.importDiagnostics.fallbackPrimitiveCount},
             {"skippedMorphPrimitives", model.importDiagnostics.skippedMorphPrimitiveCount},
             {"unsupportedPrimitiveModes", model.importDiagnostics.unsupportedPrimitiveModeCount},
             {"unsupportedFeatures", model.importDiagnostics.unsupportedFeatureCount},
             {"lightmapUvAuthoredValid", model.importDiagnostics.lightmapUvAuthoredValidCount},
             {"lightmapUvMissing", model.importDiagnostics.lightmapUvMissingCount},
             {"lightmapUvInvalid", model.importDiagnostics.lightmapUvInvalidCount},
             {"lightmapUvGenerated", model.importDiagnostics.lightmapUvGeneratedCount},
             {"lightmapUvGenerationFailed", model.importDiagnostics.lightmapUvGenerationFailedCount},
         }},
        {"importMessages", std::move(importMessages)},
        {"formatReport", std::move(formatReport)},
        {"materialAlphaPolicy", std::move(alphaPolicyReport)},
        {"textures", std::move(textures)},
        {"materials", std::move(materials)},
    };

    nlohmann::json clusterJson{
        {"format", "HCMESH"},
        {"profile", ASSETS::IMPORT_POLICY::ToString(clusterProfile)},
        {"ready", hcmeshReady},
        {"message", hcmeshMessage},
    };
    if (clusterSettings != nullptr) {
        clusterJson["cookSettings"] = {
            {"maxTrianglesPerCluster", clusterSettings->maxTrianglesPerCluster},
            {"maxVerticesPerCluster", clusterSettings->maxVerticesPerCluster},
            {"maxClustersPerPage", clusterSettings->maxClustersPerPage},
            {"maxSurfaceLodCount", clusterSettings->maxSurfaceLodCount},
            {"lodTriangleRatios", nlohmann::json::array({
                                      clusterSettings->lod1TriangleRatio,
                                      clusterSettings->lod2TriangleRatio,
                                      clusterSettings->lod3TriangleRatio,
                                      clusterSettings->lod4TriangleRatio,
                                  })},
            {"lodTargetErrors", nlohmann::json::array({
                                    clusterSettings->lod1TargetError,
                                    clusterSettings->lod2TargetError,
                                    clusterSettings->lod3TargetError,
                                    clusterSettings->lod4TargetError,
                                })},
            {"lodMinScreenRadii", nlohmann::json::array({
                                      clusterSettings->lod0MinScreenRadius,
                                      clusterSettings->lod1MinScreenRadius,
                                      clusterSettings->lod2MinScreenRadius,
                                      clusterSettings->lod3MinScreenRadius,
                                  })},
            {"surfacePartitionPolicy", ASSETS::IMPORT_POLICY::ToString(clusterSettings->surfacePartitionPolicy)},
            {"partitionLargeStaticSurfaces", clusterSettings->partitionLargeStaticSurfaces},
            {"largeSurfacePartitionMaxExtent", clusterSettings->largeSurfacePartitionMaxExtent},
            {"largeSurfacePartitionMinTriangles", clusterSettings->largeSurfacePartitionMinTriangles},
            {"largeSurfacePartitionMinTrianglesPerChunk", clusterSettings->largeSurfacePartitionMinTrianglesPerChunk},
            {"largeSurfacePartitionMaxDepth", clusterSettings->largeSurfacePartitionMaxDepth},
            {"lockPartitionBorders", clusterSettings->lockPartitionBorders},
            {"balancePlanarStaticSurfaces", clusterSettings->balancePlanarStaticSurfaces},
            {"partitionMinClusterEstimate", clusterSettings->minPartitionClusterEstimate},
            {"planarSurfaceMinPartitionExtent", clusterSettings->planarStaticSurfaceMinPartitionExtent},
            {"planarSurfaceMinTrianglesPerChunk", clusterSettings->planarStaticSurfaceMinTrianglesPerChunk},
            {"planarSurfaceMaxDepth", clusterSettings->planarStaticSurfaceMaxDepth},
            {"subdivideLargeTriangles", clusterSettings->subdivideLargeStaticTriangles},
            {"largeTriangleMaxEdgeLength", clusterSettings->largeStaticTriangleMaxEdgeLength},
            {"largeTriangleMaxSubdivisions", clusterSettings->largeStaticTriangleMaxSubdivisions},
            {"largeTriangleMaxGeneratedTriangles", clusterSettings->largeStaticTriangleMaxGeneratedTriangles},
            {"maxTriangleInflationRatio", clusterSettings->maxTriangleInflationRatio},
            {"maxVertexInflationRatio", clusterSettings->maxVertexInflationRatio},
            {"minAverageTrianglesPerClusterWarning", clusterSettings->minAverageTrianglesPerClusterWarning},
            {"meshletConeWeight", clusterSettings->meshletConeWeight},
            {"meshletSplitFactor", clusterSettings->meshletSplitFactor},
            {"compactUnderfilledClusters", clusterSettings->compactUnderfilledClusterGroups},
            {"minClusterOccupancyRatio", clusterSettings->minClusterOccupancyRatio},
            {"maxNormalBucketClusterOverhead", clusterSettings->maxNormalBucketClusterOverhead},
            {"clusterMergeNormalMinDot", clusterSettings->clusterMergeNormalMinDot},
            {"normalBucketCoherentGroupMinDot", clusterSettings->normalBucketCoherentGroupMinDot},
            {"normalBucketQualityBonusRatio", clusterSettings->normalBucketQualityBonusRatio},
            {"buildNormalCone", clusterSettings->buildNormalCone},
        };
    }
    if (clusteredReport != nullptr) {
        nlohmann::json messages = nlohmann::json::array();
        for (const std::string& message : clusteredReport->messages) {
            messages.push_back(message);
        }
        clusterJson["summary"] = {
            {"sourceStaticTriangles", clusteredReport->sourceStaticTriangleCount},
            {"sourceStaticVertices", clusteredReport->sourceStaticVertexCount},
            {"surfaces", clusteredReport->surfaceCount},
            {"surfaceLodRanges", clusteredReport->surfaceLodRangeCount},
            {"surfaceSections", clusteredReport->surfaceSectionCount},
            {"clusters", clusteredReport->clusterCount},
            {"pages", clusteredReport->pageCount},
            {"triangles", clusteredReport->triangleCount},
            {"vertices", clusteredReport->vertexCount},
            {"maxVerticesPerCluster", clusteredReport->maxVerticesPerCluster},
            {"normalConeValidClusters", clusteredReport->normalConeValidClusterCount},
            {"normalConeInvalidClusters", clusteredReport->normalConeInvalidClusterCount},
            {"normalConeValidRatio", clusteredReport->clusterCount > 0u
                                         ? static_cast<double>(clusteredReport->normalConeValidClusterCount) /
                                               static_cast<double>(clusteredReport->clusterCount)
                                         : 0.0},
            {"normalConeCutoffLeZero", clusteredReport->normalConeCutoffLeZeroCount},
            {"normalConeCutoffGeOne", clusteredReport->normalConeCutoffGeOneCount},
            {"normalConeAxisInvalid", clusteredReport->normalConeAxisInvalidCount},
            {"avgTrianglesPerCluster", clusteredReport->clusterCount > 0u
                                           ? static_cast<double>(clusteredReport->triangleCount) /
                                                 static_cast<double>(clusteredReport->clusterCount)
                                           : 0.0},
            {"avgVerticesPerCluster", clusteredReport->clusterCount > 0u
                                          ? static_cast<double>(clusteredReport->vertexCount) /
                                                static_cast<double>(clusteredReport->clusterCount)
                                          : 0.0},
            {"skippedSkinnedPrimitives", clusteredReport->skippedSkinnedPrimitiveCount},
            {"skippedMorphPrimitives", clusteredReport->skippedMorphPrimitiveCount},
            {"skippedInvalidPrimitives", clusteredReport->skippedInvalidPrimitiveCount},
            {"unsupportedPrimitiveModes", clusteredReport->unsupportedPrimitiveModeCount},
            {"unsupportedFeatures", clusteredReport->unsupportedFeatureCount},
            {"partitionedSurfaces", clusteredReport->partitionedSurfaceCount},
            {"partitionedSurfaceChunks", clusteredReport->partitionedSurfaceChunkCount},
            {"rejectedPartitionedSurfaces", clusteredReport->rejectedPartitionedSurfaceCount},
            {"normalPartitionedSurfaces", clusteredReport->normalPartitionedSurfaceCount},
            {"normalPartitionedSurfaceChunks", clusteredReport->normalPartitionedChunkCount},
            {"acceptedNormalBucketGroups", clusteredReport->acceptedNormalBucketGroupCount},
            {"rejectedNormalBucketGroups", clusteredReport->rejectedNormalBucketGroupCount},
            {"compactedClusterGroups", clusteredReport->compactedClusterGroupCount},
            {"mergedClusterGroups", clusteredReport->mergedClusterGroupCount},
            {"planarPartitionedSurfaces", clusteredReport->planarPartitionedSurfaceCount},
            {"planarPartitionedSurfaceChunks", clusteredReport->planarPartitionedChunkCount},
            {"planarPartitionCoarsenedSurfaces", clusteredReport->planarPartitionCoarsenedSurfaceCount},
            {"subdividedSurfaces", clusteredReport->subdividedSurfaceCount},
            {"subdividedSourceTriangles", clusteredReport->subdividedSourceTriangleCount},
            {"subdividedOutputTriangles", clusteredReport->subdividedOutputTriangleCount},
            {"planarSubdivisionSkippedSurfaces", clusteredReport->planarSubdivisionSkippedSurfaceCount},
            {"planarSubdivisionCoarsenedSurfaces", clusteredReport->planarSubdivisionCoarsenedSurfaceCount},
            {"singleTriangleClusters", clusteredReport->singleTriangleClusterCount},
            {"lowTriangleClusters", clusteredReport->lowTriangleClusterCount},
            {"maxTrianglesPerClusterObserved", clusteredReport->maxTrianglesPerClusterObserved},
            {"packedGeometryBytes", clusteredReport->packedGeometryByteSize},
            {"packedMetadataBytes", clusteredReport->packedMetadataByteSize},
            {"packedTotalBytes", clusteredReport->packedTotalByteSize},
            {"fallbackIndexBytes", clusteredReport->fallbackIndexByteSize},
            {"meshletPrimitiveBytes", clusteredReport->meshletPrimitiveByteSize},
            {"packedVertexPositionBytes", clusteredReport->packedVertexPositionByteSize},
            {"packedVertexAttributeBytes", clusteredReport->packedVertexAttributeByteSize},
            {"packedSkinVertexBytes", clusteredReport->packedSkinVertexByteSize},
            {"averageTrianglesPerCluster", clusteredReport->averageTrianglesPerCluster},
            {"triangleInflationRatio", clusteredReport->triangleInflationRatio},
            {"vertexInflationRatio", clusteredReport->vertexInflationRatio},
            {"triangleBudgetExceeded", clusteredReport->triangleBudgetExceeded},
            {"vertexBudgetExceeded", clusteredReport->vertexBudgetExceeded},
            {"clusterOccupancyWarning", clusteredReport->clusterOccupancyWarning},
            {"normalConeCutoffMin", clusteredReport->normalConeCutoffMin},
            {"normalConeCutoffAverage", clusteredReport->normalConeCutoffAverage},
            {"normalConeCutoffMax", clusteredReport->normalConeCutoffMax},
        };
        clusterJson["messages"] = std::move(messages);
    }
    if (clusteredValidation != nullptr) {
        nlohmann::json validationMessages = nlohmann::json::array();
        for (const std::string& message : clusteredValidation->messages) {
            validationMessages.push_back(message);
        }
        clusterJson["validation"] = {
            {"valid", clusteredValidation->valid},
            {"invalidSurfaces", clusteredValidation->invalidSurfaceCount},
            {"invalidClusters", clusteredValidation->invalidClusterCount},
            {"invalidPages", clusteredValidation->invalidPageCount},
            {"invalidBounds", clusteredValidation->invalidBoundsCount},
            {"invalidMaterials", clusteredValidation->invalidMaterialCount},
            {"messages", std::move(validationMessages)},
        };
    }
    diagnostics["clusteredGeometry"] = std::move(clusterJson);
    return diagnostics;
}

} // namespace HIKARI::MODEL_IMPORT_INTERNAL
