#include "HIKARI_AssetPickerPopup.h"
#include "Core/Text/HIKARI_AsciiCase.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetImportState.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

    namespace {

        const char* ToAssetTypeText(AssetType type) {
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
            case AssetType::AnimationStateMachine:
                return "Animation State Machine";
            case AssetType::Unknown:
            default: return "Asset";
            }
        }

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
            const std::string name = record.displayName.empty()
                ? record.sourcePath.stem().string()
                : record.displayName;
            entry.label =
                std::string(ToAssetIcon(record.type)) + " " +
                (name.empty() ? std::string("<unnamed>") : name) +
                "  [" + ToString(state) + "]##" + record.guid.value;
            entry.searchText = TEXT::ToLowerAsciiCopy(
                name + " " +
                record.sourcePath.generic_string() + " " +
                record.guid.value + " " +
                record.meta.importerId + " " +
                ToString(state));
            return entry;
        }
    }

    bool DrawAssetPickerPopup(
        const char* popupId,
        const AssetDatabase& assetDatabase,
        AssetType requiredType,
        std::string& inOutGuid,
        AssetPickerPopupState& state) {
#if defined(HIKARI_WITH_EDITOR)
        bool changed = false;
        if (!ImGui::BeginPopup(popupId)) {
            return false;
        }

        ImGui::Text("Select %s", ToAssetTypeText(requiredType));
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##AssetPickerSearch", "Search name, path, GUID...", state.searchBuffer.data(), state.searchBuffer.size());
        ImGui::Separator();

        if (ImGui::Selectable("<none>", inOutGuid.empty())) {
            inOutGuid.clear();
            changed = true;
            ImGui::CloseCurrentPopup();
        }

        std::vector<const AssetRecord*> records = requiredType == AssetType::Unknown
            ? assetDatabase.CollectAll()
            : assetDatabase.CollectByType(requiredType);

        std::vector<PickerEntry> entries;
        entries.reserve(records.size());
        for (const AssetRecord* record : records) {
            if (record && record->guid.IsValid()) {
                entries.push_back(BuildEntry(*record));
            }
        }
        std::sort(entries.begin(), entries.end(), [](const PickerEntry& lhs, const PickerEntry& rhs) {
            const std::string lhsName = lhs.record ? lhs.record->displayName : std::string{};
            const std::string rhsName = rhs.record ? rhs.record->displayName : std::string{};
            return TEXT::ToLowerAsciiCopy(lhsName) < TEXT::ToLowerAsciiCopy(rhsName);
        });

        const std::string search = TEXT::ToLowerAsciiCopy(state.searchBuffer.data());
        int visibleCount = 0;
        if (ImGui::BeginChild("##AssetPickerList", ImVec2(520.0f, 310.0f), true)) {
            for (const PickerEntry& entry : entries) {
                if (!entry.record) {
                    continue;
                }
                if (!search.empty() && entry.searchText.find(search) == std::string::npos) {
                    continue;
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
                    ImGui::SetTooltip(
                        "%s\n%s\nImporter: %s\nGUID: %s",
                        entry.record->displayName.c_str(),
                        entry.record->sourcePath.generic_string().c_str(),
                        entry.record->meta.importerId.empty() ? "<none>" : entry.record->meta.importerId.c_str(),
                        entry.record->guid.value.c_str());
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
                ImGui::PopID();
            }
            if (visibleCount == 0) {
                ImGui::TextDisabled("No matching assets.");
            }
        }
        ImGui::EndChild();

        ImGui::EndPopup();
        return changed;
#else
        (void)popupId;
            (void)assetDatabase;
        (void)requiredType;
        (void)inOutGuid;
        (void)state;
        return false;
#endif
    }

} // namespace HIKARI::EDITOR
