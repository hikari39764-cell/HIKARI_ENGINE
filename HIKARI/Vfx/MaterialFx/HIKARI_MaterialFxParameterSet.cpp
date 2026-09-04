#include "Vfx/MaterialFx/HIKARI_MaterialFxParameterSet.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <utility>

#include "Vfx/MaterialFx/HIKARI_MaterialFxProfile.h"

namespace HIKARI {
    namespace {
        bool IsFinite(float value) noexcept {
            return std::isfinite(value);
        }

        bool IsFinite(const DirectX::XMFLOAT2& value) noexcept {
            return IsFinite(value.x) && IsFinite(value.y);
        }

        bool IsFinite(const DirectX::XMFLOAT3& value) noexcept {
            return IsFinite(value.x) && IsFinite(value.y) &&
                IsFinite(value.z);
        }

        bool IsFinite(const DirectX::XMFLOAT4& value) noexcept {
            return IsFinite(value.x) && IsFinite(value.y) &&
                IsFinite(value.z) && IsFinite(value.w);
        }

        bool IsFloat3Type(VFX::ParamType type) noexcept {
            return type == VFX::ParamType::Float3 ||
                type == VFX::ParamType::Color3;
        }

        bool IsFloat4Type(VFX::ParamType type) noexcept {
            return type == VFX::ParamType::Float4 ||
                type == VFX::ParamType::Color ||
                type == VFX::ParamType::Color4;
        }

        float ReadChannel(
            const DirectX::XMFLOAT4& value,
            uint8_t channel) noexcept {
            switch (channel) {
            case 0u: return value.x;
            case 1u: return value.y;
            case 2u: return value.z;
            case 3u: return value.w;
            default: return 0.0f;
            }
        }

        void WriteChannel(
            DirectX::XMFLOAT4& destination,
            uint8_t channel,
            float value) noexcept {
            switch (channel) {
            case 0u: destination.x = value; break;
            case 1u: destination.y = value; break;
            case 2u: destination.z = value; break;
            case 3u: destination.w = value; break;
            default: break;
            }
        }

        bool EqualValue(
            const DirectX::XMFLOAT4& lhs,
            const DirectX::XMFLOAT4& rhs) noexcept {
            return lhs.x == rhs.x && lhs.y == rhs.y &&
                lhs.z == rhs.z && lhs.w == rhs.w;
        }

        MaterialFxParameterResult Resolve(
            std::string_view profileId,
            std::string_view key,
            MaterialFxProfile& outProfile,
            VFX::ParamChannelRef& outRef,
            VFX::ParamType& outType) {
            if (profileId.empty() ||
                !MaterialFxProfile::LoadById(
                    std::string(profileId),
                    outProfile)) {
                return MaterialFxParameterResult::ProfileUnavailable;
            }
            for (const VFX::ParamDesc& parameter : outProfile.params) {
                if (parameter.key == key) {
                    outRef = parameter.ref;
                    outType = parameter.type;
                    return MaterialFxParameterResult::Success;
                }
            }
            return MaterialFxParameterResult::ParameterNotFound;
        }

        void EnsureValuesReady(
            const MaterialFxProfile& profile,
            std::array<DirectX::XMFLOAT4,
                VFX::kMaterialFxUserCount>& values,
            bool& initialized) noexcept {
            if (!initialized) {
                DirectX::XMFLOAT4 defaults[VFX::kMaterialFxUserCount]{};
                profile.CopyValuesTo(defaults);
                std::copy(
                    std::begin(defaults),
                    std::end(defaults),
                    values.begin());
                initialized = true;
            }
        }

        const std::array<DirectX::XMFLOAT4,
            VFX::kMaterialFxUserCount>& ResolveReadValues(
            const MaterialFxProfile& profile,
            const std::array<DirectX::XMFLOAT4,
                VFX::kMaterialFxUserCount>& values,
            bool initialized,
            std::array<DirectX::XMFLOAT4,
                VFX::kMaterialFxUserCount>& defaults) noexcept {
            if (initialized) {
                return values;
            }
            DirectX::XMFLOAT4 rawDefaults[VFX::kMaterialFxUserCount]{};
            profile.CopyValuesTo(rawDefaults);
            std::copy(
                std::begin(rawDefaults),
                std::end(rawDefaults),
                defaults.begin());
            return defaults;
        }
    } // namespace

