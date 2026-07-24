#pragma once
#include <DirectXMath.h>
#include <cstdint>

namespace HIKARI {
    namespace POST {

        // 未来长期不动的公共参数（b0）
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

            // 每个效果自己的自由参数区（你会让每个 effect 各自维护这份）
            DirectX::XMFLOAT4 user[16]{};
        };

        static inline uint32_t Align256(uint32_t bytes)
        {
            return (bytes + 255u) & ~255u;
        }

    } // namespace POST
} // namespace HIKARI
