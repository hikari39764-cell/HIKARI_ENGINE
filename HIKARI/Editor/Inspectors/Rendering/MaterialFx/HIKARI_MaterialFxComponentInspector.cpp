#include "Scene/Components/Rendering/MaterialFx/HIKARI_MaterialFxComponent.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <string>
#include <utility>

#include "Editor/Inspectors/HIKARI_IInspectorBuilder.h"
#include "Editor/Widgets/HIKARI_PostProfileParameterWidget.h"
#include "Vfx/MaterialFx/HIKARI_MaterialFxProfile.h"

#if defined(HIKARI_ENABLE_IMGUI)
#include "imgui.h"
#endif

namespace HIKARI {

    namespace {
        bool DrawMaterialFxParamInspector(
            IInspectorBuilder& builder,
            const VFX::ParamDesc& param,
            size_t paramIndex,
            DirectX::XMFLOAT4& slotValue) {

            if (param.ref.channel >= 4u) {
                return false;
            }

            float value[4]{
                slotValue.x,
                slotValue.y,
                slotValue.z,
                slotValue.w
            };
            const std::string baseLabel =
                param.label.empty()
                    ? (param.key.empty()
                        ? "Param " + std::to_string(paramIndex)
                        : param.key)
                    : param.label;
            const std::string idSuffix =
                "##MaterialFxInspector_" +
                std::to_string(paramIndex);

            bool changed = false;
            auto editFloat = [&](
                const char* channelName,
                uint8_t channel) {

                if (channel >= 4u) {
                    return;
                }
                float nextValue = value[channel];
                if (builder.Float(
                    baseLabel + " " + channelName + idSuffix,
                    nextValue)) {

                    value[channel] = nextValue;
                    changed = true;
                }
            };

            switch (param.type) {
            case VFX::ParamType::Float:
                editFloat("", param.ref.channel);
                break;
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
                    editFloat(
                        "Y",
                        static_cast<uint8_t>(
                            param.ref.channel + 1u));
                    editFloat(
                        "Z",
                        static_cast<uint8_t>(
                            param.ref.channel + 2u));
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
                if (builder.Bool(
                    baseLabel + idSuffix,
                    enabled)) {

                    value[param.ref.channel] =
                        enabled ? 1.0f : 0.0f;
                    changed = true;
                }
                break;
            }
            default:
                break;
            }

            if (changed) {
                slotValue = {
                    value[0],
                    value[1],
                    value[2],
                    value[3]
                };
            }
            return changed;
        }
    } // namespace

    void MaterialFxComponent::BuildInspector(
        IInspectorBuilder& builder) {

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

        DirectX::XMFLOAT4
            visibleValues[VFX::kMaterialFxUserCount]{};
        if (valuesInitialized_) {
            std::copy(
                std::begin(paramValues_),
                std::end(paramValues_),
                std::begin(visibleValues));
        } else {
            profile.CopyValuesTo(visibleValues);
        }

        bool changed = false;
        for (size_t paramIndex = 0;
            paramIndex < profile.params.size();
            ++paramIndex) {

            const VFX::ParamDesc& param =
                profile.params[paramIndex];
            const size_t slot = param.ref.slot;
            if (slot >= std::size(visibleValues) ||
                param.ref.channel >= 4u) {

                continue;
            }

            DirectX::XMFLOAT4 slotValue =
                visibleValues[slot];
            if (DrawMaterialFxParamInspector(
                builder,
                param,
                paramIndex,
                slotValue)) {

                visibleValues[slot] = slotValue;
                changed = true;
            }
        }

        if (changed) {
            SetParamValues(visibleValues, true);
        }
    }

    void MaterialFxComponent::RenderImGui() {
#if defined(HIKARI_ENABLE_IMGUI)
        bool changed = false;

        char profileBuffer[256]{};
        std::strncpy(
            profileBuffer,
            profileId_.c_str(),
            sizeof(profileBuffer) - 1u);
        if (ImGui::InputText(
            "Profile##RuntimeMaterialFxProfile",
            profileBuffer,
            sizeof(profileBuffer))) {

            const std::string nextProfileId(profileBuffer);
            if (nextProfileId != profileId_) {
                SetProfileId(nextProfileId);
            }
        }

        if (!profileId_.empty()) {
            MaterialFxProfile profile{};
            if (MaterialFxProfile::LoadById(
                profileId_,
                profile)) {

                DirectX::XMFLOAT4
                    visibleValues[VFX::kMaterialFxUserCount]{};
                if (valuesInitialized_) {
                    std::copy(
                        std::begin(paramValues_),
                        std::end(paramValues_),
                        std::begin(visibleValues));
                } else {
                    profile.CopyValuesTo(visibleValues);
                    ImGui::TextDisabled(
                        "Using profile defaults until a parameter is edited.");
                }

                if (ImGui::Button(
                    "Reset Defaults##MaterialFx")) {

                    profile.CopyValuesTo(paramValues_);
                    profile.CopyValuesTo(visibleValues);
                    valuesInitialized_ = true;
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Button(
                    "Reload Profile##MaterialFx")) {

                    MaterialFxProfile::ClearCache();
                    MaterialFxProfile reloadedProfile{};
                    if (MaterialFxProfile::LoadById(
                        profileId_,
                        reloadedProfile)) {

                        profile = std::move(reloadedProfile);
                        if (!valuesInitialized_) {
                            profile.CopyValuesTo(visibleValues);
                        }
                    }
                }

                if (ImGui::TreeNode(
                    "Parameters##MaterialFxParams")) {

                    const auto ensureWritableValues = [&]() {
                        if (valuesInitialized_) {
                            return;
                        }
                        std::copy(
                            std::begin(visibleValues),
                            std::end(visibleValues),
                            std::begin(paramValues_));
                        valuesInitialized_ = true;
                    };

                    for (size_t paramIndex = 0;
                        paramIndex < profile.params.size();
                        ++paramIndex) {

                        const VFX::ParamDesc& param =
                            profile.params[paramIndex];
                        const size_t slot = param.ref.slot;
                        if (slot >= std::size(paramValues_) ||
                            param.ref.channel >= 4u) {

                            continue;
                        }

                        ImGui::PushID(
                            static_cast<int>(paramIndex));
                        DirectX::XMFLOAT4 slotValue =
                            visibleValues[slot];
                        if (EDITOR::DrawPostProfileParameter(
                            param,
                            slotValue)) {

                            visibleValues[slot] = slotValue;
                            ensureWritableValues();
                            paramValues_[slot] = slotValue;
                            changed = true;
                        }
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
            } else {
                ImGui::TextColored(
                    ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
                    "Profile not found: %s",
                    profileId_.c_str());
            }
        }

        if (ImGui::TreeNode(
            "Advanced Raw Block (4x float4)##MaterialFxRawBlock")) {

            for (size_t i = 0;
                i < std::size(paramValues_);
                ++i) {

                ImGui::PushID(static_cast<int>(i));
                if (ImGui::InputFloat4(
                    "Param",
                    &paramValues_[i].x)) {

                    valuesInitialized_ = true;
                    changed = true;
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }

        if (changed) {
            NotifyRenderStateDirty();
        }
#endif
    }

} // namespace HIKARI