    bool MaterialFxParameterSet::SetProfileId(std::string profileId) {
        if (profileId.empty()) {
            ClearProfile();
            return true;
        }
        MaterialFxProfile profile{};
        if (!MaterialFxProfile::LoadById(profileId, profile)) {
            return false;
        }
        std::array<DirectX::XMFLOAT4, VFX::kMaterialFxUserCount>
            defaults{};
        DirectX::XMFLOAT4 rawDefaults[VFX::kMaterialFxUserCount]{};
        profile.CopyValuesTo(rawDefaults);
        std::copy(
            std::begin(rawDefaults),
            std::end(rawDefaults),
            defaults.begin());
        profileId_ = std::move(profileId);
        values_ = defaults;
        valuesInitialized_ = true;
        return true;
    }

    void MaterialFxParameterSet::ClearProfile() noexcept {
        profileId_.clear();
        values_.fill({});
        valuesInitialized_ = false;
    }

    bool MaterialFxParameterSet::ResetToProfileDefaults() {
        if (profileId_.empty()) {
            values_.fill({});
            valuesInitialized_ = false;
            return true;
        }
        MaterialFxProfile profile{};
        if (!MaterialFxProfile::LoadById(profileId_, profile)) {
            return false;
        }
        DirectX::XMFLOAT4 defaults[VFX::kMaterialFxUserCount]{};
        profile.CopyValuesTo(defaults);
        std::array<DirectX::XMFLOAT4, VFX::kMaterialFxUserCount>
            next{};
        std::copy(std::begin(defaults), std::end(defaults), next.begin());
        values_ = next;
        valuesInitialized_ = true;
        return true;
    }

    void MaterialFxParameterSet::SetRawValues(
        const DirectX::XMFLOAT4
            (&values)[VFX::kMaterialFxUserCount],
        bool initialized) noexcept {
        std::copy(std::begin(values), std::end(values), values_.begin());
        valuesInitialized_ = initialized;
    }

    void MaterialFxParameterSet::RestorePersistedState(
        std::string profileId,
        const DirectX::XMFLOAT4
            (&values)[VFX::kMaterialFxUserCount],
        bool initialized) {
        std::array<DirectX::XMFLOAT4, VFX::kMaterialFxUserCount>
            restored{};
        std::copy(std::begin(values), std::end(values), restored.begin());
        profileId_ = std::move(profileId);
        values_ = restored;
        valuesInitialized_ = initialized;
    }

    bool MaterialFxParameterSet::HasSameState(
        const MaterialFxParameterSet& rhs) const noexcept {
        if (profileId_ != rhs.profileId_ ||
            valuesInitialized_ != rhs.valuesInitialized_) {
            return false;
        }
        if (!valuesInitialized_) {
            return true;
        }
        for (size_t index = 0u; index < values_.size(); ++index) {
            if (!EqualValue(values_[index], rhs.values_[index])) {
                return false;
            }
        }
        return true;
    }

    MaterialFxParameterResult MaterialFxParameterSet::SetFloat(
        std::string_view key,
        float value) {
        if (!IsFinite(value)) {
            return MaterialFxParameterResult::InvalidValue;
        }
        MaterialFxProfile profile{};
        VFX::ParamChannelRef ref{};
        VFX::ParamType type{};
        const MaterialFxParameterResult resolved = Resolve(
            profileId_, key, profile, ref, type);
        if (resolved != MaterialFxParameterResult::Success) {
            return resolved;
        }
        if (type != VFX::ParamType::Float ||
            ref.slot >= values_.size() || ref.channel >= 4u) {
            return MaterialFxParameterResult::ParameterTypeMismatch;
        }
        EnsureValuesReady(profile, values_, valuesInitialized_);
        WriteChannel(values_[ref.slot], ref.channel, value);
        return MaterialFxParameterResult::Success;
    }

