#pragma once

#include <array>
#include <cstdint>

#include "Render3D/Vfx/HIKARI_ParticleRenderInput.h"

namespace HIKARI::RENDER3D::PARTICLES::INTERNAL {

    inline constexpr uint32_t kGpuParticleEmitterValueCount = 210u;
    inline constexpr uint32_t kGpuParticleEmitterUploadPageCount =
        kMaximumParticleEmitterSubmissionsPerFrame /
        kDefaultParticleEmitterUploadPageCapacity;

    static_assert(
        kMaximumParticleEmitterSubmissionsPerFrame %
            kDefaultParticleEmitterUploadPageCapacity == 0u);

    struct GpuParticleEmitterUploadAddress {
        uint32_t pageIndex = 0u;
        uint32_t elementIndex = 0u;
        bool valid = false;
    };

    inline GpuParticleEmitterUploadAddress
        ResolveGpuParticleEmitterUploadAddress(
            uint32_t linearIndex,
            uint32_t pageCapacity,
            uint32_t pageCount =
                kGpuParticleEmitterUploadPageCount) noexcept {

        if (pageCapacity == 0u || pageCount == 0u ||
            linearIndex / pageCapacity >= pageCount) {
            return {};
        }
        return {
            linearIndex / pageCapacity,
            linearIndex % pageCapacity,
            true
        };
    }

    struct alignas(16) GpuParticleFloat4 {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        float w = 0.0f;
    };

    // 形状（ジオメトリ）段だけが読む per-draw パラメータ。ルート定数の
    // コスト上限が厳しいため、伸びる余地のあるバッファ側に置く。
    struct alignas(16) GpuParticleShapeState {
        GpuParticleFloat4 renderShapeParamsA{};
        GpuParticleFloat4 renderUvParams{};
        GpuParticleFloat4 renderAnchor{};
        uint32_t renderUvMode = 0u;
        uint32_t shapeParam0 = 0u;
        uint32_t shapeParam1 = 0u;
        uint32_t shapeParam2 = 0u;
        // 長さ軸を持つ形状の変形。bend は無次元、twist はラジアン。
        float bendAmount = 0.0f;
        float twistRadians = 0.0f;
        // 3=トーラ主半径, 4=トーラ副半径, 5=カプセル半径。
        float shapeParam3 = 0.4f;
        float shapeParam4 = 0.12f;
        float shapeParam5 = 0.35f;
        // 軸固定ビルボードが固定するローカル軸（0=X, 1=Y, 2=Z）。
        float billboardAxis = 0.0f;
        float shapeReserved1 = 0.0f;
        float shapeReserved2 = 0.0f;
    };

    struct alignas(16) GpuParticleEmitterParameters {
        std::array<
            GpuParticleFloat4,
            kGpuParticleEmitterValueCount> values{};
    };

    inline GpuParticleFloat4 MakeGpuParticleFloat4(
        float x,
        float y,
        float z,
        float w) noexcept {

        return { x, y, z, w };
    }

    GpuParticleEmitterParameters BuildGpuParticleEmitterParameters(
        const ParticleEmitterSubmission& submission);

    static_assert(sizeof(GpuParticleFloat4) == 16u);
    static_assert(sizeof(GpuParticleEmitterParameters) == 3360u);

} // namespace HIKARI::RENDER3D::PARTICLES::INTERNAL
