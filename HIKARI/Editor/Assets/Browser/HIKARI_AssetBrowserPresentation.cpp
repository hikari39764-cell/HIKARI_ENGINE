#include "Core/Text/HIKARI_AsciiCase.h"
#include "Editor/Assets/Browser/HIKARI_AssetBrowserPanel.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <json.hpp>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetImportState.h"
#include "Assets/HIKARI_AssetUsageAnalyzer.h"
#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Editor/DragDrop/HIKARI_EditorAssetDragDrop.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/Style/HIKARI_EditorAssetIcons.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorTheme.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Platform/HIKARI_Win32Window.h"
#include "Project/HIKARI_ProjectSettings.h"
#include "Project/Paths/HIKARI_ProjectPath.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

#include "Editor/Assets/Browser/HIKARI_AssetBrowserInternal.h"

namespace HIKARI::EDITOR::ASSET_BROWSER {

#if defined(HIKARI_WITH_EDITOR)
ImVec4 StateColor(AssetImportState state) {
  switch (state) {
  case AssetImportState::Imported:
    return ImVec4(0.62f, 0.86f, 0.66f, 1.0f);
  case AssetImportState::Outdated:
    return ImVec4(0.95f, 0.78f, 0.34f, 1.0f);
  case AssetImportState::MissingArtifact:
    return ImVec4(1.0f, 0.58f, 0.28f, 1.0f);
  case AssetImportState::MissingSource:
  case AssetImportState::UnknownImporter:
  case AssetImportState::DuplicateGuid:
  case AssetImportState::ImportFailed:
    return ImVec4(1.0f, 0.36f, 0.36f, 1.0f);
  case AssetImportState::MetaOnly:
    return ImVec4(0.62f, 0.66f, 0.72f, 1.0f);
  case AssetImportState::MissingMeta:
  case AssetImportState::Unknown:
  default:
    return ImVec4(0.78f, 0.78f, 0.78f, 1.0f);
  }
}

std::string BuildGridCardLabel(std::string_view text, float maxWidth) {
  if (text.empty() || maxWidth <= 0.0f) {
    return {};
  }
  if (ImGui::CalcTextSize(text.data(), text.data() + text.size()).x <=
      maxWidth) {
    return std::string(text);
  }

  // 驛｢・ｧ繝ｻ・ｰ驛｢譎｢・ｽ・ｪ驛｢譏ｴ繝ｻ郢晢ｽｩ驍ｵ・ｺ繝ｻ・ｧ驍ｵ・ｺ繝ｻ・ｯ髯ｷ・ｷ隶朱｡披・驍ｵ・ｺ繝ｻ・ｰ驍ｵ・ｺ闔会ｽ｣繝ｻ蟶晢ｿ･繝ｻ・ｭ驍ｵ・ｺ陷托ｽｰ繝ｻ・｡繝ｻ・ｨ鬩穂ｼ夲ｽｽ・ｺ驍ｵ・ｺ陷会ｽｱ・つ遶擾ｽｬ繝ｻ・ｩ繝ｻ・ｳ鬩肴得・ｽ・ｰ驍ｵ・ｺ繝ｻ・ｯ驛｢譏ｴ繝ｻ郢晢ｽｻ驛｢譎｢・ｽ・ｫ驛｢譏ｶ繝ｻ郢晢ｽ｣驛｢譎・諛翫・驍ｵ・ｺ繝ｻ・ｫ髣比ｼ夲ｽｽ・ｻ驍ｵ・ｺ陝ｶ蜻ｻ・ｽ迢暦ｽｸ・ｲ郢晢ｽｻ
  constexpr const char *kSuffix = "...";
  std::vector<size_t> utf8Ends{};
  for (size_t i = 0; i < text.size();) {
    const unsigned char c = static_cast<unsigned char>(text[i]);
    size_t step = 1;
    if ((c & 0xE0) == 0xC0) {
      step = 2;
    } else if ((c & 0xF0) == 0xE0) {
      step = 3;
    } else if ((c & 0xF8) == 0xF0) {
      step = 4;
    }

    if (i + step > text.size()) {
      break;
    }
    i += step;
    utf8Ends.push_back(i);
  }

  for (size_t count = utf8Ends.size(); count > 0; --count) {
    std::string candidate{text.substr(0, utf8Ends[count - 1])};
    candidate += kSuffix;
    if (ImGui::CalcTextSize(candidate.c_str()).x <= maxWidth) {
      return candidate;
    }
  }

  return ImGui::CalcTextSize(kSuffix).x <= maxWidth ? std::string(kSuffix)
                                                    : std::string{};
}

void DrawDirectoryBreadcrumbs(std::filesystem::path &currentDirectory) {
  const std::filesystem::path normalized = currentDirectory.lexically_normal();
  std::filesystem::path accumulated{};
  bool first = true;
  int segmentIndex = 0;
  for (const auto &segment : normalized) {
    accumulated /= segment;
    const std::string label = segment.generic_string();
    if (label.empty() || label == ".") {
      continue;
    }
    if (!first) {
      ImGui::SameLine(0.0f, 5.0f);
      ImGui::TextDisabled(">");
      ImGui::SameLine(0.0f, 5.0f);
    }
    const std::string id = "AssetBreadcrumb" + std::to_string(segmentIndex++);
    const std::string tooltip = accumulated.generic_string();
    if (EDITOR::ActionButton(label.c_str(), id.c_str(),
                             EDITOR::EditorButtonTone::Quiet,
                             ImVec2(0.0f, 24.0f), tooltip.c_str())) {
      currentDirectory = accumulated.lexically_normal();
    }
    first = false;
  }
}

void DrawRecordTooltip(const AssetRecord &record) {
  if (!ImGui::BeginTooltip()) {
    return;
  }

  const AssetImportState state = GetImportState(record);
  EDITOR::DrawAssetTypeGlyph(record.type, ImVec2(18.0f, 18.0f));
  ImGui::SameLine();
  ImGui::TextUnformatted(record.displayName.c_str());
  ImGui::TextDisabled("%s | %s", ToAssetTypeText(record.type), ToString(state));
  ImGui::Separator();
  ImGui::Text("GUID: %s",
              record.guid.value.empty() ? "<none>" : record.guid.value.c_str());
  ImGui::Text("Source: %s", record.sourcePath.generic_string().c_str());
  ImGui::Text("Meta: %s", record.metaPath.generic_string().c_str());
  ImGui::Text("Importer: %s v%u",
              record.meta.importerId.empty() ? "<none>"
                                             : record.meta.importerId.c_str(),
              record.meta.importerVersion);
  const std::string artifactPath = FirstArtifactPath(record);
  ImGui::Text("Artifact: %s",
              artifactPath.empty() ? "<none>" : artifactPath.c_str());
  ImGui::Text("Dependencies: %d",
              static_cast<int>(record.artifactManifest.dependencies.size()));
  ImGui::EndTooltip();
}

void DrawCompactBadge(const char *label, const ImVec4 &color) {
  if (!label || label[0] == '\0') {
    ImGui::TextDisabled("-");
    return;
  }
  ImGui::TextColored(color, "[%s]", label);
}

ImVec4 TypeBadgeColor(AssetType type) {
  switch (type) {
  case AssetType::Texture:
    return ImVec4(0.55f, 0.78f, 1.0f, 1.0f);
  case AssetType::Model:
    return ImVec4(0.76f, 0.70f, 1.0f, 1.0f);
  case AssetType::Scene:
    return ImVec4(0.66f, 0.88f, 0.68f, 1.0f);
  case AssetType::Material:
    return ImVec4(1.0f, 0.78f, 0.48f, 1.0f);
  case AssetType::Sky:
    return ImVec4(0.54f, 0.90f, 0.92f, 1.0f);
  case AssetType::VfxEffect:
    return ImVec4(1.0f, 0.62f, 0.74f, 1.0f);
  case AssetType::Sequence:
    return ImVec4(0.72f, 0.68f, 1.0f, 1.0f);
  case AssetType::AnimationStateMachine:
    return ImVec4(0.40f, 0.84f, 0.78f, 1.0f);
  default:
    return ImVec4(0.72f, 0.74f, 0.78f, 1.0f);
  }
}

ImVec4 UsageBadgeColor(const char *label) {
  if (label && std::string_view(label) == "USE") {
    return ImVec4(0.60f, 0.86f, 0.60f, 1.0f);
  }
  return ImVec4(0.62f, 0.66f, 0.72f, 1.0f);
}

ImVec4 CookedBadgeColor(const char *label) {
  if (!label || label[0] == '\0') {
    return ImVec4(0.62f, 0.66f, 0.72f, 1.0f);
  }
  if (std::string_view(label) == "HTEX" || std::string_view(label) == "HMDL") {
    return ImVec4(0.54f, 0.82f, 1.0f, 1.0f);
  }
  if (std::string_view(label) == "JSON") {
    return ImVec4(0.66f, 0.88f, 0.68f, 1.0f);
  }
  if (std::string_view(label) == "DDS") {
    return ImVec4(0.92f, 0.78f, 0.42f, 1.0f);
  }
  return ImVec4(0.62f, 0.66f, 0.72f, 1.0f);
}
#endif

} // namespace HIKARI::EDITOR::ASSET_BROWSER
