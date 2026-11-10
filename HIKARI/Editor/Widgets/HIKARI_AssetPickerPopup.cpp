#include "Core/Text/HIKARI_Localization.h"
#include "HIKARI_AssetPickerPopup.h"
#include "Core/Text/HIKARI_AsciiCase.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetImportState.h"
#include "Assets/Semantics/HIKARI_AssetSourceSemantics.h"

#include "imgui.h"

namespace HIKARI::EDITOR {

std::string AssetKindDisplayName(AssetType type, std::string_view moduleAssetTypeId) {
    if (!moduleAssetTypeId.empty()) {
        const ASSETS::SEMANTICS::ModuleAssetTypeDesc* module =
            ASSETS::SEMANTICS::FindModuleAssetType(moduleAssetTypeId);
        return module != nullptr && !module->displayName.empty() ? module->displayName : std::string(moduleAssetTypeId);
    }
    switch (type) {
    case AssetType::Model: return "Model";
    case AssetType::Scene: return "Scene";
    case AssetType::Texture: return "Texture";
    case AssetType::Sky: return "Sky";
    case AssetType::Material: return "Material";
    case AssetType::VfxEffect: return "VFX";
    case AssetType::Animation: return "Animation";
    case AssetType::Particle: return "Particle";
    case AssetType::Sequence: return "Sequence";
    case AssetType::AnimationStateMachine: return "Animation State Machine";
    case AssetType::Dialogue: return "Dialogue";
    case AssetType::ModelMaterialBinding: return "Material Set";
    default: return "Asset";
    }
}

namespace {

const char* ToAssetIcon(AssetType type) {
    switch (type) {
    case AssetType::Model: return "[M]";
    case AssetType::Scene: return "[Scn]";
    case AssetType::Texture: return "[T]";
    case AssetType::Sky: return "[S]";
    case AssetType::Material: return "[Mat]";
    case AssetType::VfxEffect: return "[V]";
    case AssetType::Sequence: return "[Seq]";
    case AssetType::AnimationStateMachine: return "[ASM]";
    case AssetType::Dialogue: return "[DLG]";
    case AssetType::ModelMaterialBinding: return "[Set]";
    case AssetType::ModuleAsset: return "[Mod]";
    default: return "[?]";
    }
}

struct PickerEntry {
    const AssetRecord* record = nullptr;
    std::string label{};
    std::string searchText{};
};

PickerEntry BuildEntry(const AssetRecord& record) {
    PickerEntry entry{};
    entry.record = &record;
    const AssetImportState state = GetImportState(record);
    const std::string name = record.displayName.empty() ? record.sourcePath.stem().string() : record.displayName;
    entry.label = std::string(ToAssetIcon(record.type)) + " " + (name.empty() ? std::string("<unnamed>") : name) +
                  "  [" + ToString(state) + "]##" + record.guid.value;
    entry.searchText = TEXT::ToLowerAsciiCopy(name + " " + record.sourcePath.generic_string() + " " +
                                              record.guid.value + " " + record.meta.importerId + " " + ToString(state));
    return entry;
}
} // namespace

bool DrawAssetPickerPopup(const char* popupId, const AssetDatabase& assetDatabase, AssetType requiredType,
                          std::string& inOutGuid, AssetPickerPopupState& state, std::string_view moduleAssetTypeId) {
    bool changed = false;
    if (!ImGui::BeginPopup(popupId)) {
        return false;
    }

    ImGui::Text(TEXT::Localize("editor.ui.select.s.95d86c").c_str(),
                AssetKindDisplayName(requiredType, moduleAssetTypeId).c_str());
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##AssetPickerSearch", TEXT::Localize("editor.ui.search.name.path.guid.dc5276").c_str(),
                             state.searchBuffer.data(), state.searchBuffer.size());
    ImGui::Separator();

    if (ImGui::Selectable(TEXT::LocalizeWindow("ui.common.none", "<none>").c_str(), inOutGuid.empty())) {
        inOutGuid.clear();
        changed = true;
        ImGui::CloseCurrentPopup();
    }

    std::vector<const AssetRecord*> records =
        requiredType == AssetType::Unknown ? assetDatabase.CollectAll() : assetDatabase.CollectByType(requiredType);

    std::vector<PickerEntry> entries;
    std::vector<std::string> folders{};
    entries.reserve(records.size());
    for (const AssetRecord* record : records) {
        if (record && record->guid.IsValid() &&
            (moduleAssetTypeId.empty() ||
             ASSETS::SEMANTICS::MatchesAssetType(record->type, record->sourcePath, requiredType, moduleAssetTypeId))) {
            entries.push_back(BuildEntry(*record));
            const std::string folder = record->sourcePath.parent_path().generic_string();
            if (!folder.empty()) {
                folders.push_back(folder);
            }
        }
    }
    std::sort(folders.begin(), folders.end());
    folders.erase(std::unique(folders.begin(), folders.end()), folders.end());
    std::sort(entries.begin(), entries.end(), [](const PickerEntry& lhs, const PickerEntry& rhs) {
        const std::string lhsName = lhs.record ? lhs.record->displayName : std::string{};
        const std::string rhsName = rhs.record ? rhs.record->displayName : std::string{};
        return TEXT::ToLowerAsciiCopy(lhsName) < TEXT::ToLowerAsciiCopy(rhsName);
    });

    const std::string search = TEXT::ToLowerAsciiCopy(state.searchBuffer.data());
    int visibleCount = 0;
    if (ImGui::BeginChild("##AssetPickerFolders", ImVec2(190.0f, 310.0f), true)) {
        ImGui::TextDisabled(TEXT::Localize("editor.ui.folders.d10bdc").c_str());
        if (ImGui::Selectable(TEXT::LocalizeWindow("editor.ui.all.assets.69d4af", "All Assets").c_str(),
                              state.selectedFolder.empty())) {
            state.selectedFolder.clear();
        }
        for (const std::string& folder : folders) {
            if (ImGui::Selectable(folder.c_str(), state.selectedFolder == folder)) {
                state.selectedFolder = folder;
            }
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();
    if (ImGui::BeginChild("##AssetPickerList", ImVec2(460.0f, 310.0f), true)) {
        for (const PickerEntry& entry : entries) {
            if (!entry.record) {
                continue;
            }
            if (!search.empty() && entry.searchText.find(search) == std::string::npos) {
                continue;
            }
            if (!state.selectedFolder.empty()) {
                const std::string folder = entry.record->sourcePath.parent_path().generic_string();
                const std::string prefix = state.selectedFolder + "/";
                if (folder != state.selectedFolder && !folder.starts_with(prefix)) {
                    continue;
                }
            }

            ++visibleCount;
            const bool selected = inOutGuid == entry.record->guid.value;
            ImGui::PushID(entry.record->guid.value.c_str());
            if (ImGui::Selectable(entry.label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                inOutGuid = entry.record->guid.value;
                changed = true;
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    ImGui::CloseCurrentPopup();
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(TEXT::Localize("editor.ui.s.n.s.nimporter.s.nguid.s.2e6b04").c_str(),
                                  entry.record->displayName.c_str(), entry.record->sourcePath.generic_string().c_str(),
                                  entry.record->meta.importerId.empty() ? "<none>"
                                                                        : entry.record->meta.importerId.c_str(),
                                  entry.record->guid.value.c_str());
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
        }
        if (visibleCount == 0) {
            ImGui::TextDisabled(TEXT::Localize("editor.ui.no.matching.assets.5560dd").c_str());
        }
    }
    ImGui::EndChild();

    ImGui::EndPopup();
    return changed;
}

} // namespace HIKARI::EDITOR
