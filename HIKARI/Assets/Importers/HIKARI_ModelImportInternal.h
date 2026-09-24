#pragma once
#include "Assets/Importers/HIKARI_ModelImporter.h"
#include "Assets/Geometry/HIKARI_ClusteredGeometryCooker.h"
#include "Assets/Geometry/HIKARI_ClusteredGeometryValidator.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#include <json.hpp>

namespace HIKARI::MODEL_IMPORT_INTERNAL {
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

nlohmann::json BuildModelDiagnostics(
    const ModelAsset& model, const std::vector<TextureCookDiagnostic>& textureDiagnostics, int htexReferenceCount,
    int fallbackTextureCount, const RENDER3D::CLUSTER::ClusteredGeometryBuildReport* clusteredReport,
    const ASSETS::GEOMETRY::ClusteredGeometryValidationResult* clusteredValidation,
    ModelGeometryCookProfile clusterProfile, const ASSETS::GEOMETRY::ClusteredGeometryCookSettings* clusterSettings,
    bool hcmeshReady, const std::string& hcmeshMessage, const MaterialAlphaPolicyStats& alphaPolicyStats);
bool InspectSourceTextureAlpha(const std::filesystem::path& absoluteTexturePath,
                               TextureCookDiagnostic& inOutDiagnostic);
bool ResolveTextureToHtex(const std::filesystem::path& projectRoot, const std::filesystem::path& sourceMetaRoot,
                          const std::filesystem::path& libraryRoot, TextureAsset3D& texture,
                          AssetDependencyDesc& outDependency, bool& outHasDependency,
                          TextureCookDiagnostic& inOutDiagnostic);
MaterialAlphaPolicyStats ApplyCanonicalBaseColorAlphaMaterialPolicy(
    ModelAsset& model, const std::vector<TextureCookDiagnostic>& textureDiagnostics);
void BuildCanonicalMetallicRoughnessTextures(ModelAsset& model, const AssetImportContext& context,
                                             std::vector<AssetArtifactDesc>& artifacts);
void ApplyCanonicalSpecularPolicy(ModelAsset& model);
} // namespace HIKARI::MODEL_IMPORT_INTERNAL
