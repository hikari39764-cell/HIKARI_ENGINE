#pragma once
#include <array>
#include <cstdint>
#include <string_view>
#include <wrl.h>
#include <d3d12.h>
#include <DirectXMath.h>
#include "Assets/Shader/HIKARI_ShaderAssetResolver.h"
#include "Vfx/Post/HIKARI_PostCommon.h"
#include "Gfx/HIKARI_GfxContext.h"

namespace HIKARI {
    namespace POST {

        class QuadDrawer;

        class PostEffect
        {
        public:
            PostEffect();
            ~PostEffect();
            static void UpdateContext(const GFX::Context& ctx);

            bool LoadBuiltinPixelShader(
                std::string_view programId,
                std::string_view entryId = "Main");

            void ApplyCommonParams(const CommonParams& p); 

            void SetTime(float t);
            void SetUser(int index, const DirectX::XMFLOAT4& v);

            bool BindAndDraw(QuadDrawer& drawer);

        private:
            bool CreateConstantBuffer();
            void ReleaseConstantBuffer() noexcept;

        private:
            static constexpr uint32_t kConstantDrawCapacityPerFrame = 32u;

            static GFX::Context context_;
            ASSETS::SHADER::ShaderAssetEntryLease psLease_{};

            Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
            GFX::Context constantBufferContext_{};
            uint8_t* mappedPtr_ = nullptr;
            uint32_t constantStride_ = 0u;
            std::array<uint64_t, GFX::kFrameResourceCount>
                constantFrameSerials_{};
            std::array<uint32_t, GFX::kFrameResourceCount>
                constantDrawCursors_{};

            CommonParams params_{};
        };

    } // POST
} // HIKARI
