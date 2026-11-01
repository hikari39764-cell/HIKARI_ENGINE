#pragma once

#include <cstdint>

namespace HIKARI::RENDER3D::MATERIAL {

inline constexpr uint32_t kInvalidMaterialProgramIndex = 0xffffffffu;
inline constexpr uint32_t kStandardPbrMaterialProgramIndex = 0u;

struct MaterialProgramId {
    uint32_t value = kInvalidMaterialProgramIndex;

    constexpr bool IsValid() const noexcept { return value != kInvalidMaterialProgramIndex; }

    constexpr explicit operator bool() const noexcept { return IsValid(); }

    friend constexpr bool operator==(MaterialProgramId lhs, MaterialProgramId rhs) noexcept = default;
};

inline constexpr MaterialProgramId kInvalidMaterialProgramId{};
inline constexpr MaterialProgramId kStandardPbrMaterialProgramId{kStandardPbrMaterialProgramIndex};

} // namespace HIKARI::RENDER3D::MATERIAL
