// Inspector の項目記述はオーサリング専用。Shipping では IComponent の既定実装を使う。
#if defined(HIKARI_WITH_EDITOR)

#include "Scene/Components/Rendering/MaterialFx/HIKARI_MaterialFxComponent.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

#include "Authoring/HIKARI_FieldInspector.h"
#include "Vfx/MaterialFx/HIKARI_MaterialFxProfile.h"

namespace HIKARI {

namespace {
// 一つの参数を key を ID にした一項目で編集する（数値は型付き値、Toggle は真偽）。
bool DrawMaterialFxParamInspector(IInspectorBuilder& builder, const VFX::ParamDesc& param, size_t paramIndex,
                                  DirectX::XMFLOAT4& slotValue) {
    const size_t channel = param.ref.channel;
    if (channel >= 4u) {
        return false;
    }
    float value[4]{slotValue.x, slotValue.y, slotValue.z, slotValue.w};
    const std::string id = param.key.empty() ? "param" + std::to_string(paramIndex) : param.key;
    const AuthoringFieldDescriptor field{id,
                                         param.label.empty() ? std::string_view(id) : std::string_view(param.label)};

    if (param.type == VFX::ParamType::Toggle) {
        bool enabled = value[channel] >= 0.5f;
        if (!builder.Bool(field, enabled)) {
            return false;
        }
        value[channel] = enabled ? 1.0f : 0.0f;
    } else {
        BINDING::Value typed{};
        size_t count = 0u;
        switch (param.type) {
        case VFX::ParamType::Float:
            typed.type = BINDING::ValueType::Float;
            count = 1u;
            break;
        case VFX::ParamType::Float2:
            typed.type = BINDING::ValueType::Float2;
            count = 2u;
            break;
        case VFX::ParamType::Float3:
        case VFX::ParamType::Color3:
            typed.type = BINDING::ValueType::Float3;
            count = 3u;
            break;
        case VFX::ParamType::Float4:
            typed.type = BINDING::ValueType::Float4;
            count = 4u;
            break;
        case VFX::ParamType::Color:
        case VFX::ParamType::Color4:
            typed.type = BINDING::ValueType::Color;
            count = 4u;
            break;
        default: return false;
        }
        if (channel + count > 4u) {
            return false;
        }
        float* components[4]{&typed.numeric.x, &typed.numeric.y, &typed.numeric.z, &typed.numeric.w};
        for (size_t index = 0u; index < count; ++index) {
            *components[index] = value[channel + index];
        }
        const bool hasRange = param.minValues[0] < param.maxValues[0];
        if (!builder.TypedValue(field, typed, hasRange, param.minValues[0], param.maxValues[0], param.speed)) {
            return false;
        }
        for (size_t index = 0u; index < count; ++index) {
            value[channel + index] = *components[index];
        }
    }
    slotValue = {value[0], value[1], value[2], value[3]};
    return true;
}
} // namespace

void MaterialFxComponent::BuildInspector(IInspectorBuilder& builder) {
    MaterialFxSettings settings{parameters_.GetProfileId()};
    if (InspectFields(builder, settings, kMaterialFxFields)) {
        SetProfileId(std::move(settings.profileId));
    }
    // ここから先はプロファイルが決めるパラメータの欄。
    if (parameters_.GetProfileId().empty()) {
        return;
    }

    MaterialFxProfile profile{};
    if (!MaterialFxProfile::LoadById(parameters_.GetProfileId(), profile)) {
        return;
    }

    DirectX::XMFLOAT4 visibleValues[VFX::kMaterialFxUserCount]{};
    if (parameters_.AreValuesInitialized()) {
        std::copy(parameters_.GetValues().begin(), parameters_.GetValues().end(), std::begin(visibleValues));
    } else {
        profile.CopyValuesTo(visibleValues);
    }

    bool changed = false;
    for (size_t paramIndex = 0; paramIndex < profile.params.size(); ++paramIndex) {
        const VFX::ParamDesc& param = profile.params[paramIndex];
        const size_t slot = param.ref.slot;
        if (slot >= std::size(visibleValues) || param.ref.channel >= 4u) {
            continue;
        }

        DirectX::XMFLOAT4 slotValue = visibleValues[slot];
        if (DrawMaterialFxParamInspector(builder, param, paramIndex, slotValue)) {
            visibleValues[slot] = slotValue;
            changed = true;
        }
    }

    if (changed) {
        SetParamValues(visibleValues, true);
    }
}

} // namespace HIKARI

#endif // defined(HIKARI_WITH_EDITOR)
