#pragma once

#include <cmath>
#include <cstdint>

// 筆刷・印章・材質規則で共有する決定的な値噪声（-1..1）。同じ入力は人と智能体で同じ結果になる。
namespace HIKARI::TERRAIN::DATA {

inline uint32_t HashLattice(int64_t x, int64_t z, uint32_t seed) noexcept {
    uint32_t h = seed * 0x9E3779B9u;
    h ^= static_cast<uint32_t>(x) * 0x85EBCA6Bu;
    h = (h ^ (h >> 13u)) * 0xC2B2AE35u;
    h ^= static_cast<uint32_t>(z) * 0x27D4EB2Fu;
    h = (h ^ (h >> 15u)) * 0x165667B1u;
    return h ^ (h >> 16u);
}

inline float LatticeValueNoise(double x, double z, uint32_t seed) noexcept {
    const double fx = std::floor(x);
    const double fz = std::floor(z);
    const auto ix = static_cast<int64_t>(fx);
    const auto iz = static_cast<int64_t>(fz);
    const auto lattice = [&](int64_t dx, int64_t dz) {
        return static_cast<float>(HashLattice(ix + dx, iz + dz, seed) & 0xFFFFu) / 32767.5f - 1.0f;
    };
    const auto fade = [](double t) { return static_cast<float>(t * t * (3.0 - 2.0 * t)); };
    const float u = fade(x - fx);
    const float v = fade(z - fz);
    const float top = lattice(0, 0) + (lattice(1, 0) - lattice(0, 0)) * u;
    const float bottom = lattice(0, 1) + (lattice(1, 1) - lattice(0, 1)) * u;
    return top + (bottom - top) * v;
}

} // namespace HIKARI::TERRAIN::DATA
