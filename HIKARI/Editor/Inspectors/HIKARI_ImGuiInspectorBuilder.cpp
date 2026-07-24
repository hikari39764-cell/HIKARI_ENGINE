#include "Editor/Inspectors/HIKARI_ImGuiInspectorBuilder.h"
#include "Core/Text/HIKARI_AsciiCase.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetImportState.h"
#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Editor/Widgets/HIKARI_AssetFieldWidget.h"
#include "Editor/Widgets/HIKARI_InputActionFieldWidget.h"
#include "Editor/Widgets/HIKARI_InspectorPropertyLayout.h"
#include "Input/Runtime/HIKARI_InputService.h"
#include "Scene/HIKARI_WorldServiceRegistry.h"
#include "Scene/HIKARI_SceneDocument.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include <cstring>
#endif

namespace HIKARI {

    namespace {

        std::string ShortGuid(std::string_view guid) {
            if (guid.size() <= 8) {
                return std::string(guid);
            }
            return std::string(guid.substr(0, 8));
        }

        struct AssetPickerEntry {
            std::string id{};
            std::string name{};
            std::string path{};
            std::string state{};
            std::string label{};
            std::string searchText{};
        };

        AssetPickerEntry BuildAssetPickerEntry(
            const AssetDescriptor& descriptor,
            const AssetDatabase* assetDatabase) {

            AssetPickerEntry entry{};
            entry.id = descriptor.id.value;
            entry.path = descriptor.sourcePath;

            if (assetDatabase) {
                if (const AssetRecord* record = assetDatabase->FindByGuid(AssetGuid{ descriptor.id.value })) {
                    entry.name = record->displayName.empty() ? record->sourcePath.stem().string() : record->displayName;
                    entry.path = record->sourcePath.generic_string();
                    entry.state = ToString(GetImportState(*record));
                }
            }

            if (entry.name.empty()) {
                std::filesystem::path sourcePath{ descriptor.sourcePath };
                entry.name = sourcePath.stem().string();
            }
            if (entry.name.empty()) {
                entry.name = entry.id.empty() ? "<unnamed>" : entry.id;
            }
            if (entry.state.empty()) {
                entry.state = "Registered";
            }

            entry.label = entry.name + "  [" + entry.state + "]##" + entry.id;
            entry.searchText = TEXT::ToLowerAsciiCopy(entry.name + " " + entry.path + " " + entry.id + " " + entry.state);
            return entry;
        }
    }

    void ImGuiInspectorBuilder::SetContext(const InspectorContext& context) {
        context_ = context;
    }

    const InspectorContext&
        ImGuiInspectorBuilder::GetContext() const noexcept {

        return context_;
    }

    void ImGuiInspectorBuilder::Text(std::string_view text) {
#if defined(HIKARI_WITH_EDITOR)
        ImGui::TextWrapped("%.*s", static_cast<int>(text.size()), text.data());
#else
        (void)text;
#endif
    }

    bool ImGuiInspectorBuilder::Button(std::string_view label) {
#if defined(HIKARI_WITH_EDITOR)
        return ImGui::Button(
            std::string(label).c_str(),
            ImVec2(-1.0f, 0.0f));
#else
        (void)label;
        return false;
#endif
    }

    bool ImGuiInspectorBuilder::Bool(std::string_view label, bool& value) {
#if defined(HIKARI_WITH_EDITOR)
        EDITOR::InspectorPropertyRow row(
            label,
            EDITOR::InspectorPropertyValueKind::Compact);
        return row.IsVisible() && ImGui::Checkbox("##Value", &value);
#else
        (void)label;
        (void)value;
        return false;
#endif
    }

    bool ImGuiInspectorBuilder::Int(std::string_view label, int& value) {
#if defined(HIKARI_WITH_EDITOR)
        EDITOR::InspectorPropertyRow row(label);
        return row.IsVisible() && ImGui::InputInt("##Value", &value);
#else
        (void)label;
        (void)value;
        return false;
#endif
    }

    bool ImGuiInspectorBuilder::Float(std::string_view label, float& value) {
#if defined(HIKARI_WITH_EDITOR)
        EDITOR::InspectorPropertyRow row(label);
        return row.IsVisible() &&
            ImGui::DragFloat("##Value", &value, 0.1f);
#else
        (void)label;
        (void)value;
        return false;
#endif
    }

    bool ImGuiInspectorBuilder::String(std::string_view label, std::string& value) {
#if defined(HIKARI_WITH_EDITOR)
        char buffer[256]{};
        strncpy_s(buffer, value.c_str(), sizeof(buffer) - 1);
        EDITOR::InspectorPropertyRow row(label);
        if (row.IsVisible() &&
            ImGui::InputText("##Value", buffer, sizeof(buffer))) {
            value = buffer;
            return true;
        }
        return false;
#else
        (void)label;
        (void)value;
        return false;
#endif
    }

