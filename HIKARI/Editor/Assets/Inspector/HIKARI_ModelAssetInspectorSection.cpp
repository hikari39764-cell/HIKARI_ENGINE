#include "Editor/Assets/Inspector/HIKARI_AssetInspectorPanel.h"

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
const char *
ToClusteredGeometryArtifactStateText(ClusteredGeometryArtifactState state) {
  switch (state) {
  case ClusteredGeometryArtifactState::Exists:
    return "Exists";
  case ClusteredGeometryArtifactState::Valid:
    return "Valid";
  case ClusteredGeometryArtifactState::Invalid:
    return "Invalid";
  case ClusteredGeometryArtifactState::Outdated:
    return "Outdated";
  case ClusteredGeometryArtifactState::Missing:
  default:
    return "Missing";
  }
}

void DrawClusteredGeometryArtifactInfo(const AssetDatabase &assetDatabase,
                                       const AssetRecord &record) {
  const ClusteredGeometryArtifactInfo info =
      assetDatabase.GetClusteredGeometryArtifactInfo(record);
  ImGui::SeparatorText("ClusteredGeometry Artifact");
  ImGui::Text("State: %s", ToClusteredGeometryArtifactStateText(info.state));
  ImGui::Text("Path: %s", info.path.empty()
                              ? "<none>"
                              : info.path.generic_string().c_str());
  ImGui::Text("Message: %s",
              info.message.empty() ? "<none>" : info.message.c_str());
  if (info.state == ClusteredGeometryArtifactState::Invalid ||
      info.state == ClusteredGeometryArtifactState::Valid ||
      info.state == ClusteredGeometryArtifactState::Outdated) {
    ImGui::Text("Validation: %s", info.validationValid ? "Valid" : "Invalid");
    ImGui::Text("Invalid S/C/P/B/M: %u / %u / %u / %u / %u",
                info.invalidSurfaceCount, info.invalidClusterCount,
                info.invalidPageCount, info.invalidBoundsCount,
                info.invalidMaterialCount);
  }
  if (!info.validationMessages.empty() &&
      ImGui::TreeNode("Validation Messages")) {
    for (const std::string &message : info.validationMessages) {
      ImGui::BulletText("%s", message.c_str());
    }
    ImGui::TreePop();
  }
}

