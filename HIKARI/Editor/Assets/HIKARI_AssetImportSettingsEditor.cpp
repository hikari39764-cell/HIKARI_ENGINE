#include "Editor/Assets/HIKARI_AssetImportSettingsEditor.h"

#include <algorithm>
#include <array>
#include <string>

#include <json.hpp>

#include "Assets/Importers/Policy/HIKARI_ModelImportPolicy.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR::ASSET_IMPORT_SETTINGS {

    namespace {

        nlohmann::json& EnsureClusterGeometrySettings(
            nlohmann::json& settings) {

            if (!settings.contains("clusterGeometry") ||
                !settings["clusterGeometry"].is_object()) {
                settings["clusterGeometry"] = nlohmann::json::object();
            }
            return settings["clusterGeometry"];
        }

    } // namespace

    bool DrawModelClusteredGeometryCookSettings(
        nlohmann::json& settings,
        bool drawSectionHeader) {

#if !defined(HIKARI_WITH_EDITOR)
        (void)settings;
        (void)drawSectionHeader;
        return false;
#else
        const std::array profileItems = {
            ASSETS::IMPORT_POLICY::ToString(
                ModelGeometryCookProfile::Scene).data(),
            ASSETS::IMPORT_POLICY::ToString(
                ModelGeometryCookProfile::Character).data(),
        };
        nlohmann::json& cluster =
            EnsureClusterGeometrySettings(settings);
        bool dirty = false;

        if (drawSectionHeader) {
            ImGui::SeparatorText("Cluster Geometry");
        }

        bool enabled = cluster.value("enabled", true);
        if (ImGui::Checkbox("Build HCMESH", &enabled)) {
            cluster["enabled"] = enabled;
            dirty = true;
        }

        const std::string profileValue =
            cluster.value("profile", std::string(profileItems[0]));
        int profile = profileValue == profileItems[1] ? 1 : 0;
        if (ImGui::Combo(
            "Cook Profile",
            &profile,
            profileItems.data(),
            static_cast<int>(profileItems.size()))) {
            cluster["profile"] = profileItems[profile];
            cluster["partitionLargeSurfaces"] = true;
            cluster["largeSurfaceTargetExtent"] =
                profile == 1 ? 1.25f : 3.0f;
            cluster["partitionMinClusterEstimate"] =
                profile == 1 ? 4 : 16;
            cluster["compactUnderfilledClusters"] = true;
            cluster["minClusterOccupancyRatio"] = 0.75f;
            cluster["maxNormalBucketClusterOverhead"] = 1.10f;
            cluster["clusterMergeNormalMinDot"] = 0.20f;
            cluster["normalBucketCoherentGroupMinDot"] = 0.35f;
            cluster["normalBucketQualityBonusRatio"] = 0.15f;
            dirty = true;
        }

        const bool characterProfile = profile == 1;
        const float defaultPartitionExtent =
            characterProfile ? 1.25f : 3.0f;

        int lodCount = cluster.value("maxLodCount", 5);
        if (ImGui::InputInt("LOD Count", &lodCount)) {
            cluster["maxLodCount"] =
                (std::max)(1, (std::min)(lodCount, 5));
            dirty = true;
        }

        float qualityBias = cluster.value("lodQualityBias", 1.0f);
        if (ImGui::InputFloat("LOD Quality Bias", &qualityBias)) {
            cluster["lodQualityBias"] =
                (std::max)(0.50f, (std::min)(qualityBias, 4.0f));
            dirty = true;
        }

        bool partition =
            cluster.value("partitionLargeSurfaces", true);
        if (ImGui::Checkbox("Partition Large Surfaces", &partition)) {
            cluster["partitionLargeSurfaces"] = partition;
            dirty = true;
        }

        float extent = cluster.value(
            "largeSurfaceTargetExtent",
            defaultPartitionExtent);
        if (ImGui::InputFloat("Partition Target Extent", &extent)) {
            cluster["largeSurfaceTargetExtent"] =
                (std::max)(
                    characterProfile ? 1.0f : 2.0f,
                    (std::min)(extent, 64.0f));
            dirty = true;
        }

        bool lockBorders = cluster.value("lockPartitionBorders", true);
        if (ImGui::Checkbox("Lock Partition Borders", &lockBorders)) {
            cluster["lockPartitionBorders"] = lockBorders;
            dirty = true;
        }

        if (dirty) {
            settings["meshFormat"] = std::string(
                ASSETS::SEMANTICS::ToString(
                    CookedAssetFormat::HCMESH));
        }
        return dirty;
#endif
    }

} // namespace HIKARI::EDITOR::ASSET_IMPORT_SETTINGS