    bool ImGuiInspectorBuilder::InputActionIdPicker(
        std::string_view label,
        INPUT::InputActionValueType expectedType,
        std::string& value) {
#if defined(HIKARI_WITH_EDITOR)
        const auto* inputService = context_.worldServices != nullptr
            ? context_.worldServices->Find<INPUT::InputService>()
            : nullptr;
        EDITOR::InspectorPropertyRow row(label);
        if (!row.IsVisible()) {
            return false;
        }
        if (inputService == nullptr) {
            ImGui::TextDisabled("Input action map unavailable");
            return false;
        }
        return EDITOR::DrawInputActionField(
            inputService->GetActionMap(),
            "##Value",
            expectedType,
            value);
#else
        (void)expectedType;
        return String(label, value);
#endif
    }

    bool ImGuiInspectorBuilder::FloatRange(
        std::string_view label,
        float& value,
        float minimum,
        float maximum,
        float speed) {
#if defined(HIKARI_WITH_EDITOR)
        EDITOR::InspectorPropertyRow row(label);
        return row.IsVisible() &&
            ImGui::DragFloat(
                "##Value",
                &value,
                speed,
                minimum,
                maximum,
                "%.3f",
                ImGuiSliderFlags_AlwaysClamp);
#else
        (void)label;
        (void)value;
        (void)minimum;
        (void)maximum;
        (void)speed;
        return false;
#endif
    }

