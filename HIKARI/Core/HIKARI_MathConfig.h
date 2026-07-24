#pragma once

namespace HIKARI::MATH {

    // Engine-wide 3D math convention (frozen for consistency):
    // - Coordinate System: Right-Handed (RH)
    // - Axes: +X right, +Y up, +Z forward
    // - NDC depth range: [0, 1] (D3D style)
    // - Vector convention: column vector
    // - Transform order: clip = Proj * View * World * local
    constexpr bool kRightHanded = true;
    constexpr bool kNdcDepthZeroToOne = true;

} // namespace HIKARI::MATH
