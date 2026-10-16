#include "Editor/Workspaces/Vfx/HIKARI_VfxInspectorControls.h"

#include <array>
#include <cstdio>
#include <string>

#include <json.hpp>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Core/Hashing/HIKARI_FnvHash.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxEditorValue.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"
#include "Core/Text/HIKARI_Localization.h"

#include "Editor/Widgets/HIKARI_NumberField.h"
#include "imgui.h"

namespace HIKARI::EDITOR {
std::string VfxUiText(std::string_view text) { return TEXT::LocalizeSourceText("ui.vfx.text.", text); }
namespace {

nlohmann::json Vec3Json(const MATH::Vec3& value) { return nlohmann::json::array({value.x, value.y, value.z}); }

nlohmann::json Vec4Json(const MATH::Vec4& value) { return nlohmann::json::array({value.x, value.y, value.z, value.w}); }

} // namespace

VfxInspectorPropertyEditor::VfxInspectorPropertyEditor(VfxWorkspaceController& controller, AssetDatabase& assetDatabase,
                                                       std::string_view targetId, std::string& statusMessage) noexcept
    : controller_(controller), assetDatabase_(assetDatabase), targetId_(targetId), statusMessage_(statusMessage) {}

uint64_t VfxInspectorPropertyEditor::MergeGroup(std::string_view targetId, std::string_view propertyId) noexcept {
    uint64_t hash = HASH::Fnv1aString(targetId);
    hash = HASH::Fnv1aBytes(hash, "/", 1u);
    hash = HASH::Fnv1aBytes(hash, propertyId.data(), propertyId.size());
    return hash != 0u ? hash : 1u;
}

void VfxInspectorPropertyEditor::Set(std::string_view propertyId, const nlohmann::json& value, bool continuous) {
    (void)controller_.SetProperty(targetId_, propertyId, value, statusMessage_,
                                  continuous ? MergeGroup(targetId_, propertyId) : 0u, &assetDatabase_);
}

void VfxInspectorPropertyEditor::Text(const char* label, std::string_view propertyId, const std::string& value) {
    std::array<char, 256u> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
    if (ImGui::InputText(VfxUiText(label).c_str(), buffer.data(), buffer.size())) {
        Set(propertyId, std::string(buffer.data()), true);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        controller_.SealPropertyEdit();
    }
}

void VfxInspectorPropertyEditor::Bool(const char* label, std::string_view propertyId, bool value) {
    if (ImGui::Checkbox(VfxUiText(label).c_str(), &value))
        Set(propertyId, value, false);
}

void VfxInspectorPropertyEditor::Float(const char* label, std::string_view propertyId, float value, float speed,
                                       float minimum, float maximum) {
    if (DragNumber(label, value, {.minimum = minimum, .maximum = maximum, .step = speed})) {
        Set(propertyId, value, true);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        controller_.SealPropertyEdit();
    }
}

void VfxInspectorPropertyEditor::Unsigned(const char* label, std::string_view propertyId, uint32_t value,
                                          uint32_t minimum, uint32_t maximum) {
    if (DragNumber(label, value, {.minimum = static_cast<double>(minimum), .maximum = static_cast<double>(maximum)})) {
        Set(propertyId, value, true);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        controller_.SealPropertyEdit();
    }
}

void VfxInspectorPropertyEditor::Vec3(const char* label, std::string_view propertyId, const MATH::Vec3& value,
                                      float speed) {
    MATH::Vec3 edited = value;
    if (DragNumbers(VfxUiText(label).c_str(), &edited.x, 3, {.step = speed})) {
        Set(propertyId, Vec3Json(edited), true);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        controller_.SealPropertyEdit();
    }
}

void VfxInspectorPropertyEditor::Color(const char* label, std::string_view propertyId, const MATH::Vec4& value) {
    MATH::Vec4 edited = value;
    if (ImGui::ColorEdit4(label, &edited.x, ImGuiColorEditFlags_Float)) {
        Set(propertyId, Vec4Json(edited), true);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        controller_.SealPropertyEdit();
    }
}

void VfxInspectorPropertyEditor::TypedValue(const char* label, std::string_view propertyId,
                                            const VFX::VfxValue& value) {
    VFX::VfxValue edited = value;
    const VfxValueEditResult result = DrawVfxValueEditor(label, edited);
    if (result.changed) {
        Set(propertyId, WriteVfxEditorValue(edited), true);
    }
    if (result.committed)
        controller_.SealPropertyEdit();
}

void VfxInspectorPropertyEditor::Choice(const char* label, std::string_view propertyId, std::string_view current,
                                        const std::string_view* choices, size_t choiceCount) {
    if (!ImGui::BeginCombo(VfxUiText(label).c_str(), VfxUiText(current).c_str()))
        return;
    for (size_t index = 0u; index < choiceCount; ++index) {
        if (ImGui::Selectable(VfxUiText(choices[index]).c_str(), choices[index] == current)) {
            Set(propertyId, choices[index], false);
        }
    }
    ImGui::EndCombo();
}

} // namespace HIKARI::EDITOR
