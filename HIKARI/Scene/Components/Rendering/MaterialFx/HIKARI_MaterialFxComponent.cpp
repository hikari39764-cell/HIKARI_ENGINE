#include "Scene/Components/Rendering/MaterialFx/HIKARI_MaterialFxComponent.h"

#include <algorithm>
#include <iterator>
#include <utility>

#include "Scene/HIKARI_GameObject.h"
#include "Vfx/MaterialFx/HIKARI_MaterialFxProfile.h"

namespace HIKARI {

    namespace {
        bool ResolveParamRef(
            const MaterialFxProfile& profile,
            const std::string& key,
            VFX::ParamChannelRef& outRef,
            VFX::ParamType* outType = nullptr) {

            for (const VFX::ParamDesc& param : profile.params) {
                if (param.key != key) {
                    continue;
                }
                outRef = param.ref;
                if (outType != nullptr) {
                    *outType = param.type;
                }
                return true;
            }
            return false;
        }

        bool EnsureValuesReady(
            const std::string& profileId,
            DirectX::XMFLOAT4
                (&values)[VFX::kMaterialFxUserCount],
            bool& initialized) {

            if (initialized) {
                return true;
            }
            if (profileId.empty()) {
                return false;
            }

            MaterialFxProfile profile{};
            if (!MaterialFxProfile::LoadById(profileId, profile)) {
                return false;
            }
            profile.CopyValuesTo(values);
            initialized = true;
            return true;
        }
    } // namespace

    void MaterialFxComponent::NotifyRenderStateDirty() {
        if (GameObject* owner = GetOwner()) {
            owner->MarkRenderStateDirty();
        }
    }

    void MaterialFxComponent::SetProfileId(
        std::string profileId) {

        profileId_ = std::move(profileId);
        ResetToProfileDefaults();
    }

    const std::string& MaterialFxComponent::GetProfileId() const {
        return profileId_;
    }

    const DirectX::XMFLOAT4
        (&MaterialFxComponent::GetParamValues() const)
        [VFX::kMaterialFxUserCount] {

        return paramValues_;
    }

    bool MaterialFxComponent::AreValuesInitialized() const {
        return valuesInitialized_;
    }

    void MaterialFxComponent::SetParamValues(
        const DirectX::XMFLOAT4
            (&values)[VFX::kMaterialFxUserCount],
        bool initialized) {

        std::copy(
            std::begin(values),
            std::end(values),
            std::begin(paramValues_));
        valuesInitialized_ = initialized;
        NotifyRenderStateDirty();
    }

    bool MaterialFxComponent::SetFloat(
        const std::string& key,
        float value) {

        if (profileId_.empty()) {
            return false;
        }

        MaterialFxProfile profile{};
        if (!MaterialFxProfile::LoadById(profileId_, profile) ||
            !EnsureValuesReady(
                profileId_,
                paramValues_,
                valuesInitialized_)) {

            return false;
        }

        VFX::ParamChannelRef ref{};
        VFX::ParamType type{};
        if (!ResolveParamRef(profile, key, ref, &type) ||
            type != VFX::ParamType::Float ||
            ref.slot >= std::size(paramValues_) ||
            ref.channel >= 4u) {

            return false;
        }

        float* destination = &paramValues_[ref.slot].x;
        destination[ref.channel] = value;
        valuesInitialized_ = true;
        NotifyRenderStateDirty();
        return true;
    }

    bool MaterialFxComponent::SetFloat2(
        const std::string& key,
        const DirectX::XMFLOAT2& value) {

        if (profileId_.empty()) {
            return false;
        }

        MaterialFxProfile profile{};
        if (!MaterialFxProfile::LoadById(profileId_, profile) ||
            !EnsureValuesReady(
                profileId_,
                paramValues_,
                valuesInitialized_)) {

            return false;
        }

        VFX::ParamChannelRef ref{};
        VFX::ParamType type{};
        if (!ResolveParamRef(profile, key, ref, &type) ||
            type != VFX::ParamType::Float2 ||
            ref.slot >= std::size(paramValues_) ||
            ref.channel > 2u) {

            return false;
        }

        float* destination = &paramValues_[ref.slot].x;
        destination[ref.channel] = value.x;
        destination[ref.channel + 1u] = value.y;
        valuesInitialized_ = true;
        NotifyRenderStateDirty();
        return true;
    }

    bool MaterialFxComponent::SetFloat3(
        const std::string& key,
        const DirectX::XMFLOAT3& value) {

        if (profileId_.empty()) {
            return false;
        }

        MaterialFxProfile profile{};
        if (!MaterialFxProfile::LoadById(profileId_, profile) ||
            !EnsureValuesReady(
                profileId_,
                paramValues_,
                valuesInitialized_)) {

            return false;
        }

        VFX::ParamChannelRef ref{};
        VFX::ParamType type{};
        if (!ResolveParamRef(profile, key, ref, &type) ||
            type != VFX::ParamType::Float3 ||
            ref.slot >= std::size(paramValues_) ||
            ref.channel > 1u) {

            return false;
        }

        float* destination = &paramValues_[ref.slot].x;
        destination[ref.channel] = value.x;
        destination[ref.channel + 1u] = value.y;
        destination[ref.channel + 2u] = value.z;
        valuesInitialized_ = true;
        NotifyRenderStateDirty();
        return true;
    }

    bool MaterialFxComponent::SetFloat4(
        const std::string& key,
        const DirectX::XMFLOAT4& value) {

        if (profileId_.empty()) {
            return false;
        }

        MaterialFxProfile profile{};
        if (!MaterialFxProfile::LoadById(profileId_, profile) ||
            !EnsureValuesReady(
                profileId_,
                paramValues_,
                valuesInitialized_)) {

            return false;
        }

        VFX::ParamChannelRef ref{};
        VFX::ParamType type{};
        if (!ResolveParamRef(profile, key, ref, &type) ||
            type != VFX::ParamType::Float4 ||
            ref.slot >= std::size(paramValues_) ||
            ref.channel != 0u) {

            return false;
        }

        paramValues_[ref.slot] = value;
        valuesInitialized_ = true;
        NotifyRenderStateDirty();
        return true;
    }

    bool MaterialFxComponent::GetFloat(
        const std::string& key,
        float& out) const {

        if (profileId_.empty()) {
            return false;
        }

        MaterialFxProfile profile{};
        if (!MaterialFxProfile::LoadById(profileId_, profile)) {
            return false;
        }

        VFX::ParamChannelRef ref{};
        VFX::ParamType type{};
        if (!ResolveParamRef(profile, key, ref, &type) ||
            type != VFX::ParamType::Float ||
            ref.slot >= std::size(paramValues_) ||
            ref.channel >= 4u) {

            return false;
        }

        const DirectX::XMFLOAT4* source = paramValues_;
        DirectX::XMFLOAT4 defaults[VFX::kMaterialFxUserCount]{};
        if (!valuesInitialized_) {
            profile.CopyValuesTo(defaults);
            source = defaults;
        }

        out = (&source[ref.slot].x)[ref.channel];
        return true;
    }

    void MaterialFxComponent::ResetToProfileDefaults() {
        valuesInitialized_ = false;
        for (DirectX::XMFLOAT4& value : paramValues_) {
            value = {};
        }

        if (!profileId_.empty()) {
            MaterialFxProfile profile{};
            if (MaterialFxProfile::LoadById(profileId_, profile)) {
                profile.CopyValuesTo(paramValues_);
                valuesInitialized_ = true;
            }
        }
        NotifyRenderStateDirty();
    }

} // namespace HIKARI
