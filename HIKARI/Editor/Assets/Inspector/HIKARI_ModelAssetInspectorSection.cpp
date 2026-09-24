#include "Editor/Assets/Inspector/HIKARI_AssetInspectorPanel.h"

#include "Core/Text/HIKARI_Localization.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

#include <json.hpp>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetImportState.h"
#include "Assets/Importers/Policy/HIKARI_TextureImportPolicy.h"
#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Editor/Assets/HIKARI_AssetImportSettingsEditor.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/Widgets/HIKARI_MaterialTextureSlotWidget.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif
#include "Editor/Assets/Inspector/HIKARI_AssetInspectorInternal.h"

namespace HIKARI::EDITOR::ASSET_INSPECTOR {

#if defined(HIKARI_WITH_EDITOR)
std::string ToClusteredGeometryArtifactStateText(ClusteredGeometryArtifactState state) {
    switch (state) {
    case ClusteredGeometryArtifactState::Exists: return TEXT::Localize("ui.modelDiagnostics.stateExists");
    case ClusteredGeometryArtifactState::Valid: return TEXT::Localize("ui.modelDiagnostics.stateValid");
    case ClusteredGeometryArtifactState::Invalid: return TEXT::Localize("ui.modelDiagnostics.stateInvalid");
    case ClusteredGeometryArtifactState::Outdated: return TEXT::Localize("ui.modelDiagnostics.stateOutdated");
    case ClusteredGeometryArtifactState::Missing:
    default: return TEXT::Localize("ui.modelDiagnostics.stateMissing");
    }
}

void DrawClusteredGeometryArtifactInfo(const ClusteredGeometryArtifactInfo& info) {
    ImGui::SeparatorText(TEXT::Localize("ui.modelDiagnostics.clusteredArtifact").c_str());
    ImGui::Text(TEXT::Localize("ui.modelDiagnostics.stateFormat").c_str(),
                ToClusteredGeometryArtifactStateText(info.state).c_str());
    ImGui::Text(TEXT::Localize("ui.modelDiagnostics.pathFormat").c_str(),
                info.path.empty() ? TEXT::Localize("ui.common.none").c_str() : info.path.generic_string().c_str());
    ImGui::Text(TEXT::Localize("ui.modelDiagnostics.messageFormat").c_str(),
                info.message.empty() ? TEXT::Localize("ui.common.none").c_str() : info.message.c_str());
    if (info.state == ClusteredGeometryArtifactState::Invalid || info.state == ClusteredGeometryArtifactState::Valid ||
        info.state == ClusteredGeometryArtifactState::Outdated) {
        ImGui::Text(TEXT::Localize("ui.modelDiagnostics.validationFormat").c_str(),
                    info.validationValid ? TEXT::Localize("ui.modelDiagnostics.stateValid").c_str()
                                         : TEXT::Localize("ui.modelDiagnostics.stateInvalid").c_str());
        ImGui::Text(TEXT::Localize("ui.modelDiagnostics.invalidScpbmFormat").c_str(), info.invalidSurfaceCount,
                    info.invalidClusterCount, info.invalidPageCount, info.invalidBoundsCount,
                    info.invalidMaterialCount);
    }
    if (!info.validationMessages.empty() &&
        ImGui::TreeNode(TEXT::Localize("ui.modelDiagnostics.validationMessages").c_str())) {
        for (const std::string& message : info.validationMessages) {
            ImGui::BulletText("%s", message.c_str());
        }
        ImGui::TreePop();
    }
}

void DrawModelDiagnostics(const ClusteredGeometryArtifactInfo& info, const nlohmann::json& report) {
    DrawClusteredGeometryArtifactInfo(info);

    if (!report.is_object() || !report.contains("diagnostics") || !report["diagnostics"].is_object()) {
        ImGui::TextDisabled("%s", TEXT::Localize("ui.modelDiagnostics.awaitImport").c_str());
        return;
    }

    const nlohmann::json& diagnostics = report["diagnostics"];
    if (diagnostics.contains("summary") && diagnostics["summary"].is_object()) {
        const nlohmann::json& summary = diagnostics["summary"];
        ImGui::SeparatorText(TEXT::Localize("ui.modelDiagnostics.hmodelSummary").c_str());
        ImGui::Text(TEXT::Localize("ui.modelDiagnostics.meshesFormat").c_str(), summary.value("meshes", 0),
                    summary.value("primitives", 0), summary.value("materials", 0), summary.value("textures", 0));
        ImGui::Text(TEXT::Localize("ui.modelDiagnostics.verticesFormat").c_str(), summary.value("staticVertices", 0),
                    summary.value("skinnedVertices", 0), summary.value("indices", 0));
        ImGui::Text(TEXT::Localize("ui.modelDiagnostics.htexRefsFormat").c_str(), summary.value("htexRefs", 0),
                    summary.value("fallbackTextures", 0));
        ImGui::Text(TEXT::Localize("ui.modelDiagnostics.generatedNtFormat").c_str(),
                    summary.value("missingNormalGeneratedCount", 0), summary.value("missingTangentGeneratedCount", 0),
                    summary.value("unresolvedTextureCount", 0));
        ImGui::Text(TEXT::Localize("ui.modelDiagnostics.clusterPrimFormat").c_str(),
                    summary.value("clusteredStaticPrimitiveCount", 0), summary.value("fallbackPrimitiveCount", 0));
    }

    if (diagnostics.contains("formatReport") && diagnostics["formatReport"].is_object()) {
        const nlohmann::json& formatReport = diagnostics["formatReport"];
        ImGui::SeparatorText(TEXT::Localize("ui.modelDiagnostics.sourceReport").c_str());
        ImGui::Text(TEXT::Localize("ui.modelDiagnostics.sourceFormat").c_str(),
                    formatReport.value("sourceFormat", "<unknown>").c_str());
        ImGui::Text(TEXT::Localize("ui.modelDiagnostics.objectGroupFormat").c_str(),
                    formatReport.value("objectCount", 0), formatReport.value("groupCount", 0),
                    formatReport.value("triangulatedPolygonCount", 0));
        ImGui::Text(TEXT::Localize("ui.modelDiagnostics.unsupportedFormat").c_str(),
                    formatReport.value("unsupportedPrimitiveModeCount", 0),
                    formatReport.value("unsupportedFeatureCount", 0));
        ImGui::Text(TEXT::Localize("ui.modelDiagnostics.generatedNtFormat2").c_str(),
                    formatReport.value("missingNormalGeneratedCount", 0),
                    formatReport.value("missingTangentGeneratedCount", 0));
        ImGui::Text(TEXT::Localize("ui.modelDiagnostics.clusterPrimFormat").c_str(),
                    formatReport.value("clusteredStaticPrimitiveCount", 0),
                    formatReport.value("fallbackPrimitiveCount", 0));
        if (formatReport.contains("unsupportedExtensions") && formatReport["unsupportedExtensions"].is_array() &&
            !formatReport["unsupportedExtensions"].empty() &&
            ImGui::TreeNode(TEXT::Localize("ui.modelDiagnostics.unsupportedExtensions").c_str())) {
            for (const nlohmann::json& extension : formatReport["unsupportedExtensions"]) {
                if (extension.is_string()) {
                    ImGui::BulletText("%s", extension.get<std::string>().c_str());
                }
            }
            ImGui::TreePop();
        }
    }

    if (diagnostics.contains("materialAlphaPolicy") && diagnostics["materialAlphaPolicy"].is_object()) {
        const nlohmann::json& alpha = diagnostics["materialAlphaPolicy"];
        ImGui::SeparatorText(TEXT::Localize("ui.modelDiagnostics.materialClassification").c_str());
        ImGui::Text(TEXT::Localize("ui.modelDiagnostics.opaqueMaskBlendFormat").c_str(), alpha.value("opaque", 0),
                    alpha.value("mask", 0), alpha.value("blend", 0));
        ImGui::Text(TEXT::Localize("ui.modelDiagnostics.normalizedFormat").c_str(),
                    alpha.value("normalizedToOpaque", 0), alpha.value("normalizedToMask", 0),
                    alpha.value("normalizedToBlend", 0));
        const int opticalFallback = alpha.value("opticalOpacityFallback", 0);
        if (opticalFallback > 0) {
            ImGui::Text(TEXT::Localize("ui.modelDiagnostics.recoveredOpticalFormat").c_str(), opticalFallback);
            ImGui::TextDisabled("%s", TEXT::Localize("ui.modelDiagnostics.recoveredOpticalHint").c_str());
        }
    }

    if (diagnostics.contains("importMessages") && diagnostics["importMessages"].is_array() &&
        !diagnostics["importMessages"].empty() &&
        ImGui::TreeNode(TEXT::Localize("ui.modelDiagnostics.advancedImport").c_str())) {
        for (const nlohmann::json& message : diagnostics["importMessages"]) {
            if (message.is_string()) {
                ImGui::BulletText("%s", message.get<std::string>().c_str());
            }
        }
        ImGui::TreePop();
    }

    if (diagnostics.contains("clusteredGeometry") && diagnostics["clusteredGeometry"].is_object()) {
        const nlohmann::json& cluster = diagnostics["clusteredGeometry"];
        ImGui::SeparatorText(TEXT::Localize("ui.modelDiagnostics.hcmesh").c_str());
        ImGui::Text(TEXT::Localize("ui.modelDiagnostics.readyFormat").c_str(),
                    cluster.value("ready", false) ? TEXT::Localize("ui.common.yes").c_str()
                                                  : TEXT::Localize("ui.common.no").c_str());
        ImGui::Text(TEXT::Localize("ui.modelDiagnostics.profileFormat").c_str(),
                    cluster.value("profile", "Scene").c_str());
        ImGui::Text(TEXT::Localize("ui.modelDiagnostics.messageFormat").c_str(), cluster.value("message", "").c_str());
        if (cluster.contains("cookSettings") && cluster["cookSettings"].is_object()) {
            const nlohmann::json& cook = cluster["cookSettings"];
            ImGui::Text(TEXT::Localize("ui.modelDiagnostics.lodPartitionFormat").c_str(),
                        cook.value("maxSurfaceLodCount", 0),
                        cook.value("partitionLargeStaticSurfaces", false) ? TEXT::Localize("ui.common.yes").c_str()
                                                                          : TEXT::Localize("ui.common.no").c_str());
            ImGui::Text(TEXT::Localize("ui.modelDiagnostics.partitionPolicyFormat").c_str(),
                        cook.value("surfacePartitionPolicy", "SceneStatic").c_str());
            ImGui::Text(TEXT::Localize("ui.modelDiagnostics.partitionExtentFormat").c_str(),
                        cook.value("largeSurfacePartitionMaxExtent", 0.0f),
                        cook.value("lockPartitionBorders", false) ? TEXT::Localize("ui.common.yes").c_str()
                                                                  : TEXT::Localize("ui.common.no").c_str());
        }
        if (cluster.contains("summary") && cluster["summary"].is_object()) {
            const nlohmann::json& summary = cluster["summary"];
            ImGui::Text(TEXT::Localize("ui.modelDiagnostics.surfacesFormat").c_str(), summary.value("surfaces", 0),
                        summary.value("surfaceLodRanges", 0), summary.value("clusters", 0), summary.value("pages", 0));
            ImGui::Text(TEXT::Localize("ui.modelDiagnostics.trianglesFormat").c_str(), summary.value("triangles", 0),
                        summary.value("vertices", 0));
            ImGui::Text(TEXT::Localize("ui.modelDiagnostics.avgTriVertFormat").c_str(),
                        summary.value("avgTrianglesPerCluster", 0.0), summary.value("avgVerticesPerCluster", 0.0));
            ImGui::Text(TEXT::Localize("ui.modelDiagnostics.skippedFormat").c_str(),
                        summary.value("skippedSkinnedPrimitives", 0), summary.value("skippedMorphPrimitives", 0),
                        summary.value("skippedInvalidPrimitives", 0));
        }
        if (cluster.contains("validation") && cluster["validation"].is_object()) {
            const nlohmann::json& validation = cluster["validation"];
            ImGui::Text(TEXT::Localize("ui.modelDiagnostics.validationFormat").c_str(),
                        validation.value("valid", false) ? TEXT::Localize("ui.modelDiagnostics.stateValid").c_str()
                                                         : TEXT::Localize("ui.modelDiagnostics.stateInvalid").c_str());
            ImGui::Text(TEXT::Localize("ui.modelDiagnostics.invalidScpbmFormat").c_str(),
                        validation.value("invalidSurfaces", 0), validation.value("invalidClusters", 0),
                        validation.value("invalidPages", 0), validation.value("invalidBounds", 0),
                        validation.value("invalidMaterials", 0));
        }
    }

    if (diagnostics.contains("collisionGeometry") && diagnostics["collisionGeometry"].is_object()) {
        const nlohmann::json& collision = diagnostics["collisionGeometry"];
        ImGui::SeparatorText(TEXT::Localize("ui.modelDiagnostics.modelCollision").c_str());
        ImGui::Text(TEXT::Localize("ui.modelDiagnostics.runtimeArtifactFormat").c_str(),
                    collision.value("ready", false) ? TEXT::Localize("ui.modelDiagnostics.stateReady").c_str()
                                                    : TEXT::Localize("ui.modelDiagnostics.stateNone").c_str());
        ImGui::Text(TEXT::Localize("ui.modelDiagnostics.shapesFormat").c_str(), collision.value("shapeCount", 0));
        ImGui::Text(TEXT::Localize("ui.modelDiagnostics.messageFormat").c_str(),
                    collision.value("message", "").c_str());
        ImGui::TextDisabled("%s", TEXT::Localize("ui.modelDiagnostics.authorInCollision").c_str());
    }

    if (diagnostics.contains("textures") && diagnostics["textures"].is_array() &&
        ImGui::BeginTable("ModelDiagnosticTextures", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn(TEXT::Localize("ui.modelDiagnostics.index").c_str(), ImGuiTableColumnFlags_WidthFixed,
                                48.0f);
        ImGui::TableSetupColumn("HTEX", ImGuiTableColumnFlags_WidthFixed, 54.0f);
        ImGui::TableSetupColumn(TEXT::Localize("ui.modelDiagnostics.source").c_str());
        ImGui::TableSetupColumn(TEXT::Localize("ui.modelDiagnostics.runtime").c_str());
        ImGui::TableHeadersRow();

        for (const nlohmann::json& texture : diagnostics["textures"]) {
            if (!texture.is_object()) {
                continue;
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", texture.value("index", -1));
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(texture.value("htexReady", false) ? TEXT::Localize("ui.common.yes").c_str()
                                                                     : TEXT::Localize("ui.common.no").c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(texture.value("sourcePath", "").c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(texture.value("runtimePath", "").c_str());
        }

        ImGui::EndTable();
    }

    if (diagnostics.contains("materials") && diagnostics["materials"].is_array() &&
        ImGui::BeginTable("ModelDiagnosticMaterials", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn(TEXT::Localize("ui.modelDiagnostics.material").c_str());
        ImGui::TableSetupColumn(TEXT::Localize("ui.modelDiagnostics.metallic").c_str(),
                                ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupColumn(TEXT::Localize("ui.modelDiagnostics.roughness").c_str(),
                                ImGuiTableColumnFlags_WidthFixed, 80.0f);
        ImGui::TableSetupColumn(TEXT::Localize("ui.modelDiagnostics.alpha").c_str(), ImGuiTableColumnFlags_WidthFixed,
                                70.0f);
        ImGui::TableHeadersRow();

        for (const nlohmann::json& material : diagnostics["materials"]) {
            if (!material.is_object()) {
                continue;
            }
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(material.value("name", "<unnamed>").c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.3f", material.value("metallicFactor", 0.0f));
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.3f", material.value("roughnessFactor", 1.0f));
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(material.value("alphaMode", "Opaque").c_str());
        }

        ImGui::EndTable();
    }
}
#endif

} // namespace HIKARI::EDITOR::ASSET_INSPECTOR