    bool ImGuiInspectorBuilder::Choice(
        std::string_view label,
        int& selectedIndex,
        std::span<const char* const> choices) {
#if defined(HIKARI_WITH_EDITOR)
        if (choices.empty()) {
            return false;
        }
        selectedIndex = std::clamp(
            selectedIndex,
            0,
            static_cast<int>(choices.size() - 1));
        bool changed = false;
        EDITOR::InspectorPropertyRow row(label);
        if (!row.IsVisible()) {
            return false;
        }
        if (ImGui::BeginCombo(
                "##Value",
                choices[static_cast<size_t>(selectedIndex)])) {
            for (size_t index = 0; index < choices.size(); ++index) {
                const bool selected = static_cast<int>(index) ==
                    selectedIndex;
                if (ImGui::Selectable(choices[index], selected)) {
                    selectedIndex = static_cast<int>(index);
                    changed = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        return changed;
#else
        (void)label;
        (void)selectedIndex;
        (void)choices;
        return false;
#endif
    }

    bool ImGuiInspectorBuilder::Vec2(std::string_view label, float& x, float& y) {
#if defined(HIKARI_WITH_EDITOR)
        float values[2]{ x, y };
        EDITOR::InspectorPropertyRow row(label);
        if (!row.IsVisible() ||
            !ImGui::DragFloat2("##Value", values, 1.0f)) {
            return false;
        }

        x = values[0];
        y = values[1];
        return true;
#else
        (void)label;
        (void)x;
        (void)y;
        return false;
#endif
    }

    bool ImGuiInspectorBuilder::AssetIdPicker(std::string_view label, AssetType assetType, std::string& value) {
#if defined(HIKARI_WITH_EDITOR)
        if (context_.assetDatabase) {
            EDITOR::InspectorPropertyRow row(label);
            if (!row.IsVisible()) {
                return false;
            }
            return EDITOR::DrawAssetField(
                context_.assetDatabase,
                EDITOR::AssetFieldOptions{
                    "##Value",
                    assetType,
                    true,
                    false,
                    true,
                    false
                },
                value);
        }

        if (!context_.assetRegistry) {
            return String(label, value);
        }

        std::vector<const AssetDescriptor*> assets = context_.assetRegistry->CollectByType(assetType);
        std::vector<AssetPickerEntry> entries;
        entries.reserve(assets.size());
        for (const AssetDescriptor* descriptor : assets) {
            if (descriptor && !descriptor->id.value.empty()) {
                entries.push_back(BuildAssetPickerEntry(*descriptor, context_.assetDatabase));
            }
        }
        std::sort(entries.begin(), entries.end(), [](const AssetPickerEntry& lhs, const AssetPickerEntry& rhs) {
            return TEXT::ToLowerAsciiCopy(lhs.name) < TEXT::ToLowerAsciiCopy(rhs.name);
        });

        const AssetPickerEntry* current = nullptr;
        for (const AssetPickerEntry& entry : entries) {
            if (entry.id == value) {
                current = &entry;
                break;
            }
        }

        EDITOR::InspectorPropertyRow row(label);
        if (!row.IsVisible()) {
            return false;
        }
        bool changed = false;
        const std::string labelText(label);
        const std::string previewText = value.empty()
            ? std::string("<none>")
            : (current ? current->name : ("Missing: " + value));

        if (ImGui::BeginCombo("##Value", previewText.c_str())) {
            static char searchBuffer[128]{};
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputText("Search##AssetPicker", searchBuffer, sizeof(searchBuffer));
            const std::string search = TEXT::ToLowerAsciiCopy(searchBuffer);

            const bool isNoneSelected = value.empty();
            if (ImGui::Selectable("<none>", isNoneSelected)) {
                value.clear();
                changed = true;
            }
            if (isNoneSelected) {
                ImGui::SetItemDefaultFocus();
            }

            if (entries.empty()) {
                ImGui::TextDisabled("No matching assets");
            }

            for (const AssetPickerEntry& entry : entries) {
                if (!search.empty() && entry.searchText.find(search) == std::string::npos) {
                    continue;
                }

                const bool selected = (value == entry.id);
                ImGui::PushID(entry.id.c_str());
                if (ImGui::Selectable(entry.label.c_str(), selected)) {
                    value = entry.id;
                    changed = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s\n%s\nGUID: %s",
                        entry.name.c_str(),
                        entry.path.empty() ? "<no source path>" : entry.path.c_str(),
                        entry.id.c_str());
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }

        if (!value.empty()) {
            ImGui::SameLine();
            const std::string copyLabel = "Copy##" + labelText;
            if (ImGui::SmallButton(copyLabel.c_str())) {
                ImGui::SetClipboardText(value.c_str());
            }

            if (current) {
                ImGui::TextDisabled("%s | %s | %s",
                    current->path.empty() ? "<no path>" : current->path.c_str(),
                    current->state.c_str(),
                    ShortGuid(current->id).c_str());
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "Unresolved asset id: %s", value.c_str());
            }
        }

        return changed;
#else
        (void)label;
        (void)assetType;
        (void)value;
        return false;
#endif
    }

    bool ImGuiInspectorBuilder::SceneIdPicker(std::string_view label, std::string& value) {
#if defined(HIKARI_WITH_EDITOR)
        if (!context_.assetDatabase) {
            return String(label, value);
        }

        std::vector<const AssetRecord*> sceneRecords = context_.assetDatabase->CollectByType(AssetType::Scene);
        std::sort(sceneRecords.begin(), sceneRecords.end(), [](const AssetRecord* lhs, const AssetRecord* rhs) {
            if (!lhs || !rhs) {
                return lhs < rhs;
            }
            return lhs->displayName < rhs->displayName;
        });

        EDITOR::InspectorPropertyRow row(label);
        if (!row.IsVisible()) {
            return false;
        }
        bool changed = false;
        const std::string previewText = value.empty() ? std::string("<none>") : value;
        if (ImGui::BeginCombo("##Value", previewText.c_str())) {
            // Scene 鬩包ｽｷ驕假ｽｻ陷亥現繝ｻ Scene Asset 邵ｺ・ｮ GUID 邵ｺ・ｧ鬩包ｽｸ邵ｺ・ｶ邵ｲ繝ｻ
            if (ImGui::Selectable("<none>", value.empty())) {
                value.clear();
                changed = true;
            }
            if (sceneRecords.empty()) {
                ImGui::TextDisabled("No scene assets");
            }
            for (const AssetRecord* record : sceneRecords) {
                if (!record || !record->guid.IsValid()) {
                    continue;
                }
                const bool selected = (record->guid.value == value);
                const std::string itemText = record->displayName.empty()
                    ? record->sourcePath.filename().string()
                    : record->displayName;
                ImGui::PushID(record->guid.value.c_str());
                if (ImGui::Selectable(itemText.c_str(), selected)) {
                    value = record->guid.value;
                    changed = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("%s\n%s",
                        record->sourcePath.generic_string().c_str(),
                        record->guid.value.c_str());
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }

        if (!value.empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Copy")) {
                ImGui::SetClipboardText(value.c_str());
            }
        }

        return changed;
#else
        (void)label;
        (void)value;
        return false;
#endif
    }

    bool ImGuiInspectorBuilder::SceneObjectIdPicker(
        std::string_view label,
        SceneObjectId& value) {
#if defined(HIKARI_WITH_EDITOR)
        EDITOR::InspectorPropertyRow row(label);
        if (!row.IsVisible()) {
            return false;
        }
        if (context_.sceneDocument == nullptr) {
            uint64_t rawValue = value.value;
            if (ImGui::InputScalar(
                    "##Value",
                    ImGuiDataType_U64,
                    &rawValue)) {
                value.value = rawValue;
                return true;
            }
            return false;
        }

        const SceneObjectData* current = nullptr;
        for (const SceneObjectData& object :
                context_.sceneDocument->objects) {
            if (object.id == value) {
                current = &object;
                break;
            }
        }
        const std::string preview = value.value == 0
            ? std::string("<none>")
            : (current != nullptr
                ? current->name + "  [" +
                    std::to_string(value.value) + "]"
                : "Missing Object [" +
                    std::to_string(value.value) + "]");
        bool changed = false;
        if (ImGui::BeginCombo("##Value", preview.c_str())) {
            if (ImGui::Selectable("<none>", value.value == 0)) {
                value = {};
                changed = true;
            }
            for (const SceneObjectData& object :
                    context_.sceneDocument->objects) {
                const bool selected = object.id == value;
                const std::string itemLabel =
                    (object.name.empty() ? "GameObject" : object.name) +
                    "  [" + std::to_string(object.id.value) + "]##" +
                    std::to_string(object.id.value);
                if (ImGui::Selectable(itemLabel.c_str(), selected)) {
                    value = object.id;
                    changed = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        return changed;
#else
        (void)label;
        (void)value;
        return false;
#endif
    }

} // namespace HIKARI
