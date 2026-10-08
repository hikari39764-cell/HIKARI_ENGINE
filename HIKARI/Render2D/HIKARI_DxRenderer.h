#pragma once
#include <cstdint>
#include <d3d12.h>

#include "Gfx/HIKARI_GfxContext.h"

namespace HIKARI::DX {

// 2D コマンド（Spine など）用のテクスチャ付き四角形描画。直前に SetOutputTarget した形式へ描く。
// colorScale は頂点色に掛ける倍率で、HDR の場面層へ描く時は表示参照の色をビューの露出空間へ移す P/E。
class DxRenderer {
public:
    static bool Init(const GFX::Context& ctx);
    static void UpdateContext(const GFX::Context& ctx);
    static void SetScreenSize(int width, int height);
    static bool SetOutputTarget(DXGI_FORMAT format, int width, int height, float colorScale = 1.0f);
    static void Finalize();
    static void BeginFrame();

    // 頂点は画面ピクセル座標、順序は三角形ストリップ（lt, rt, lb, rb）。
    static void DrawMeshQuad(float x0, float y0, float u0, float v0, float x1, float y1, float u1, float v1, float x2,
                             float y2, float u2, float v2, float x3, float y3, float u3, float v3, int texHandle,
                             uint32_t color = 0xFFFFFFFF);

    DxRenderer() = delete;
};

} // namespace HIKARI::DX
