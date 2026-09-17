#include "Editor/Workspaces/Vfx/HIKARI_VfxInspectorControls.h"

#include <array>
#include <cstdio>
#include <string>

#include <json.hpp>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxEditorValue.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"
#include "Core/Text/HIKARI_Localization.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
    std::string VfxUiText(std::string_view text) {
        const auto key = "ui.vfx.text." + std::string(text);
        auto translated = TEXT::Localize(key);
        return translated == key ? std::string(text) : translated;
    }
    namespace {

        nlohmann::json Vec3Json(const MATH::Vec3& value) {
            return nlohmann::json::array({ value.x, value.y, value.z });
        }

        nlohmann::json Vec4Json(const MATH::Vec4& value) {
            return nlohmann::json::array({
                value.x, value.y, value.z, value.w
            });
        }

    } // namespace

    VfxInspectorPropertyEditor::VfxInspectorPropertyEditor(
        VfxWorkspaceController& controller,
        AssetDatabase& assetDatabase,
        std::string_view targetId,
        std::string& statusMessage) noexcept
        : controller_(controller),
          assetDatabase_(assetDatabase),
          targetId_(targetId),
          statusMessage_(statusMessage) {}

    uint64_t VfxInspectorPropertyEditor::MergeGroup(
        std::string_view targetId,
        std::string_view propertyId) noexcept {

        uint64_t hash = 1469598103934665603ull;
        const auto append = [&hash](std::string_view text) {
            for (const unsigned char character : text) {
                hash ^= character;
                hash *= 1099511628211ull;
            }
        };
        append(targetId);
        append("/");
        append(propertyId);
        return hash != 0u ? hash : 1u;
    }

    void VfxInspectorPropertyEditor::Set(
        std::string_view propertyId,
        const nlohmann::json& value,
        bool continuous) {

        (void)controller_.SetProperty(
            targetId_,
            propertyId,
            value,
            statusMessage_,
            continuous ? MergeGroup(targetId_, propertyId) : 0u,
            &assetDatabase_);
    }

    void VfxInspectorPropertyEditor::Text(
        const char* label,
        std::string_view propertyId,
        const std::string& value) {
#if defined(HIKARI_WITH_EDITOR)
        std::array<char, 256u> buffer{};
        std::snprintf(buffer.data(), buffer.size(), "%s", value.c_str());
        if (ImGui::InputText(VfxUiText(label).c_str(), buffer.data(), buffer.size())) {
            Set(propertyId, std::string(buffer.data()), true);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            controller_.SealPropertyEdit();
        }
#else
        (void)label;
        (void)propertyId;
        (void)value;
#endif
    }

    void VfxInspectorPropertyEditor::Bool(
        const char* label,
        std::string_view propertyId,
        bool value) {
#if defined(HIKARI_WITH_EDITOR)
        if (ImGui::Checkbox(VfxUiText(label).c_str(), &value)) Set(propertyId, value, false);
#else
        (void)label;
        (void)propertyId;
        (void)value;
#endif
    }

    void VfxInspectorPropertyEditor::Float(
        const char* label,
        std::string_view propertyId,
        float value,
        float speed,
        float minimum,
        float maximum) {
#if defined(HIKARI_WITH_EDITOR)
        if (ImGui::DragFloat(
                label, &value, speed, minimum, maximum)) {
            Set(propertyId, value, true);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            controller_.SealPropertyEdit();
        }
#else
        (void)label;
        (void)propertyId;
        (void)value;
        (void)speed;
        (void)minimum;
        (void)maximum;
#endif
    }

    void VfxInspectorPropertyEditor::Unsigned(
        const char* label,
        std::string_view propertyId,
        uint32_t value,
        uint32_t minimum,
        uint32_t maximum) {
#if defined(HIKARI_WITH_EDITOR)
        if (ImGui::DragScalar(
                label,
                ImGuiDataType_U32,
                &value,
                1.0f,
                &minimum,
                &maximum)) {
            Set(propertyId, value, true);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            controller_.SealPropertyEdit();
        }
#else
        (void)label;
        (void)propertyId;
        (void)value;
        (void)minimum;
        (void)maximum;
#endif
    }

    void VfxInspectorPropertyEditor::Vec3(
        const char* label,
        std::string_view propertyId,
        const MATH::Vec3& value,
        float speed) {
#if defined(HIKARI_WITH_EDITOR)
        MATH::Vec3 edited = value;
        if (ImGui::DragFloat3(VfxUiText(label).c_str(), &edited.x, speed)) {
            Set(propertyId, Vec3Json(edited), true);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            controller_.SealPropertyEdit();
        }
#else
        (void)label;
        (void)propertyId;
        (void)value;
        (void)speed;
#endif
    }

    void VfxInspectorPropertyEditor::Color(
        const char* label,
        std::string_view propertyId,
        const MATH::Vec4& value) {
#if defined(HIKARI_WITH_EDITOR)
        MATH::Vec4 edited = value;
        if (ImGui::ColorEdit4(
                label, &edited.x, ImGuiColorEditFlags_Float)) {
            Set(propertyId, Vec4Json(edited), true);
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            controller_.SealPropertyEdit();
        }
#else
        (void)label;
        (void)propertyId;
        (void)value;
#endif
    }

    void VfxInspectorPropertyEditor::TypedValue(
        const char* label,
        std::string_view propertyId,
        const VFX::VfxValue& value) {
#if defined(HIKARI_WITH_EDITOR)
        VFX::VfxValue edited = value;
        const VfxValueEditResult result =
            DrawVfxValueEditor(label, edited);
        if (result.changed) {
            Set(propertyId, WriteVfxEditorValue(edited), true);
        }
        if (result.committed) controller_.SealPropertyEdit();
#else
        (void)label;
        (void)propertyId;
        (void)value;
#endif
    }

    void VfxInspectorPropertyEditor::Choice(
        const char* label,
        std::string_view propertyId,
        std::string_view current,
        const std::string_view* choices,
        size_t choiceCount) {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::BeginCombo(VfxUiText(label).c_str(), VfxUiText(current).c_str())) return;
        for (size_t index = 0u; index < choiceCount; ++index) {
            if (ImGui::Selectable(
                    VfxUiText(choices[index]).c_str(), choices[index] == current)) {
                Set(propertyId, choices[index], false);
            }
        }
        ImGui::EndCombo();
#else
        (void)label;
        (void)propertyId;
        (void)current;
        (void)choices;
        (void)choiceCount;
#endif
    }


} // namespace HIKARI::EDITOR
