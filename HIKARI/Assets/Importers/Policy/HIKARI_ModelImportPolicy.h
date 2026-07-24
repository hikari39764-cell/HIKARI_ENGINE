#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "Assets/Geometry/HIKARI_ClusteredGeometryCooker.h"
#include "Assets/HIKARI_AssetTypes.h"

namespace HIKARI::ASSETS::IMPORT_POLICY {

    struct ModelImportPolicy {
        bool cookModel = true;
        bool loadMaterials = true;
        bool loadTextures = true;
        ModelImporterKind importer = ModelImporterKind::Gltf;
        ModelClusterCookOptions clusterOptions{};
        GEOMETRY::ClusteredGeometryCookSettings clusteredGeometryCookSettings{};
    };

    ModelImportPolicy ResolveModelImportPolicy(
        const std::filesystem::path& sourcePath,
        std::string_view importSettingsJson);
    std::string MakeDefaultModelImportSettingsJson(
        const std::filesystem::path& sourcePath);

    std::string_view ToString(
        ModelGeometryCookProfile profile) noexcept;
    std::string_view ToString(
        GEOMETRY::SurfacePartitionPolicy policy) noexcept;

} // namespace HIKARI::ASSETS::IMPORT_POLICY
