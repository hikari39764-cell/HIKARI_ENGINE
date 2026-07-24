#include "HIKARI_AssetFieldWidget.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <string>
#include <unordered_map>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetImportState.h"
#include "Editor/DragDrop/HIKARI_EditorAssetDragDrop.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/Widgets/HIKARI_AssetPickerPopup.h"
#include "Editor/Widgets/HIKARI_InspectorPropertyLayout.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

    namespace {
#if defined(HIKARI_WITH_EDITOR)
        std::unordered_map<ImGuiID, AssetPickerPopupState> gPickerStates{};

        const char* ToAssetTypeText(AssetType type) {
            switch (type) {
            case AssetType::Model: return "Model";
            case AssetType::Scene: return "Scene";
            case AssetType::Texture: return "Texture";
            case AssetType::Sky: return "Sky";
            case AssetType::Material: return "Material";
            case AssetType::VfxEffect: return "VFX";
            case AssetType::Sequence: return "Sequence";
            case AssetType::AnimationStateMachine:
                return "Animation State Machine";
            default: return "Asset";
            }
        }

        std::string ShortGuid(std::string_view guid) {
            if (guid.size() <= 8) {
                return std::string(guid);
            }
            return std::string(guid.substr(0, 8));
        }

        std::string BuildPreviewText(const AssetRecord* record, const std::string& guid) {
            if (guid.empty()) {
                return "<none>";
            }
            if (!record) {
                return "Missing: " + ShortGuid(guid);
            }
            if (!record->displayName.empty()) {
                return record->displayName;
            }
            return record->sourcePath.stem().string();
        }

        bool AcceptAssetGuidPayload(const AssetDatabase* assetDatabase, AssetType requiredType, std::string& inOutGuid) {
            if (!assetDatabase) {
                return false;
            }

            DroppedAssetPayload payload{};
            const bool accepted = requiredType == AssetType::Unknown
                ? AcceptAssetDrop(*assetDatabase, payload)
                : AcceptAssetDropOfType(*assetDatabase, requiredType, payload);
            if (!accepted || !payload.record) {
                return false;
            }

            inOutGuid = payload.guid.value;
            return true;
        }
#endif
    }

    bool DrawAssetField(
        const AssetDatabase* assetDatabase,
        const AssetFieldOptions& options,
        std::string& inOutGuid,
        EditorSelection* selection) {
#if defined(HIKARI_WITH_EDITOR)
        const std::string labelText(options.label.empty() ? ToAssetTypeText(options.requiredType) : std::string(options.label));
        if (!assetDatabase) {
            std::array<char, 256> buffer{};
            strncpy_s(buffer.data(), buffer.size(), inOutGuid.c_str(), buffer.size() - 1);
            if (ImGui::InputText(labelText.c_str(), buffer.data(), buffer.size())) {
                inOutGuid = buffer.data();
                return true;
            }
            return false;
        }

        bool changed = false;
        const AssetRecord* current = inOutGuid.empty() ? nullptr : assetDatabase->FindByGuid(AssetGuid{ inOutGuid });
        const std::string popupId = "AssetPickerPopup##" + labelText;
        const std::string preview = BuildPreviewText(current, inOutGuid);

        std::optional<InspectorPropertyRow> propertyRow{};
        if (options.drawLabel) {
            propertyRow.emplace(labelText);
            if (!propertyRow->IsVisible()) {
                return false;
            }
        }
        ImGui::PushID(labelText.c_str());
        const float menuButtonWidth = ImGui::GetFrameHeight();
        const float buttonWidth = (std::max)(
            40.0f,
            ImGui::GetContentRegionAvail().x -
                menuButtonWidth - ImGui::GetStyle().ItemSpacing.x);
        if (ImGui::Button(preview.c_str(), ImVec2(buttonWidth, 0.0f))) {
            ImGui::OpenPopup(popupId.c_str());
        }
        changed = AcceptAssetGuidPayload(assetDatabase, options.requiredType, inOutGuid) || changed;
        if (ImGui::IsItemHovered() && !inOutGuid.empty()) {
            if (current) {
                ImGui::SetTooltip(
                    "%s\n%s\nGUID: %s",
                    current->sourcePath.generic_string().c_str(),
                    ToString(GetImportState(*current)),
                    current->guid.value.c_str());
            } else {
                ImGui::SetTooltip("Missing GUID: %s", inOutGuid.c_str());
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("...", ImVec2(menuButtonWidth, 0.0f))) {
            ImGui::OpenPopup("AssetFieldActions");
        }
        bool openPickerFromActions = false;
        if (ImGui::BeginPopup("AssetFieldActions")) {
            if (ImGui::MenuItem("Choose Asset...")) {
                openPickerFromActions = true;
            }
            if (options.allowClear) {
                const bool canClear = !inOutGuid.empty();
                if (ImGui::MenuItem("Clear", nullptr, false, canClear)) {
                    inOutGuid.clear();
                    changed = true;
                }
            }
            if (options.allowLocate) {
                const bool canLocate = current != nullptr && selection != nullptr;
                if (ImGui::MenuItem("Locate in Resources", nullptr, false, canLocate)) {
                    selection->selectedAssetGuid = current->guid.value;
                    selection->selectedAssetPath = current->sourcePath.generic_string();
                    selection->selectedAsset = nullptr;
                }
            }
            if (options.allowCopyGuid) {
                const bool canCopy = !inOutGuid.empty();
                if (ImGui::MenuItem("Copy GUID", nullptr, false, canCopy)) {
                    ImGui::SetClipboardText(inOutGuid.c_str());
                }
            }
            ImGui::EndPopup();
        }
        if (openPickerFromActions) {
            ImGui::OpenPopup(popupId.c_str());
        }

        const ImGuiID fieldId = ImGui::GetID("AssetFieldState");
        AssetPickerPopupState& popupState = gPickerStates[fieldId];
        changed = DrawAssetPickerPopup(popupId.c_str(), *assetDatabase, options.requiredType, inOutGuid, popupState) || changed;

        if (!inOutGuid.empty() && current == nullptr) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                "Missing %s: %s",
                ToAssetTypeText(options.requiredType),
                ShortGuid(inOutGuid).c_str());
        }

        ImGui::PopID();
        return changed;
#else
        (void)assetDatabase;
        (void)options;
        (void)inOutGuid;
        (void)selection;
        return false;
#endif
    }

} // namespace HIKARI::EDITOR
