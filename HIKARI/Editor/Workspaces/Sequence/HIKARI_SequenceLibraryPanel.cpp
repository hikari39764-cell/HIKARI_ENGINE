#include "Editor/Workspaces/Sequence/HIKARI_SequenceLibraryPanel.h"
#include "Core/Text/HIKARI_AsciiCase.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetDatabase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

namespace {

bool MatchesSearch(const AssetRecord &record, const char *searchText) {

  if (searchText == nullptr || searchText[0] == '\0') {
    return true;
  }
  const std::string haystack = TEXT::ToLowerAsciiCopy(
      record.displayName + " " + record.sourcePath.generic_string() + " " +
      record.guid.value);
  return haystack.find(TEXT::ToLowerAsciiCopy(searchText)) != std::string::npos;
}

std::string ShortGuid(const AssetGuid &guid) {
  return guid.value.size() <= 8 ? guid.value : guid.value.substr(0, 8);
}
} // namespace

SequenceLibraryPanelResult
SequenceLibraryPanel::Draw(AssetDatabase &assetDatabase,
                           const AssetGuid &openAssetGuid,
                           bool actionsAllowed) {

  SequenceLibraryPanelResult result{};
#if defined(HIKARI_WITH_EDITOR)
  ImGui::TextUnformatted("Sequence Library");
  ImGui::SameLine();
  ImGui::TextDisabled(
      "%d assets",
      static_cast<int>(
          assetDatabase.CollectByType(AssetType::Sequence).size()));
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputTextWithHint("##SequenceLibrarySearch",
                           "Search name, path, or GUID...",
                           searchBuffer_.data(), searchBuffer_.size());

  if (!actionsAllowed) {
    ImGui::BeginDisabled();
  }
  if (ImGui::Button("New Sequence", ImVec2(-1.0f, 0.0f))) {
    result.action = SequenceLibraryActionKind::NewAsset;
  }
  if (!actionsAllowed) {
    ImGui::EndDisabled();
  }
  ImGui::Separator();

  std::vector<const AssetRecord *> records =
      assetDatabase.CollectByType(AssetType::Sequence);
  std::sort(records.begin(), records.end(),
            [](const AssetRecord *lhs, const AssetRecord *rhs) {
              if (lhs == nullptr || rhs == nullptr) {
                return rhs != nullptr;
              }
              return lhs->displayName < rhs->displayName;
            });

  const float footerHeight = ImGui::GetFrameHeightWithSpacing() + 8.0f;
  if (ImGui::BeginChild("##SequenceLibraryList", ImVec2(0.0f, -footerHeight),
                        ImGuiChildFlags_Border)) {
    for (const AssetRecord *record : records) {
      if (record == nullptr || !MatchesSearch(*record, searchBuffer_.data())) {
        continue;
      }
      ImGui::PushID(record->guid.value.c_str());
      const bool selected = selectedAssetGuid_ == record->guid;
      std::string name = record->displayName.empty()
                             ? record->sourcePath.stem().string()
                             : record->displayName;
      if (openAssetGuid == record->guid) {
        name += "  [Open]";
      }
      if (ImGui::Selectable(name.c_str(), selected,
                            ImGuiSelectableFlags_AllowDoubleClick)) {
        selectedAssetGuid_ = record->guid;
        if (actionsAllowed &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
          result.action = SequenceLibraryActionKind::OpenAsset;
          result.assetGuid = record->guid;
        }
      }
      ImGui::TextDisabled("%s", record->sourcePath.generic_string().c_str());
      ImGui::TextDisabled("GUID %s", ShortGuid(record->guid).c_str());
      ImGui::Separator();
      ImGui::PopID();
    }
  }
  ImGui::EndChild();

  const bool canOpen = actionsAllowed && selectedAssetGuid_.IsValid();
  if (!canOpen) {
    ImGui::BeginDisabled();
  }
  std::string openLabel = "Open Selected";
  if (selectedAssetGuid_.IsValid()) {
    if (const AssetRecord *selected =
            assetDatabase.FindByGuid(selectedAssetGuid_)) {
      openLabel = "Open " + selected->displayName;
    }
  }
  if (ImGui::Button(openLabel.c_str(), ImVec2(-1.0f, 0.0f))) {
    result.action = SequenceLibraryActionKind::OpenAsset;
    result.assetGuid = selectedAssetGuid_;
  }
  if (!canOpen) {
    ImGui::EndDisabled();
  }
#else
  (void)assetDatabase;
  (void)openAssetGuid;
  (void)actionsAllowed;
#endif
  return result;
}

void SequenceLibraryPanel::Select(const AssetGuid &assetGuid) noexcept {
  selectedAssetGuid_ = assetGuid;
}

} // namespace HIKARI::EDITOR
