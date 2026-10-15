#include "Editor/Widgets/HIKARI_FxParameterWidget.h"

#include "Editor/Widgets/HIKARI_NumberField.h"
#include "imgui.h"

namespace HIKARI::EDITOR {

bool DrawFxParameter(const VFX::ParamDesc& parameter, DirectX::XMFLOAT4& slotValue) {
    if (parameter.ref.channel >= 4) {
        return false;
    }

    float value[4] = {slotValue.x, slotValue.y, slotValue.z, slotValue.w};
    const std::string labelText = parameter.label.empty() ? parameter.key : parameter.label;
    const char* label = labelText.empty() ? "<unnamed>" : labelText.c_str();
    bool changed = false;
    const NumberRange range{
        .minimum = parameter.minValues[0], .maximum = parameter.maxValues[0], .step = parameter.speed};

    switch (parameter.type) {
    case VFX::ParamType::Float:
        changed = DragNumber(label, value[parameter.ref.channel], range, &parameter.defaultValues[0]);
        break;
    case VFX::ParamType::Float2:
        if (parameter.ref.channel <= 2) {
            changed = DragNumbers(label, &value[parameter.ref.channel], 2, range, parameter.defaultValues);
        }
        break;
    case VFX::ParamType::Float3:
        if (parameter.ref.channel <= 1) {
            changed = DragNumbers(label, &value[parameter.ref.channel], 3, range, parameter.defaultValues);
        }
        break;
    case VFX::ParamType::Float4:
        if (parameter.ref.channel == 0) {
            changed = DragNumbers(label, value, 4, range, parameter.defaultValues);
        }
        break;
    case VFX::ParamType::Color3:
        if (parameter.ref.channel <= 1) {
            changed = ImGui::ColorEdit3(label, &value[parameter.ref.channel]);
        }
        break;
    case VFX::ParamType::Color:
    case VFX::ParamType::Color4:
        if (parameter.ref.channel == 0) {
            changed = ImGui::ColorEdit4(label, value);
        }
        break;
    case VFX::ParamType::Toggle: {
        bool enabled = value[parameter.ref.channel] >= 0.5f;
        if (ImGui::Checkbox(label, &enabled)) {
            value[parameter.ref.channel] = enabled ? 1.0f : 0.0f;
            changed = true;
        }
        break;
    }
    default: break;
    }

    if (changed) {
        slotValue = {value[0], value[1], value[2], value[3]};
    }
    return changed;
}

} // namespace HIKARI::EDITOR
