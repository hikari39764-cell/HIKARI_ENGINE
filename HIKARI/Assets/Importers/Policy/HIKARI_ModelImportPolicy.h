#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "Assets/Geometry/HIKARI_ClusteredGeometryCooker.h"
#include "RenderContracts/Asset/HIKARI_AssetTypes.h"
#include "Assets/Lighting/HIKARI_LightmapUvGeneration.h"

namespace HIKARI::ASSETS::IMPORT_POLICY {

struct ModelImportPolicy {
    ModelImporterKind importer = ModelImporterKind::Gltf;
    ModelClusterCookOptions clusterOptions{};
    GEOMETRY::ClusteredGeometryCookSettings clusteredGeometryCookSettings{};
    LIGHTING::LightmapUvGenerationSettings lightmapUv{};
};

ModelImportPolicy ResolveModelImportPolicy(const std::filesystem::path& sourcePath,
                                           std::string_view importSettingsJson);
std::string MakeDefaultModelImportSettingsJson(const std::filesystem::path& sourcePath);

std::string_view ToString(ModelGeometryCookProfile profile) noexcept;
std::string_view ToString(GEOMETRY::SurfacePartitionPolicy policy) noexcept;

} // namespace HIKARI::ASSETS::IMPORT_POLICY
