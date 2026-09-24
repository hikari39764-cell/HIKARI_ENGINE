#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include <DirectXMath.h>

#include "Vfx/Common/HIKARI_FxTypes.h"

namespace HIKARI {

class MaterialFxComponent;

enum class MaterialFxParameterResult : uint8_t {
    Success = 0u,
    ProfileUnavailable,
    ParameterNotFound,
    ParameterTypeMismatch,
    InvalidValue,
};

// Renderer-independent value owner shared by authored components and
// render-only presentation records. It owns only profile identity and the
// resolved user slots; GPU routing remains renderer-owned.
class MaterialFxParameterSet final {
public:
    bool SetProfileId(std::string profileId);
    void ClearProfile() noexcept;
    const std::string& GetProfileId() const noexcept { return profileId_; }

    bool ResetToProfileDefaults();
    bool AreValuesInitialized() const noexcept { return valuesInitialized_; }
    const std::array<DirectX::XMFLOAT4, VFX::kMaterialFxUserCount>& GetValues() const noexcept { return values_; }
    void SetRawValues(const DirectX::XMFLOAT4 (&values)[VFX::kMaterialFxUserCount], bool initialized) noexcept;
    // Restores serialized identity and slots without resolving a profile.
    // Profile I/O remains deferred until a typed read/write or explicit
    // reset requires the descriptor.
    void RestorePersistedState(std::string profileId, const DirectX::XMFLOAT4 (&values)[VFX::kMaterialFxUserCount],
                               bool initialized);
    bool HasSameState(const MaterialFxParameterSet& rhs) const noexcept;

    MaterialFxParameterResult SetFloat(std::string_view key, float value);
    MaterialFxParameterResult SetFloat2(std::string_view key, const DirectX::XMFLOAT2& value);
    MaterialFxParameterResult SetFloat3(std::string_view key, const DirectX::XMFLOAT3& value);
    MaterialFxParameterResult SetFloat4(std::string_view key, const DirectX::XMFLOAT4& value);
    MaterialFxParameterResult SetToggle(std::string_view key, bool value);

    MaterialFxParameterResult TryGetFloat(std::string_view key, float& outValue) const;
    MaterialFxParameterResult TryGetFloat2(std::string_view key, DirectX::XMFLOAT2& outValue) const;
    MaterialFxParameterResult TryGetFloat3(std::string_view key, DirectX::XMFLOAT3& outValue) const;
    MaterialFxParameterResult TryGetFloat4(std::string_view key, DirectX::XMFLOAT4& outValue) const;
    MaterialFxParameterResult TryGetToggle(std::string_view key, bool& outValue) const;

private:
    friend class MaterialFxComponent;

    std::string profileId_{};
    std::array<DirectX::XMFLOAT4, VFX::kMaterialFxUserCount> values_{};
    bool valuesInitialized_ = false;
};

} // namespace HIKARI