    MaterialFxParameterResult MaterialFxParameterSet::SetFloat2(
        std::string_view key,
        const DirectX::XMFLOAT2& value) {
        if (!IsFinite(value)) {
            return MaterialFxParameterResult::InvalidValue;
        }
        MaterialFxProfile profile{};
        VFX::ParamChannelRef ref{};
        VFX::ParamType type{};
        const MaterialFxParameterResult resolved = Resolve(
            profileId_, key, profile, ref, type);
        if (resolved != MaterialFxParameterResult::Success) {
            return resolved;
        }
        if (type != VFX::ParamType::Float2 ||
            ref.slot >= values_.size() || ref.channel > 2u) {
            return MaterialFxParameterResult::ParameterTypeMismatch;
        }
        EnsureValuesReady(profile, values_, valuesInitialized_);
        WriteChannel(values_[ref.slot], ref.channel, value.x);
        WriteChannel(
            values_[ref.slot],
            static_cast<uint8_t>(ref.channel + 1u),
            value.y);
        return MaterialFxParameterResult::Success;
    }

    MaterialFxParameterResult MaterialFxParameterSet::SetFloat3(
        std::string_view key,
        const DirectX::XMFLOAT3& value) {
        if (!IsFinite(value)) {
            return MaterialFxParameterResult::InvalidValue;
        }
        MaterialFxProfile profile{};
        VFX::ParamChannelRef ref{};
        VFX::ParamType type{};
        const MaterialFxParameterResult resolved = Resolve(
            profileId_, key, profile, ref, type);
        if (resolved != MaterialFxParameterResult::Success) {
            return resolved;
        }
        if (!IsFloat3Type(type) ||
            ref.slot >= values_.size() || ref.channel > 1u) {
            return MaterialFxParameterResult::ParameterTypeMismatch;
        }
        EnsureValuesReady(profile, values_, valuesInitialized_);
        WriteChannel(values_[ref.slot], ref.channel, value.x);
        WriteChannel(
            values_[ref.slot],
            static_cast<uint8_t>(ref.channel + 1u),
            value.y);
        WriteChannel(
            values_[ref.slot],
            static_cast<uint8_t>(ref.channel + 2u),
            value.z);
        return MaterialFxParameterResult::Success;
    }

    MaterialFxParameterResult MaterialFxParameterSet::SetFloat4(
        std::string_view key,
        const DirectX::XMFLOAT4& value) {
        if (!IsFinite(value)) {
            return MaterialFxParameterResult::InvalidValue;
        }
        MaterialFxProfile profile{};
        VFX::ParamChannelRef ref{};
        VFX::ParamType type{};
        const MaterialFxParameterResult resolved = Resolve(
            profileId_, key, profile, ref, type);
        if (resolved != MaterialFxParameterResult::Success) {
            return resolved;
        }
        if (!IsFloat4Type(type) ||
            ref.slot >= values_.size() || ref.channel != 0u) {
            return MaterialFxParameterResult::ParameterTypeMismatch;
        }
        EnsureValuesReady(profile, values_, valuesInitialized_);
        values_[ref.slot] = value;
        return MaterialFxParameterResult::Success;
    }

    MaterialFxParameterResult MaterialFxParameterSet::SetToggle(
        std::string_view key,
        bool value) {
        MaterialFxProfile profile{};
        VFX::ParamChannelRef ref{};
        VFX::ParamType type{};
        const MaterialFxParameterResult resolved = Resolve(
            profileId_, key, profile, ref, type);
        if (resolved != MaterialFxParameterResult::Success) {
            return resolved;
        }
        if (type != VFX::ParamType::Toggle ||
            ref.slot >= values_.size() || ref.channel >= 4u) {
            return MaterialFxParameterResult::ParameterTypeMismatch;
        }
        EnsureValuesReady(profile, values_, valuesInitialized_);
        WriteChannel(
            values_[ref.slot],
            ref.channel,
            value ? 1.0f : 0.0f);
        return MaterialFxParameterResult::Success;
    }

    MaterialFxParameterResult MaterialFxParameterSet::TryGetFloat(
        std::string_view key,
        float& outValue) const {
        MaterialFxProfile profile{};
        VFX::ParamChannelRef ref{};
        VFX::ParamType type{};
        const MaterialFxParameterResult resolved = Resolve(
            profileId_, key, profile, ref, type);
        if (resolved != MaterialFxParameterResult::Success) {
            return resolved;
        }
        if (type != VFX::ParamType::Float ||
            ref.slot >= values_.size() || ref.channel >= 4u) {
            return MaterialFxParameterResult::ParameterTypeMismatch;
        }
        std::array<DirectX::XMFLOAT4, VFX::kMaterialFxUserCount>
            defaults{};
        const auto& values = ResolveReadValues(
            profile, values_, valuesInitialized_, defaults);
        outValue = ReadChannel(values[ref.slot], ref.channel);
        return MaterialFxParameterResult::Success;
    }

