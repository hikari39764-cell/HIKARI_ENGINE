#pragma once
#include <cstddef>
#include <DirectXMath.h>
#include <cstdint>

namespace HIKARI {
    namespace POST {

        // HIKARI_PostEffectInterface.hlsli の b0 と共有する固定レイアウト。
        struct alignas(16) CommonParams
        {
            float time = 0.0f;
            float deltaTime = 0.0f;
            float combo = 0.0f;
            float intensity = 1.0f;

            float resolutionX = 1920.0f;
            float resolutionY = 1080.0f;
            float pad0 = 0.0f;
            float pad1 = 0.0f;

            // 各 PostEffect 固有の自由パラメータ領域。
            DirectX::XMFLOAT4 user[16]{};
        };

        static_assert(sizeof(CommonParams) == 288u);
        static_assert(alignof(CommonParams) == 16u);
        static_assert(offsetof(CommonParams, user) == 32u);

        static inline uint32_t Align256(uint32_t bytes)
        {
            return (bytes + 255u) & ~255u;
        }

    } // namespace POST
} // namespace HIKARI
