#include "Editor/SystemAuthoring/HIKARI_SystemSettingsFieldRenderer.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
    namespace {

#if defined(HIKARI_WITH_EDITOR)
        nlohmann::json* ResolveFieldValue(
            nlohmann::json& settings,
            const SystemSettingField& field) {

            if (field.jsonPointer.empty() ||
                field.jsonPointer.front() != '/') {
                return nullptr;
            }
            try {
                const nlohmann::json::json_pointer pointer(
                    field.jsonPointer);
                return &settings[pointer];
            } catch (const nlohmann::json::exception&) {
                return nullptr;
            }
        }

        bool DrawBooleanField(
            const SystemSettingField& field,
            nlohmann::json& value) {

            if (!value.is_boolean()) {
                value = false;
            }
            bool edited = value.get<bool>();
            if (!ImGui::Checkbox(field.displayName.c_str(), &edited)) {
                return false;
            }
            value = edited;
            return true;
        }

        bool DrawIntegerField(
            const SystemSettingField& field,
            nlohmann::json& value) {

            if (!value.is_number_integer()) {
                value = 0;
            }
            int64_t edited = value.get<int64_t>();
            const int64_t minimum = field.hasRange
                ? static_cast<int64_t>(field.minimum)
                : (std::numeric_limits<int64_t>::lowest)();
            const int64_t maximum = field.hasRange
                ? static_cast<int64_t>(field.maximum)
                : (std::numeric_limits<int64_t>::max)();
            const float speed = static_cast<float>((std::max)(
                field.step, 1.0));
            if (!ImGui::DragScalar(
                    field.displayName.c_str(),
                    ImGuiDataType_S64,
                    &edited,
                    speed,
                    field.hasRange ? &minimum : nullptr,
                    field.hasRange ? &maximum : nullptr)) {
                return false;
            }
            value = edited;
            return true;
        }

        bool DrawFloatField(
            const SystemSettingField& field,
            nlohmann::json& value) {

            if (!value.is_number()) {
                value = 0.0;
            }
            double edited = value.get<double>();
            const double minimum = field.minimum;
            const double maximum = field.maximum;
            if (!ImGui::DragScalar(
                    field.displayName.c_str(),
                    ImGuiDataType_Double,
                    &edited,
                    static_cast<float>((std::max)(field.step, 0.0001)),
                    field.hasRange ? &minimum : nullptr,
                    field.hasRange ? &maximum : nullptr,
                    "%.4f")) {
                return false;
            }
            value = edited;
            return true;
        }

        bool DrawStringField(
            const SystemSettingField& field,
            nlohmann::json& value) {

            if (!value.is_string()) {
                value = std::string{};
            }
            constexpr size_t kMaximumTextLength = 1024;
            std::array<char, kMaximumTextLength> buffer{};
            const std::string current = value.get<std::string>();
            std::memcpy(
                buffer.data(),
                current.data(),
                (std::min)(current.size(), buffer.size() - 1));
            if (!ImGui::InputText(
                    field.displayName.c_str(),
                    buffer.data(),
                    buffer.size())) {
                return false;
            }
            value = std::string(buffer.data());
            return true;
        }

        bool DrawEnumField(
            const SystemSettingField& field,
            nlohmann::json& value) {

            if (!value.is_string()) {
                value = field.options.empty()
                    ? std::string{}
                    : field.options.front().value;
            }
            const std::string current = value.get<std::string>();
            const auto selected = std::find_if(
                field.options.begin(),
                field.options.end(),
                [&current](const SystemSettingOption& option) {
                    return option.value == current;
                });
            const char* preview = selected != field.options.end()
                ? selected->displayName.c_str()
                : current.c_str();
            if (!ImGui::BeginCombo(field.displayName.c_str(), preview)) {
                return false;
            }
            bool changed = false;
            for (const SystemSettingOption& option : field.options) {
                const bool isSelected = option.value == current;
                if (ImGui::Selectable(
                        option.displayName.c_str(),
                        isSelected)) {
                    value = option.value;
                    changed = true;
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
            return changed;
        }

        bool DrawVec3Field(
            const SystemSettingField& field,
            nlohmann::json& value) {

            if (!value.is_array() || value.size() != 3) {
                value = nlohmann::json::array({ 0.0, 0.0, 0.0 });
            }
            std::array<float, 3> edited{
                value[0].is_number() ? value[0].get<float>() : 0.0f,
                value[1].is_number() ? value[1].get<float>() : 0.0f,
                value[2].is_number() ? value[2].get<float>() : 0.0f,
            };
            const float minimum = static_cast<float>(field.minimum);
            const float maximum = static_cast<float>(field.maximum);
            if (!ImGui::DragFloat3(
                    field.displayName.c_str(),
                    edited.data(),
                    static_cast<float>((std::max)(field.step, 0.0001)),
                    field.hasRange ? minimum : 0.0f,
                    field.hasRange ? maximum : 0.0f)) {
                return false;
            }
            value = nlohmann::json::array({
                edited[0], edited[1], edited[2] });
            return true;
        }

        bool DrawField(
            const SystemSettingField& field,
            nlohmann::json& settings) {

            nlohmann::json* value = ResolveFieldValue(settings, field);
            if (!value) {
                ImGui::TextColored(
                    ImVec4(1.0f, 0.4f, 0.35f, 1.0f),
                    "Invalid settings path: %s",
                    field.jsonPointer.c_str());
                return false;
            }

            ImGui::PushID(field.jsonPointer.c_str());
            bool changed = false;
            switch (field.type) {
            case SystemSettingFieldType::Boolean:
                changed = DrawBooleanField(field, *value);
                break;
            case SystemSettingFieldType::Integer:
                changed = DrawIntegerField(field, *value);
                break;
            case SystemSettingFieldType::Float:
                changed = DrawFloatField(field, *value);
                break;
            case SystemSettingFieldType::String:
                changed = DrawStringField(field, *value);
                break;
            case SystemSettingFieldType::Enum:
                changed = DrawEnumField(field, *value);
                break;
            case SystemSettingFieldType::Vec3:
                changed = DrawVec3Field(field, *value);
                break;
            }
            if (!field.description.empty() && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", field.description.c_str());
            }
            ImGui::PopID();
            return changed;
        }
#endif
    }

    bool DrawSystemSettingsFields(
        const std::vector<SystemSettingField>& fields,
        nlohmann::json& settings,
        bool advanced) {

#if defined(HIKARI_WITH_EDITOR)
        bool changed = false;
        for (const SystemSettingField& field : fields) {
            if (field.advanced == advanced) {
                changed |= DrawField(field, settings);
            }
        }
        return changed;
#else
        (void)fields;
        (void)settings;
        (void)advanced;
        return false;
#endif
    }

} // namespace HIKARI::EDITOR