    MaterialFxParameterResult MaterialFxParameterSet::TryGetFloat2(
        std::string_view key,
        DirectX::XMFLOAT2& outValue) const {
        MaterialFxProfile profile{};
        VFX::ParamChannelRef ref{};
        VFX::ParamType type{};
        const MaterialFxParameterResult resolved = Resolve(
            profileId_, key, profile, ref, type);
        if (resolved != MaterialFxParameterResult::Success) {
            return resolved;
        }
        if (type != VFX::ParamType::Float2 ||
            ref.slot >= values_.size() || ref.channel > 2u) {
            return MaterialFxParameterResult::ParameterTypeMismatch;
        }
        std::array<DirectX::XMFLOAT4, VFX::kMaterialFxUserCount>
            defaults{};
        const auto& values = ResolveReadValues(
            profile, values_, valuesInitialized_, defaults);
        outValue = {
            ReadChannel(values[ref.slot], ref.channel),
            ReadChannel(
                values[ref.slot],
                static_cast<uint8_t>(ref.channel + 1u))
        };
        return MaterialFxParameterResult::Success;
    }

    MaterialFxParameterResult MaterialFxParameterSet::TryGetFloat3(
        std::string_view key,
        DirectX::XMFLOAT3& outValue) const {
        MaterialFxProfile profile{};
        VFX::ParamChannelRef ref{};
        VFX::ParamType type{};
        const MaterialFxParameterResult resolved = Resolve(
            profileId_, key, profile, ref, type);
        if (resolved != MaterialFxParameterResult::Success) {
            return resolved;
        }
        if (!IsFloat3Type(type) ||
            ref.slot >= values_.size() || ref.channel > 1u) {
            return MaterialFxParameterResult::ParameterTypeMismatch;
        }
        std::array<DirectX::XMFLOAT4, VFX::kMaterialFxUserCount>
            defaults{};
        const auto& values = ResolveReadValues(
            profile, values_, valuesInitialized_, defaults);
        outValue = {
            ReadChannel(values[ref.slot], ref.channel),
            ReadChannel(
                values[ref.slot],
                static_cast<uint8_t>(ref.channel + 1u)),
            ReadChannel(
                values[ref.slot],
                static_cast<uint8_t>(ref.channel + 2u))
        };
        return MaterialFxParameterResult::Success;
    }

    MaterialFxParameterResult MaterialFxParameterSet::TryGetFloat4(
        std::string_view key,
        DirectX::XMFLOAT4& outValue) const {
        MaterialFxProfile profile{};
        VFX::ParamChannelRef ref{};
        VFX::ParamType type{};
        const MaterialFxParameterResult resolved = Resolve(
            profileId_, key, profile, ref, type);
        if (resolved != MaterialFxParameterResult::Success) {
            return resolved;
        }
        if (!IsFloat4Type(type) ||
            ref.slot >= values_.size() || ref.channel != 0u) {
            return MaterialFxParameterResult::ParameterTypeMismatch;
        }
        std::array<DirectX::XMFLOAT4, VFX::kMaterialFxUserCount>
            defaults{};
        const auto& values = ResolveReadValues(
            profile, values_, valuesInitialized_, defaults);
        outValue = values[ref.slot];
        return MaterialFxParameterResult::Success;
    }

    MaterialFxParameterResult MaterialFxParameterSet::TryGetToggle(
        std::string_view key,
        bool& outValue) const {
        MaterialFxProfile profile{};
        VFX::ParamChannelRef ref{};
        VFX::ParamType type{};
        const MaterialFxParameterResult resolved = Resolve(
            profileId_, key, profile, ref, type);
        if (resolved != MaterialFxParameterResult::Success) {
            return resolved;
        }
        if (type != VFX::ParamType::Toggle ||
            ref.slot >= values_.size() || ref.channel >= 4u) {
            return MaterialFxParameterResult::ParameterTypeMismatch;
        }
        std::array<DirectX::XMFLOAT4, VFX::kMaterialFxUserCount>
            defaults{};
        const auto& values = ResolveReadValues(
            profile, values_, valuesInitialized_, defaults);
        outValue = ReadChannel(values[ref.slot], ref.channel) >= 0.5f;
        return MaterialFxParameterResult::Success;
    }

} // namespace HIKARI
