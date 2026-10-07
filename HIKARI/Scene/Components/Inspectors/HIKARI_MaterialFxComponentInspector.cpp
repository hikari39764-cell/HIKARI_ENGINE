// Inspector の項目記述はオーサリング専用。Shipping では IComponent の既定実装を使う。
#if defined(HIKARI_WITH_EDITOR)

#include "Scene/Components/Rendering/MaterialFx/HIKARI_MaterialFxComponent.h"

#include <algorithm>
#include <iterator>
#include <string>
#include <utility>

#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Vfx/MaterialFx/HIKARI_MaterialFxProfile.h"

namespace HIKARI {

namespace {
bool DrawMaterialFxParamInspector(IInspectorBuilder& builder, const VFX::ParamDesc& param, size_t paramIndex,
                                  DirectX::XMFLOAT4& slotValue) {
    if (param.ref.channel >= 4u) {
        return false;
    }

    float value[4]{slotValue.x, slotValue.y, slotValue.z, slotValue.w};
    const std::string baseLabel =
        param.label.empty() ? (param.key.empty() ? "Param " + std::to_string(paramIndex) : param.key) : param.label;
    const std::string idSuffix = "##MaterialFxInspector_" + std::to_string(paramIndex);

    bool changed = false;
    auto editFloat = [&](const char* channelName, uint8_t channel) {
        if (channel >= 4u) {
            return;
        }
        float nextValue = value[channel];
        if (builder.Float(baseLabel + " " + channelName + idSuffix, nextValue)) {
            value[channel] = nextValue;
            changed = true;
        }
    };

    switch (param.type) {
    case VFX::ParamType::Float: editFloat("", param.ref.channel); break;
    case VFX::ParamType::Float2:
        if (param.ref.channel <= 2u) {
            float x = value[param.ref.channel];
            float y = value[param.ref.channel + 1u];
            if (builder.Vec2(baseLabel + idSuffix, x, y)) {
                value[param.ref.channel] = x;
                value[param.ref.channel + 1u] = y;
                changed = true;
            }
        }
        break;
    case VFX::ParamType::Float3:
    case VFX::ParamType::Color3:
        if (param.ref.channel <= 1u) {
            editFloat("X", param.ref.channel);
            editFloat("Y", static_cast<uint8_t>(param.ref.channel + 1u));
            editFloat("Z", static_cast<uint8_t>(param.ref.channel + 2u));
        }
        break;
    case VFX::ParamType::Float4:
    case VFX::ParamType::Color:
    case VFX::ParamType::Color4:
        if (param.ref.channel == 0u) {
            editFloat("X", 0u);
            editFloat("Y", 1u);
            editFloat("Z", 2u);
            editFloat("W", 3u);
        }
        break;
    case VFX::ParamType::Toggle: {
        bool enabled = value[param.ref.channel] >= 0.5f;
        if (builder.Bool(baseLabel + idSuffix, enabled)) {
            value[param.ref.channel] = enabled ? 1.0f : 0.0f;
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
} // namespace

void MaterialFxComponent::BuildInspector(IInspectorBuilder& builder) {
    const std::string& profileId_ = parameters_.profileId_;
    const auto& paramValues_ = parameters_.values_;
    const bool valuesInitialized_ = parameters_.valuesInitialized_;

    std::string profileId = profileId_;
    if (builder.String("Profile", profileId)) {
        SetProfileId(std::move(profileId));
    }
    if (profileId_.empty()) {
        return;
    }

    MaterialFxProfile profile{};
    if (!MaterialFxProfile::LoadById(profileId_, profile)) {
        return;
    }

    DirectX::XMFLOAT4 visibleValues[VFX::kMaterialFxUserCount]{};
    if (valuesInitialized_) {
        std::copy(std::begin(paramValues_), std::end(paramValues_), std::begin(visibleValues));
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
