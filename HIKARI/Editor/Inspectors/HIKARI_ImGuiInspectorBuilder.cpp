#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Inspectors/HIKARI_ImGuiInspectorBuilder.h"
#include "Core/Text/HIKARI_AsciiCase.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetImportState.h"
#include "RenderContracts/Asset/HIKARI_AssetRegistry.h"
#include "RenderContracts/Asset/HIKARI_AssetTypes.h"
#include "Editor/Widgets/HIKARI_AssetFieldWidget.h"
#include "Editor/Widgets/HIKARI_InputActionFieldWidget.h"
#include "Editor/Widgets/HIKARI_InspectorPropertyLayout.h"
#include "Editor/Widgets/HIKARI_NumberField.h"
#include "Editor/Widgets/HIKARI_RuntimeBindingFieldWidget.h"
#include "Editor/Widgets/HIKARI_TypedValueEditor.h"
#include "Input/Runtime/HIKARI_InputService.h"
#include "Runtime/Binding/HIKARI_RuntimeBindingStore.h"
#include "Scene/HIKARI_WorldServiceRegistry.h"
#include "Scene/HIKARI_SceneDocument.h"

#include "imgui.h"
#include <cstring>

namespace HIKARI {

namespace {

struct AssetPickerEntry {
    std::string id{};
    std::string name{};
    std::string path{};
    std::string state{};
    std::string label{};
    std::string searchText{};
};

// 項目 ID を ImGui の ID 範囲にする（同じ表示名の項目が並んでも衝突しない）。読み取り専用の項目は灰色にして触れなくする。
class FieldIdScope {
public:
    explicit FieldIdScope(const AuthoringFieldDescriptor& field) : readOnly_(!field.writable) {
        ImGui::PushID(field.id.data(), field.id.data() + field.id.size());
        if (readOnly_) {
            ImGui::BeginDisabled();
        }
    }
    ~FieldIdScope() {
        if (readOnly_) {
            ImGui::EndDisabled();
        }
        ImGui::PopID();
    }
    FieldIdScope(const FieldIdScope&) = delete;
    FieldIdScope& operator=(const FieldIdScope&) = delete;

private:
    bool readOnly_;
};

using EDITOR::InspectorText;

// 開閉状態などの ImGui ID は原文に結び、言語を切り替えても保つ。
std::string StableImGuiLabel(std::string_view label) {
    std::string result = InspectorText(label);
    if (label.find("##") == std::string_view::npos) {
        result.append("###").append(label);
    }
    return result;
}

std::string FieldDescription(const AuthoringFieldDescriptor& field) {
    return field.description.empty() ? std::string{} : InspectorText(field.description);
}

// 訳した表示名と、ラベルに重ねた時に出す説明で項目行を開く。
EDITOR::InspectorPropertyRow FieldRow(
    const AuthoringFieldDescriptor& field,
    EDITOR::InspectorPropertyValueKind valueKind = EDITOR::InspectorPropertyValueKind::Regular) {
    return EDITOR::InspectorPropertyRow(InspectorText(field.label), valueKind, FieldDescription(field));
}

AssetPickerEntry BuildAssetPickerEntry(const AssetDescriptor& descriptor, const AssetDatabase* assetDatabase) {
    AssetPickerEntry entry{};
    entry.id = descriptor.id.value;
    entry.path = descriptor.sourcePath;

    if (assetDatabase) {
        if (const AssetRecord* record = assetDatabase->FindByGuid(descriptor.id)) {
            entry.name = record->displayName.empty() ? record->sourcePath.stem().string() : record->displayName;
            entry.path = record->sourcePath.generic_string();
            entry.state = ToString(GetImportState(*record));
        }
    }

    if (entry.name.empty()) {
        std::filesystem::path sourcePath{descriptor.sourcePath};
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
} // namespace

void ImGuiInspectorBuilder::SetContext(const InspectorContext& context) { context_ = context; }

bool ImGuiInspectorBuilder::MultilineString(const AuthoringFieldDescriptor& field, std::string& value) {
    const FieldIdScope scope(field);
    // 固定サイズの小さいバッファによる長文切り捨てを避ける。
    std::vector<char> buffer(std::max<size_t>(16385, value.size() + 4096), 0);
    std::copy(value.begin(), value.end(), buffer.begin());
    EDITOR::DrawInspectorPropertyLabel(InspectorText(field.label), ImGui::GetContentRegionAvail().x,
                                       FieldDescription(field));
    const std::string id = "##" + std::string(field.id);
    if (ImGui::InputTextMultiline(id.c_str(), buffer.data(), buffer.size(), {-1, ImGui::GetTextLineHeight() * 5})) {
        value.assign(buffer.data());
        return true;
    }
    return false;
}

const InspectorContext& ImGuiInspectorBuilder::GetContext() const noexcept { return context_; }

void ImGuiInspectorBuilder::Text(std::string_view text) {
    const std::string translated = InspectorText(text);
    ImGui::TextWrapped("%s", translated.c_str());
}

bool ImGuiInspectorBuilder::Section(std::string_view label, bool defaultOpen) {
    const ImGuiTreeNodeFlags flags = defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None;
    return ImGui::CollapsingHeader(StableImGuiLabel(label).c_str(), flags);
}

bool ImGuiInspectorBuilder::Button(std::string_view label) {
    return ImGui::Button(StableImGuiLabel(label).c_str(), ImVec2(-1.0f, 0.0f));
}

bool ImGuiInspectorBuilder::Bool(const AuthoringFieldDescriptor& field, bool& value) {
    const FieldIdScope scope(field);
    const auto row = FieldRow(field, EDITOR::InspectorPropertyValueKind::Compact);
    return row.IsVisible() && ImGui::Checkbox("##Value", &value);
}

bool ImGuiInspectorBuilder::Int(const AuthoringFieldDescriptor& field, int& value) {
    const FieldIdScope scope(field);
    const auto row = FieldRow(field);
    return row.IsVisible() && ImGui::InputInt("##Value", &value);
}

bool ImGuiInspectorBuilder::Float(const AuthoringFieldDescriptor& field, float& value) {
    const FieldIdScope scope(field);
    const auto row = FieldRow(field);
    return row.IsVisible() && EDITOR::DragNumber("##Value", value, {.step = 0.01});
}

bool ImGuiInspectorBuilder::String(const AuthoringFieldDescriptor& field, std::string& value) {
    const FieldIdScope scope(field);
    char buffer[256]{};
    strncpy_s(buffer, value.c_str(), sizeof(buffer) - 1);
    const auto row = FieldRow(field);
    if (row.IsVisible() && ImGui::InputText("##Value", buffer, sizeof(buffer))) {
        value = buffer;
        return true;
    }
    return false;
}

bool ImGuiInspectorBuilder::InputActionIdPicker(const AuthoringFieldDescriptor& field,
                                                INPUT::InputActionValueType expectedType, std::string& value) {
    const FieldIdScope scope(field);
    const auto* inputService =
        context_.worldServices != nullptr ? context_.worldServices->Find<INPUT::InputService>() : nullptr;
    const auto row = FieldRow(field);
    if (!row.IsVisible()) {
        return false;
    }
    if (inputService == nullptr) {
        ImGui::TextDisabled(TEXT::Localize("editor.ui.input.action.map.unavailable.2edb89").c_str());
        return false;
    }
    return EDITOR::DrawInputActionField(inputService->GetActionMap(), "##Value", expectedType, value);
}

bool ImGuiInspectorBuilder::RuntimeVariableIdPicker(const AuthoringFieldDescriptor& field,
                                                    std::span<const BINDING::ValueType> allowedTypes,
                                                    std::string& value) {
    const FieldIdScope scope(field);
    const auto* store =
        context_.worldServices != nullptr ? context_.worldServices->Find<BINDING::RuntimeBindingStore>() : nullptr;
    if (store == nullptr) {
        return String(field, value);
    }
    const auto row = FieldRow(field);
    return row.IsVisible() && EDITOR::DrawRuntimeVariableField(store->GetCatalog(), "##Value", allowedTypes, value);
}

bool ImGuiInspectorBuilder::RuntimeEventIdPicker(const AuthoringFieldDescriptor& field, bool hasPayload,
                                                 BINDING::ValueType payloadType, std::string& value) {
    const FieldIdScope scope(field);
    const auto* store =
        context_.worldServices != nullptr ? context_.worldServices->Find<BINDING::RuntimeBindingStore>() : nullptr;
    if (store == nullptr) {
        return String(field, value);
    }
    const auto row = FieldRow(field);
    return row.IsVisible() &&
           EDITOR::DrawRuntimeEventField(store->GetCatalog(), "##Value", hasPayload, payloadType, value);
}

bool ImGuiInspectorBuilder::RuntimeEventIdPicker(const AuthoringFieldDescriptor& field,
                                                 AuthoringEventPayloadPolicy policy, BINDING::ValueType payloadType,
                                                 std::string& value) {
    const FieldIdScope scope(field);
    const auto* store =
        context_.worldServices != nullptr ? context_.worldServices->Find<BINDING::RuntimeBindingStore>() : nullptr;
    if (store == nullptr) {
        return String(field, value);
    }
    const auto row = FieldRow(field);
    return row.IsVisible() && EDITOR::DrawRuntimeEventField(store->GetCatalog(), "##Value", policy, payloadType, value);
}

bool ImGuiInspectorBuilder::FloatRange(const AuthoringFieldDescriptor& field, float& value, float minimum,
                                       float maximum, float speed) {
    const FieldIdScope scope(field);
    const auto row = FieldRow(field);
    if (!row.IsVisible()) {
        return false;
    }
    if (field.slider) {
        return EDITOR::SliderNumber("##Value", value, minimum, maximum);
    }
    return EDITOR::DragNumber("##Value", value, {.minimum = minimum, .maximum = maximum, .step = speed});
}

bool ImGuiInspectorBuilder::TypedValue(const AuthoringFieldDescriptor& field, BINDING::Value& value, bool hasRange,
                                       float minimum, float maximum, float step) {
    const FieldIdScope scope(field);
    const auto row = FieldRow(field);
    if (!row.IsVisible()) {
        return false;
    }
    const std::string id = "##" + std::string(field.id);
    const EDITOR::TypedValueRange range{hasRange, minimum, maximum, step > 0.0f ? step : 0.01f};
    return EDITOR::DrawTypedEditorValue(id.c_str(), value, range, ImGui::GetContentRegionAvail().x).changed;
}

bool ImGuiInspectorBuilder::Color3(const AuthoringFieldDescriptor& field, float& red, float& green, float& blue) {
    const FieldIdScope scope(field);
    float values[3]{red, green, blue};
    const auto row = FieldRow(field);
    if (!row.IsVisible() ||
        !ImGui::ColorEdit3("##Value", values,
                           ImGuiColorEditFlags_Float | ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_InputRGB)) {
        return false;
    }

    red = values[0];
    green = values[1];
    blue = values[2];
    return true;
}

bool ImGuiInspectorBuilder::Choice(const AuthoringFieldDescriptor& field, int& selectedIndex,
                                   std::span<const AuthoringChoice> choices) {
    const FieldIdScope scope(field);
    if (choices.empty()) {
        return false;
    }
    selectedIndex = std::clamp(selectedIndex, 0, static_cast<int>(choices.size() - 1));
    bool changed = false;
    const auto row = FieldRow(field);
    if (!row.IsVisible()) {
        return false;
    }
    const std::string preview = InspectorText(choices[static_cast<size_t>(selectedIndex)].label);
    if (ImGui::BeginCombo("##Value", preview.c_str())) {
        for (size_t index = 0; index < choices.size(); ++index) {
            const bool selected = static_cast<int>(index) == selectedIndex;
            const std::string label = InspectorText(choices[index].label);
            ImGui::PushID(static_cast<int>(index));
            if (ImGui::Selectable(label.c_str(), selected)) {
                selectedIndex = static_cast<int>(index);
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool ImGuiInspectorBuilder::Vec2(const AuthoringFieldDescriptor& field, float& x, float& y) {
    const FieldIdScope scope(field);
    float values[2]{x, y};
    const auto row = FieldRow(field);
    if (!row.IsVisible() || !EDITOR::DragNumbers("##Value", values, 2, {.step = 0.01})) {
        return false;
    }

    x = values[0];
    y = values[1];
    return true;
}

bool ImGuiInspectorBuilder::Vec3(const AuthoringFieldDescriptor& field, float& x, float& y, float& z) {
    const FieldIdScope scope(field);
    float values[3]{x, y, z};
    const auto row = FieldRow(field);
    if (!row.IsVisible() || !EDITOR::DragNumbers("##Value", values, 3, {.step = 0.01})) {
        return false;
    }

    x = values[0];
    y = values[1];
    z = values[2];
    return true;
}

bool ImGuiInspectorBuilder::ModuleAssetIdPicker(const AuthoringFieldDescriptor& field,
                                                std::string_view moduleAssetTypeId, std::string& value) {
    if (context_.assetDatabase == nullptr) {
        return String(field, value);
    }
    const FieldIdScope scope(field);
    const auto row = FieldRow(field);
    if (!row.IsVisible()) {
        return false;
    }
    EDITOR::AssetFieldOptions options{"##Value", AssetType::ModuleAsset, true, false, true, false};
    options.moduleAssetTypeId = moduleAssetTypeId;
    return EDITOR::DrawAssetField(context_.assetDatabase, options, value);
}

bool ImGuiInspectorBuilder::AssetIdPicker(const AuthoringFieldDescriptor& field, AssetType assetType,
                                          std::string& value) {
    const FieldIdScope scope(field);
    if (context_.assetDatabase) {
        const auto row = FieldRow(field);
        if (!row.IsVisible()) {
            return false;
        }
        return EDITOR::DrawAssetField(context_.assetDatabase,
                                      EDITOR::AssetFieldOptions{"##Value", assetType, true, false, true, false}, value);
    }

    if (!context_.assetRegistry) {
        return String(field, value);
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

    const auto row = FieldRow(field);
    if (!row.IsVisible()) {
        return false;
    }
    bool changed = false;
    const std::string labelText(field.label);
    const std::string previewText =
        value.empty() ? std::string("<none>") : (current ? current->name : ("Missing: " + value));

    if (ImGui::BeginCombo("##Value", previewText.c_str())) {
        static char searchBuffer[128]{};
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText(TEXT::LocalizeWindow("editor.ui.search.bce064", "Search##AssetPicker").c_str(), searchBuffer,
                         sizeof(searchBuffer));
        const std::string search = TEXT::ToLowerAsciiCopy(searchBuffer);

        const bool isNoneSelected = value.empty();
        if (ImGui::Selectable(TEXT::LocalizeWindow("ui.common.none", "<none>").c_str(), isNoneSelected)) {
            value.clear();
            changed = true;
        }
        if (isNoneSelected) {
            ImGui::SetItemDefaultFocus();
        }

        if (entries.empty()) {
            ImGui::TextDisabled(TEXT::Localize("editor.ui.no.matching.assets.be534d").c_str());
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
                ImGui::SetTooltip("%s\n%s\nGUID: %s", entry.name.c_str(),
                                  entry.path.empty() ? "<no source path>" : entry.path.c_str(), entry.id.c_str());
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
            ImGui::TextDisabled("%s | %s | %s", current->path.empty() ? "<no path>" : current->path.c_str(),
                                current->state.c_str(), EDITOR::ShortGuid(current->id).c_str());
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                               TEXT::Localize("editor.ui.unresolved.asset.id.s.4d385d").c_str(), value.c_str());
        }
    }

    return changed;
}

bool ImGuiInspectorBuilder::SceneIdPicker(const AuthoringFieldDescriptor& field, std::string& value) {
    const FieldIdScope scope(field);
    if (!context_.assetDatabase) {
        return String(field, value);
    }

    std::vector<const AssetRecord*> sceneRecords = context_.assetDatabase->CollectByType(AssetType::Scene);
    std::sort(sceneRecords.begin(), sceneRecords.end(), [](const AssetRecord* lhs, const AssetRecord* rhs) {
        if (!lhs || !rhs) {
            return lhs < rhs;
        }
        return lhs->displayName < rhs->displayName;
    });

    const auto row = FieldRow(field);
    if (!row.IsVisible()) {
        return false;
    }
    bool changed = false;
    const std::string previewText = value.empty() ? std::string("<none>") : value;
    if (ImGui::BeginCombo("##Value", previewText.c_str())) {
        // 遷移先は Scene アセットの GUID で選ぶ。
        if (ImGui::Selectable(TEXT::LocalizeWindow("ui.common.none", "<none>").c_str(), value.empty())) {
            value.clear();
            changed = true;
        }
        if (sceneRecords.empty()) {
            ImGui::TextDisabled(TEXT::Localize("editor.ui.no.scene.assets.e5eee2").c_str());
        }
        for (const AssetRecord* record : sceneRecords) {
            if (!record || !record->guid.IsValid()) {
                continue;
            }
            const bool selected = (record->guid.value == value);
            const std::string itemText =
                record->displayName.empty() ? record->sourcePath.filename().string() : record->displayName;
            ImGui::PushID(record->guid.value.c_str());
            if (ImGui::Selectable(itemText.c_str(), selected)) {
                value = record->guid.value;
                changed = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s\n%s", record->sourcePath.generic_string().c_str(), record->guid.value.c_str());
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
        if (ImGui::SmallButton(TEXT::LocalizeWindow("ui.materialGraph.copy", "Copy").c_str())) {
            ImGui::SetClipboardText(value.c_str());
        }
    }

    return changed;
}

bool ImGuiInspectorBuilder::SceneObjectIdPicker(const AuthoringFieldDescriptor& field, SceneObjectId& value) {
    const FieldIdScope scope(field);
    const auto row = FieldRow(field);
    if (!row.IsVisible()) {
        return false;
    }
    if (context_.sceneDocument == nullptr) {
        uint64_t rawValue = value.value;
        if (ImGui::InputScalar("##Value", ImGuiDataType_U64, &rawValue)) {
            value.value = rawValue;
            return true;
        }
        return false;
    }

    const SceneObjectData* current = nullptr;
    for (const SceneObjectData& object : context_.sceneDocument->objects) {
        if (object.id == value) {
            current = &object;
            break;
        }
    }
    const std::string preview = value.value == 0
                                    ? std::string("<none>")
                                    : (current != nullptr ? current->name + "  [" + std::to_string(value.value) + "]"
                                                          : "Missing Object [" + std::to_string(value.value) + "]");
    bool changed = false;
    if (ImGui::BeginCombo("##Value", preview.c_str())) {
        if (ImGui::Selectable(TEXT::LocalizeWindow("ui.common.none", "<none>").c_str(), value.value == 0)) {
            value = {};
            changed = true;
        }
        for (const SceneObjectData& object : context_.sceneDocument->objects) {
            const bool selected = object.id == value;
            const std::string itemLabel = (object.name.empty() ? "GameObject" : object.name) + "  [" +
                                          std::to_string(object.id.value) + "]##" + std::to_string(object.id.value);
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
}

} // namespace HIKARI