void DrawModelDiagnostics(const AssetDatabase &assetDatabase,
                          const AssetRecord &record) {
  DrawClusteredGeometryArtifactInfo(assetDatabase, record);

  nlohmann::json report;
  if (!SERIALIZATION::JSON::ReadJsonFile(
          record.importedDirectory / "import_report.json", report) ||
      !report.is_object() || !report.contains("diagnostics") ||
      !report["diagnostics"].is_object()) {
    ImGui::TextDisabled(
        "Model diagnostics are written after a successful HMODEL import");
    return;
  }

  const nlohmann::json &diagnostics = report["diagnostics"];
  if (diagnostics.contains("summary") && diagnostics["summary"].is_object()) {
    const nlohmann::json &summary = diagnostics["summary"];
    ImGui::SeparatorText("HMODEL Summary");
    ImGui::Text("Meshes: %d  Primitives: %d  Materials: %d  Textures: %d",
                summary.value("meshes", 0), summary.value("primitives", 0),
                summary.value("materials", 0), summary.value("textures", 0));
    ImGui::Text("Static Vertices: %d  Skinned Vertices: %d  Indices: %d",
                summary.value("staticVertices", 0),
                summary.value("skinnedVertices", 0),
                summary.value("indices", 0));
    ImGui::Text("HTEX Refs: %d  Fallback Textures: %d",
                summary.value("htexRefs", 0),
                summary.value("fallbackTextures", 0));
    ImGui::Text("Generated N/T: %d / %d  Unresolved Textures: %d",
                summary.value("missingNormalGeneratedCount", 0),
                summary.value("missingTangentGeneratedCount", 0),
                summary.value("unresolvedTextureCount", 0));
    ImGui::Text("Cluster Static / Fallback Prim: %d / %d",
                summary.value("clusteredStaticPrimitiveCount", 0),
                summary.value("fallbackPrimitiveCount", 0));
  }

  if (diagnostics.contains("formatReport") &&
      diagnostics["formatReport"].is_object()) {
    const nlohmann::json &formatReport = diagnostics["formatReport"];
    ImGui::SeparatorText("OBJ / glTF Support Report");
    ImGui::Text("Source Format: %s",
                formatReport.value("sourceFormat", "<unknown>").c_str());
    ImGui::Text("Object / Group / Triangulated: %d / %d / %d",
                formatReport.value("objectCount", 0),
                formatReport.value("groupCount", 0),
                formatReport.value("triangulatedPolygonCount", 0));
    ImGui::Text("Unsupported Modes / Features: %d / %d",
                formatReport.value("unsupportedPrimitiveModeCount", 0),
                formatReport.value("unsupportedFeatureCount", 0));
    ImGui::Text("Generated Normals / Tangents: %d / %d",
                formatReport.value("missingNormalGeneratedCount", 0),
                formatReport.value("missingTangentGeneratedCount", 0));
    ImGui::Text("Cluster Static / Fallback Prim: %d / %d",
                formatReport.value("clusteredStaticPrimitiveCount", 0),
                formatReport.value("fallbackPrimitiveCount", 0));
    if (formatReport.contains("unsupportedExtensions") &&
        formatReport["unsupportedExtensions"].is_array() &&
        !formatReport["unsupportedExtensions"].empty() &&
        ImGui::TreeNode("Unsupported Extensions")) {
      for (const nlohmann::json &extension :
           formatReport["unsupportedExtensions"]) {
        if (extension.is_string()) {
          ImGui::BulletText("%s", extension.get<std::string>().c_str());
        }
      }
      ImGui::TreePop();
    }
  }

  if (diagnostics.contains("clusteredGeometry") &&
      diagnostics["clusteredGeometry"].is_object()) {
    const nlohmann::json &cluster = diagnostics["clusteredGeometry"];
    ImGui::SeparatorText("HCMESH Diagnostics");
    ImGui::Text("Ready: %s", cluster.value("ready", false) ? "Yes" : "No");
    ImGui::Text("Profile: %s", cluster.value("profile", "Scene").c_str());
    ImGui::Text("Message: %s", cluster.value("message", "").c_str());
    if (cluster.contains("cookSettings") &&
        cluster["cookSettings"].is_object()) {
      const nlohmann::json &cook = cluster["cookSettings"];
      ImGui::Text(
          "LOD Count / Partition: %d / %s", cook.value("maxSurfaceLodCount", 0),
          cook.value("partitionLargeStaticSurfaces", false) ? "Yes" : "No");
      ImGui::Text("Partition Policy: %s",
                  cook.value("surfacePartitionPolicy", "SceneStatic").c_str());
      ImGui::Text("Partition Extent / Border Lock: %.2f / %s",
                  cook.value("largeSurfacePartitionMaxExtent", 0.0f),
                  cook.value("lockPartitionBorders", false) ? "Yes" : "No");
    }
    if (cluster.contains("summary") && cluster["summary"].is_object()) {
      const nlohmann::json &summary = cluster["summary"];
      ImGui::Text("Surfaces / LOD Ranges / Clusters / Pages: %d / %d / %d / %d",
                  summary.value("surfaces", 0),
                  summary.value("surfaceLodRanges", 0),
                  summary.value("clusters", 0), summary.value("pages", 0));
      ImGui::Text("Triangles / Vertices: %d / %d",
                  summary.value("triangles", 0), summary.value("vertices", 0));
      ImGui::Text("Avg Tri / Vert per Cluster: %.2f / %.2f",
                  summary.value("avgTrianglesPerCluster", 0.0),
                  summary.value("avgVerticesPerCluster", 0.0));
      ImGui::Text("Skipped Skin / Morph / Invalid: %d / %d / %d",
                  summary.value("skippedSkinnedPrimitives", 0),
                  summary.value("skippedMorphPrimitives", 0),
                  summary.value("skippedInvalidPrimitives", 0));
    }
    if (cluster.contains("validation") && cluster["validation"].is_object()) {
      const nlohmann::json &validation = cluster["validation"];
      ImGui::Text("Validation: %s",
                  validation.value("valid", false) ? "Valid" : "Invalid");
      ImGui::Text("Invalid S/C/P/B/M: %d / %d / %d / %d / %d",
                  validation.value("invalidSurfaces", 0),
                  validation.value("invalidClusters", 0),
                  validation.value("invalidPages", 0),
                  validation.value("invalidBounds", 0),
                  validation.value("invalidMaterials", 0));
    }
  }

  if (diagnostics.contains("collisionGeometry") &&
      diagnostics["collisionGeometry"].is_object()) {
    const nlohmann::json &collision = diagnostics["collisionGeometry"];
    ImGui::SeparatorText("Model Collision");
    ImGui::Text("Runtime artifact: %s",
                collision.value("ready", false) ? "Ready" : "None");
    ImGui::Text("Shapes: %d", collision.value("shapeCount", 0));
    ImGui::Text("Message: %s", collision.value("message", "").c_str());
    ImGui::TextDisabled("Author in the Model Collision workspace.");
  }

  if (diagnostics.contains("textures") && diagnostics["textures"].is_array() &&
      ImGui::BeginTable("ModelDiagnosticTextures", 4,
                        ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_BordersInnerV)) {
    ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 48.0f);
    ImGui::TableSetupColumn("HTEX", ImGuiTableColumnFlags_WidthFixed, 54.0f);
    ImGui::TableSetupColumn("Source");
    ImGui::TableSetupColumn("Runtime");
    ImGui::TableHeadersRow();

    for (const nlohmann::json &texture : diagnostics["textures"]) {
      if (!texture.is_object()) {
        continue;
      }
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::Text("%d", texture.value("index", -1));
      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(texture.value("htexReady", false) ? "Yes" : "No");
      ImGui::TableSetColumnIndex(2);
      ImGui::TextUnformatted(texture.value("sourcePath", "").c_str());
      ImGui::TableSetColumnIndex(3);
      ImGui::TextUnformatted(texture.value("runtimePath", "").c_str());
    }

    ImGui::EndTable();
  }

  if (diagnostics.contains("materials") &&
      diagnostics["materials"].is_array() &&
      ImGui::BeginTable("ModelDiagnosticMaterials", 4,
                        ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_BordersInnerV)) {
    ImGui::TableSetupColumn("Material");
    ImGui::TableSetupColumn("Metallic", ImGuiTableColumnFlags_WidthFixed,
                            72.0f);
    ImGui::TableSetupColumn("Roughness", ImGuiTableColumnFlags_WidthFixed,
                            80.0f);
    ImGui::TableSetupColumn("Alpha", ImGuiTableColumnFlags_WidthFixed, 70.0f);
    ImGui::TableHeadersRow();

    for (const nlohmann::json &material : diagnostics["materials"]) {
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
