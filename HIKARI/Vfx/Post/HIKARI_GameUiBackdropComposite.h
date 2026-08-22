#pragma once

#include <array>
#include <cstdint>

#include <d3d12.h>
#include <wrl.h>

#include "Gfx/HIKARI_D3DBlobCompat.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "HIKARI/Project/UiTypes.h"

namespace HIKARI {
    class RenderTarget2D;
}

namespace HIKARI::POST {

    class QuadDrawer;

    class GameUiBackdropComposite final {
    public:
        ~GameUiBackdropComposite();

        void UpdateContext(const GFX::Context& context);
        void Shutdown();
        bool Execute(
            RenderTarget2D& source,
            RenderTarget2D& destination,
            QuadDrawer& quad,
            const GameProjectUiBackdropRequest& request);

    private:
        bool EnsureResources();

        GFX::Context context_{};
        Microsoft::WRL::ComPtr<ID3DBlob> pixelShader_{};
        Microsoft::WRL::ComPtr<ID3D12Resource> constants_{};
        uint8_t* mappedConstants_ = nullptr;
        uint32_t constantStride_ = 0u;
    };

} // namespace HIKARI::POST
