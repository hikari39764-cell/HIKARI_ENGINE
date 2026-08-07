#include "Editor/Assets/HIKARI_AssetImportSettingsEditor.h"

#include "Core/Text/HIKARI_Localization.h"
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

        nlohmann::json& EnsureLightmapUvSettings(
            nlohmann::json& settings) {

            if (!settings.contains("lightmapUv") ||
                !settings["lightmapUv"].is_object()) {
                settings["lightmapUv"] =
                    nlohmann::json::object();
            }
            return settings["lightmapUv"];
        }

    } // namespace

    bool DrawModelImportSettings(
        nlohmann::json& settings) {

#if !defined(HIKARI_WITH_EDITOR)
        (void)settings;
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

        nlohmann::json& lightmapUv =
            EnsureLightmapUvSettings(settings);
        const std::array<const char*, 3> uvModes{
            "Preserve",
            "GenerateIfMissingOrInvalid",
            "ForceRegenerate",
        };
        const std::string currentUvMode =
            lightmapUv.value(
                "mode",
                std::string(uvModes[1]));
        int selectedUvMode = 1;
        for (size_t index = 0u;
            index < uvModes.size();
            ++index) {
            if (currentUvMode == uvModes[index]) {
                selectedUvMode =
                    static_cast<int>(index);
                break;
            }
        }

        ImGui::SeparatorText(
            TEXT::Localize(
                "ui.importSettings.lightmapUv").c_str());
        if (ImGui::Combo(
                TEXT::Localize(
                    "ui.importSettings.uv1Policy").c_str(),
                &selectedUvMode,
                uvModes.data(),
                static_cast<int>(uvModes.size()))) {
            lightmapUv["mode"] =
                uvModes[
                    static_cast<size_t>(
                        selectedUvMode)];
            dirty = true;
        }
        ImGui::TextDisabled(
            selectedUvMode == 0
            ? TEXT::Localize(
                "ui.importSettings.uv1Keep").c_str()
            : TEXT::Localize(
                "ui.importSettings.uv1Generate").c_str());
        if (selectedUvMode != 0 &&
            ImGui::TreeNode(
                TEXT::Localize(
                    "ui.importSettings.advancedLightmapUv").c_str())) {
            int resolution = lightmapUv.value(
                "resolution",
                64);
            if (ImGui::InputInt(
                    TEXT::Localize(
                        "ui.importSettings.uvResolution").c_str(),
                    &resolution)) {
                lightmapUv["resolution"] =
                    std::clamp(
                        resolution,
                        64,
                        8192);
                dirty = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "%s",
                    TEXT::Localize(
                        "ui.importSettings.uvResolutionTooltip").c_str());
            }
            int padding = lightmapUv.value(
                "padding",
                8);
            if (ImGui::InputInt(
                    TEXT::Localize(
                        "ui.importSettings.chartPadding").c_str(),
                    &padding)) {
                lightmapUv["padding"] =
                    std::clamp(
                        padding,
                        1,
                        64);
                dirty = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "%s",
                    TEXT::Localize(
                        "ui.importSettings.chartPaddingTooltip").c_str());
            }
            ImGui::TreePop();
        }

        ImGui::SeparatorText(
            TEXT::Localize(
                "ui.importSettings.clusterGeometry").c_str());
        bool enabled = cluster.value("enabled", true);
        if (ImGui::Checkbox(
                TEXT::Localize(
                    "ui.importSettings.buildHcmesh").c_str(),
                &enabled)) {
            cluster["enabled"] = enabled;
            dirty = true;
        }

        const std::string profileValue =
            cluster.value("profile", std::string(profileItems[0]));
        int profile = profileValue == profileItems[1] ? 1 : 0;
        if (ImGui::Combo(
            TEXT::Localize(
                "ui.importSettings.cookProfile").c_str(),
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
            lightmapUv["mode"] =
                profile == 1
                ? "Preserve"
                : "GenerateIfMissingOrInvalid";
            dirty = true;
        }

        const bool characterProfile = profile == 1;
        const float defaultPartitionExtent =
            characterProfile ? 1.25f : 3.0f;

        int lodCount = cluster.value("maxLodCount", 5);
        if (ImGui::InputInt(
                TEXT::Localize(
                    "ui.importSettings.lodCount").c_str(),
                &lodCount)) {
            cluster["maxLodCount"] =
                (std::max)(1, (std::min)(lodCount, 5));
            dirty = true;
        }

        float qualityBias = cluster.value("lodQualityBias", 1.0f);
        if (ImGui::InputFloat(
                TEXT::Localize(
                    "ui.importSettings.lodQualityBias").c_str(),
                &qualityBias)) {
            cluster["lodQualityBias"] =
                (std::max)(0.50f, (std::min)(qualityBias, 4.0f));
            dirty = true;
        }

        bool partition =
            cluster.value("partitionLargeSurfaces", true);
        if (ImGui::Checkbox(
                TEXT::Localize(
                    "ui.importSettings.partitionLargeSurfaces").c_str(),
                &partition)) {
            cluster["partitionLargeSurfaces"] = partition;
            dirty = true;
        }

        float extent = cluster.value(
            "largeSurfaceTargetExtent",
            defaultPartitionExtent);
        if (ImGui::InputFloat(
                TEXT::Localize(
                    "ui.importSettings.partitionTargetExtent").c_str(),
                &extent)) {
            cluster["largeSurfaceTargetExtent"] =
                (std::max)(
                    characterProfile ? 1.0f : 2.0f,
                    (std::min)(extent, 64.0f));
            dirty = true;
        }

        bool lockBorders = cluster.value("lockPartitionBorders", true);
        if (ImGui::Checkbox(
                TEXT::Localize(
                    "ui.importSettings.lockPartitionBorders").c_str(),
                &lockBorders)) {
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
