#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <DirectXMath.h>
#include "Gfx/HIKARI_D3DBlobCompat.h"
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

            bool LoadPixelShader(const wchar_t* path);

            void ApplyCommonParams(const CommonParams& p); 

            void SetTime(float t);
            void SetUser(int index, const DirectX::XMFLOAT4& v);

            bool BindAndDraw(QuadDrawer& drawer);

        private:
            bool CreateConstantBuffer();

        private:
            static GFX::Context context_;
            Microsoft::WRL::ComPtr<ID3DBlob> psBlob_;

            Microsoft::WRL::ComPtr<ID3D12Resource> constantBuffer_;
            void* mappedPtr_ = nullptr;

            CommonParams params_{};
        };

    } // POST
} // HIKARI
