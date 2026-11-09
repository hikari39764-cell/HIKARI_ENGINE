#pragma once

#include <algorithm>
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

// 勾配噪声（おおよそ -1..1）。値噪声より格子の癖が出ないので、地形の形と色に使う。
inline float GradientNoise(double x, double z, uint32_t seed) noexcept {
    static constexpr float kDirection[16] = {
        1.0f,  0.92387953f,  0.70710678f,  0.38268343f,  0.0f, -0.38268343f, -0.70710678f, -0.92387953f,
        -1.0f, -0.92387953f, -0.70710678f, -0.38268343f, 0.0f, 0.38268343f,  0.70710678f,  0.92387953f};
    const double fx = std::floor(x);
    const double fz = std::floor(z);
    const auto ix = static_cast<int64_t>(fx);
    const auto iz = static_cast<int64_t>(fz);
    const auto tx = static_cast<float>(x - fx);
    const auto tz = static_cast<float>(z - fz);
    const auto corner = [&](int dx, int dz) {
        const uint32_t h = HashLattice(ix + dx, iz + dz, seed) & 15u;
        return kDirection[h] * (tx - static_cast<float>(dx)) +
               kDirection[(h + 4u) & 15u] * (tz - static_cast<float>(dz));
    };
    const float u = tx * tx * tx * (tx * (tx * 6.0f - 15.0f) + 10.0f);
    const float v = tz * tz * tz * (tz * (tz * 6.0f - 15.0f) + 10.0f);
    const float top = corner(0, 0) + (corner(1, 0) - corner(0, 0)) * u;
    const float bottom = corner(0, 1) + (corner(1, 1) - corner(0, 1)) * u;
    return 1.5f * (top + (bottom - top) * v);
}

inline float FractalNoise(double x, double z, int octaves, uint32_t seed, float gain = 0.5f) noexcept {
    float sum = 0.0f;
    float amplitude = 1.0f;
    float total = 0.0f;
    for (int octave = 0; octave < octaves; ++octave) {
        sum += amplitude * GradientNoise(x, z, seed + static_cast<uint32_t>(octave));
        total += amplitude;
        amplitude *= gain;
        x = x * 2.03 + 13.1;
        z = z * 2.03 - 7.7;
    }
    return sum / total;
}

// 尖った稜の噪声（0..1）。前の段が高い所ほど次の段が効く。
inline float RidgedNoise(double x, double z, int octaves, uint32_t seed, float gain = 0.5f) noexcept {
    float sum = 0.0f;
    float amplitude = 1.0f;
    float total = 0.0f;
    float weight = 1.0f;
    for (int octave = 0; octave < octaves; ++octave) {
        float value = 1.0f - std::abs(GradientNoise(x, z, seed + static_cast<uint32_t>(octave)));
        value *= value;
        sum += amplitude * value * weight;
        total += amplitude;
        weight = std::clamp(value * 1.6f, 0.0f, 1.0f);
        amplitude *= gain;
        x = x * 2.03 + 13.1;
        z = z * 2.03 - 7.7;
    }
    return sum / total;
}

} // namespace HIKARI::TERRAIN::DATA
