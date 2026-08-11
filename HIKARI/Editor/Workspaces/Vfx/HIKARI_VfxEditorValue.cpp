#include "Editor/Workspaces/Vfx/HIKARI_VfxEditorValue.h"

#include <cstddef>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
    namespace {

        size_t ComponentCount(VFX::VfxValueType type) noexcept {
            switch (type) {
            case VFX::VfxValueType::Float2:
                return 2u;
            case VFX::VfxValueType::Float3:
                return 3u;
            case VFX::VfxValueType::Float4:
            case VFX::VfxValueType::Color:
                return 4u;
            default:
                return 1u;
            }
        }

    } // namespace

    bool TryReadVfxEditorValue(
        const nlohmann::json& node,
        VFX::VfxValueType expectedType,
        VFX::VfxValue& outValue,
        std::string& outMessage) {

        VFX::VfxValue value = VFX::MakeDefaultVfxValue(expectedType);
        try {
            switch (expectedType) {
            case VFX::VfxValueType::Bool:
                if (!node.is_boolean()) {
                    outMessage = "Expected a boolean VFX value";
                    return false;
                }
                value.boolean = node.get<bool>();
                break;
            case VFX::VfxValueType::Int:
                if (!node.is_number_integer()) {
                    outMessage = "Expected an integer VFX value";
                    return false;
                }
                value.integer = node.get<int64_t>();
                break;
            case VFX::VfxValueType::Float:
                if (!node.is_number()) {
                    outMessage = "Expected a numeric VFX value";
                    return false;
                }
                value.numeric.x = node.get<float>();
                break;
            case VFX::VfxValueType::Float2:
            case VFX::VfxValueType::Float3:
            case VFX::VfxValueType::Float4:
            case VFX::VfxValueType::Color: {
                const size_t count = ComponentCount(expectedType);
                if (!node.is_array() || node.size() != count) {
                    outMessage = "Expected a VFX numeric array with " +
                        std::to_string(count) + " components";
                    return false;
                }
                float* components[] = {
                    &value.numeric.x,
                    &value.numeric.y,
                    &value.numeric.z,
                    &value.numeric.w,
                };
                for (size_t index = 0u; index < count; ++index) {
                    if (!node[index].is_number()) {
                        outMessage =
                            "VFX value components must be numeric";
                        return false;
                    }
                    *components[index] = node[index].get<float>();
                }
                break;
            }
            }
        } catch (const nlohmann::json::exception& exception) {
            outMessage = "Invalid VFX value: " +
                std::string(exception.what());
            return false;
        }
        outValue = value;
        outMessage.clear();
        return true;
    }

    nlohmann::json WriteVfxEditorValue(
        const VFX::VfxValue& value) {

        switch (value.type) {
        case VFX::VfxValueType::Bool:
            return value.boolean;
        case VFX::VfxValueType::Int:
            return value.integer;
        case VFX::VfxValueType::Float:
            return value.numeric.x;
        case VFX::VfxValueType::Float2:
            return nlohmann::json::array({
                value.numeric.x,
                value.numeric.y,
            });
        case VFX::VfxValueType::Float3:
            return nlohmann::json::array({
                value.numeric.x,
                value.numeric.y,
                value.numeric.z,
            });
        case VFX::VfxValueType::Float4:
        case VFX::VfxValueType::Color:
            return nlohmann::json::array({
                value.numeric.x,
                value.numeric.y,
                value.numeric.z,
                value.numeric.w,
            });
        }
        return nullptr;
    }

#if defined(HIKARI_WITH_EDITOR)
    VfxValueEditResult DrawVfxValueEditor(
        const char* id,
        VFX::VfxValue& value,
        float width) {

        VfxValueEditResult result{};
        ImGui::SetNextItemWidth(width);
        switch (value.type) {
        case VFX::VfxValueType::Bool:
            result.changed = ImGui::Checkbox(id, &value.boolean);
            result.committed = result.changed;
            break;
        case VFX::VfxValueType::Int:
            result.changed = ImGui::DragScalar(
                id,
                ImGuiDataType_S64,
                &value.integer,
                1.0f);
            result.committed = ImGui::IsItemDeactivatedAfterEdit();
            break;
        case VFX::VfxValueType::Float:
            result.changed = ImGui::DragFloat(
                id, &value.numeric.x, 0.01f);
            result.committed = ImGui::IsItemDeactivatedAfterEdit();
            break;
        case VFX::VfxValueType::Float2:
            result.changed = ImGui::DragFloat2(
                id, &value.numeric.x, 0.01f);
            result.committed = ImGui::IsItemDeactivatedAfterEdit();
            break;
        case VFX::VfxValueType::Float3:
            result.changed = ImGui::DragFloat3(
                id, &value.numeric.x, 0.01f);
            result.committed = ImGui::IsItemDeactivatedAfterEdit();
            break;
        case VFX::VfxValueType::Float4:
            result.changed = ImGui::DragFloat4(
                id, &value.numeric.x, 0.01f);
            result.committed = ImGui::IsItemDeactivatedAfterEdit();
            break;
        case VFX::VfxValueType::Color:
            result.changed = ImGui::ColorEdit4(
                id,
                &value.numeric.x,
                ImGuiColorEditFlags_Float);
            result.committed = ImGui::IsItemDeactivatedAfterEdit();
            break;
        }
        return result;
    }
#endif

} // namespace HIKARI::EDITOR
