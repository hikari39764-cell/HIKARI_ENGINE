// --- START OF FILE HIKARI_DxRenderer.h ---

#pragma once
#include <cstddef>
#include <cstdint>
#include <wrl.h>
#include <d3d12.h>
#include "Gfx/HIKARI_GfxContext.h"
namespace HIKARI {
    namespace DX {

        enum class BlendMode {
            StraightAlpha,
            PremultipliedAlpha,
            Additive,
            Multiply,
            Screen,
            Opaque,
            Count
        };

        struct MeshVertex {
            float px, py;
            float u, v;
            float r, g, b, a;
        };

        class DxRenderer {
        public:
            static void Init(const GFX::Context& ctx);
            static void UpdateContext(const GFX::Context& ctx);
            static void SetScreenSize(int width, int height);
            static bool SetOutputTarget(
                DXGI_FORMAT format,
                int width,
                int height);
            static DXGI_FORMAT GetOutputFormat();
            static void Finalize();

            static void BeginFrame();
            static void EndFrame();

            static void SetBlendMode(BlendMode mode);

            static void DrawSprite(
                int texHandle,
                float x, float y,
                float w, float h,
                uint32_t color = 0xFFFFFFFF);

            static void DrawSpriteUV(
                int texHandle,
                float x, float y,
                float w, float h,
                float u0, float v0,
                float u1, float v1,
                uint32_t color = 0xFFFFFFFF);

            static void DrawMesh(
                const MeshVertex* verts,
                int vertexCount,
                D3D_PRIMITIVE_TOPOLOGY topology,
                int texHandle,
                uint32_t color = 0xFFFFFFFF
            );

            static void DrawMeshQuad(
                float x0, float y0, float u0, float v0,
                float x1, float y1, float u1, float v1,
                float x2, float y2, float u2, float v2,
                float x3, float y3, float u3, float v3,
                int texHandle,
                uint32_t color = 0xFFFFFFFF);

            // ==========================================
            // [新增] 遮罩相?接口
            // ==========================================

            // 更新遮罩参数 (玩家屏幕位置, 半径, 羽化)
            static void UpdateMaskParams(float screenX, float screenY, float radius, float softness);

            // 使用遮罩 Shader ?制 Quad
            static void DrawMeshQuadMasked(
                float x0, float y0, float u0, float v0,
                float x1, float y1, float u1, float v1,
                float x2, float y2, float u2, float v2,
                float x3, float y3, float u3, float v3,
                int texHandle,
                uint32_t color = 0xFFFFFFFF);
            // ==========================================

            static void DrawLine(
                float x0, float y0,
                float x1, float y1,
                uint32_t color = 0xFFFFFFFF);

        private:

            DxRenderer() = default;
            ~DxRenderer() = default;

            DxRenderer(const DxRenderer&) = delete;
            DxRenderer& operator=(const DxRenderer&) = delete;

            static Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSig_;
            static BlendMode currentBlendMode_;

            // ==========================================
            // [新增] 遮罩相?私有?源
            // ==========================================
            static Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSigMask_; // ?用的 RootSig (? b1 参数)
        };

    } // namespace DX
} // namespace HIKARI
