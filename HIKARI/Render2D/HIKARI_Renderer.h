#pragma once
#include <cstddef>
#include <string>
#include <vector>
#include "Core/Math/HIKARI_Vector2.h"
#include "Matrix3x3.h"
#include "Render2D/HIKARI_Transform2D.h"
#include "Render2D/HIKARI_Camera.h"
#include "Render2D/HIKARI_Texture.h"
namespace HIKARI {
    namespace RENDERER {

        enum class RenderLayer {
            Background = 0, // 地板
            Shadow = 1, // ?影
            Entity = 2, // 角色、物体
            VFX = 3, // 特效
            UI = 4, // 界面
            Debug = 5  // ?框等
        };


        // スプライトシート情報
        struct SpriteSheetInfo {
            int frameWidth = 0; // 1コマの幅（px）
            int frameHeight = 0; // 1コマの高さ（px）
            int columns = 1; // 横方向のコマ数
        };

        // スプライト用の UV 範囲（正規化 0.0～1.0）
        struct SpriteUV {
            float u0 = 0.0f, v0 = 0.0f; // 左上
            float u1 = 1.0f, v1 = 1.0f; // 右下
        };

        // フレームアニメ + MeshQuad 変形用
        struct SpriteFrameDeform {
            // この UV は「フレーム内」でのローカル UV（0.0～1.0）
            // 例: (0,0)-(1,1) = フレーム全体
            SpriteUV uv{};

            // メッシュの4頂点に対するローカル座標オフセット
            Vector2 offsetLT{ 0.0f, 0.0f };
            Vector2 offsetRT{ 0.0f, 0.0f };
            Vector2 offsetLB{ 0.0f, 0.0f };
            Vector2 offsetRB{ 0.0f, 0.0f };
        };

        // この描画呼び出しがカメラの影響を受けるかどうか
        enum class CameraMode { Inherit, Ignore };

        enum class FillMode { Fill, Wireframe };

        enum class BlendMode {
            StraightAlpha,
            PremultipliedAlpha,
            Additive,
            Multiply,
            Screen,
            Opaque
        };

        // グローバル既定のティントカラー
        void SetDefaultColor(unsigned int rgba);
        unsigned int GetDefaultColor();
        void ReserveRenderCommands(size_t count);
        void SetOcclusionSource(Vector2 screenPos, float radius);
        void SetBlendMode(BlendMode mode);

        // オプション：内部で使う 1x1 白テクスチャ（RGBA）のパスを設定
        // 既定値: "./NoviceResources/white1x1.png"
        void SetWhiteTexturePath(const char* pathRGBA1x1);

        // ---- 線分 & ボックス ----
        void DrawLine(
            Vector2 p0, Vector2 p1,
            CameraMode cam = CameraMode::Inherit,
            unsigned int rgba = 0xFFFFFFFF
        );

        void DrawQuad(
            const HIKARI::Transform2D& t,
            float width, float height,
            CameraMode cam = CameraMode::Inherit,
            unsigned int rgba = 0xFFFFFFFF,
            bool billboard = false // Added
        );

        void DrawBox(
            const HIKARI::Transform2D& t,
            float width, float height,
            FillMode mode = FillMode::Wireframe,
            CameraMode cam = CameraMode::Inherit,
            unsigned int rgba = 0xFFFFFFFF,
            bool billboard = false // Added
        );

        void DrawTriangle(
            const HIKARI::Transform2D& t,
            Vector2 p0, Vector2 p1, Vector2 p2,
            FillMode mode = FillMode::Wireframe,
            CameraMode cam = CameraMode::Inherit,
            unsigned int rgba = 0xFFFFFFFF,
            bool billboard = false // Added
        );

        void DrawEllipse(
            const HIKARI::Transform2D& t,
            float radiusX, float radiusY,
            FillMode mode = FillMode::Wireframe,
            CameraMode cam = CameraMode::Inherit,
            unsigned int rgba = 0xFFFFFFFF,
            bool billboard = false // Added
        );

        // ---- スプライト（名前指定）----
        void DrawSprite(
            const std::string& textureName,
            const HIKARI::Transform2D& t,
            float width, float height,
            CameraMode cam = CameraMode::Inherit,
            unsigned int rgba = 0xFFFFFFFF // RGBA
        );

        // UV 指定版
        void DrawSprite(
            const std::string& textureName,
            const HIKARI::Transform2D& t,
            float width, float height,
            const SpriteUV& uv,
            CameraMode cam = CameraMode::Inherit,
            unsigned int rgba = 0xFFFFFFFF
        );


        // スプライトの一部分を描画
        void DrawSpriteRect(
            const std::string& textureName,
            int srcX, int srcY, int srcW, int srcH,
            const HIKARI::Transform2D& t,
            float dstW, float dstH,
            CameraMode cam = CameraMode::Inherit,
            unsigned int rgba = 0xFFFFFFFF
        );


        enum class SpritePileDepthMode
        {
            Pure2D_YStep,   // 2.5を使わない：yStepPx で厚みを出す
            UseCamera25_Z   // 2.5を使う：各sliceにzを与えてCAMERA25で投影
        };

        struct SpritePileStrip
        {
            std::string textureName;

            int sliceW = 0;
            int sliceH = 0;

            int sliceCount = 0;


            bool reverseOrder = false;
        };

        struct SpritePileConfig
        {
            SpritePileDepthMode depthMode = SpritePileDepthMode::Pure2D_YStep;

            bool isBillboard = false; // true: 常にカメラ正面を向く
            float yStepPx = 1.0f;

            float stepXPerSlicePx = 0.0f;
            float stepYPerSlicePx = 0.0f;

            float dz = 0.1f;

            // 2.5モードでも「少しだけ」追加厚み（見た目調整）
            float extraYStepPx = 0.0f;

            // ============ pivot ============
            float pivotX01 = 0.5f;
            float pivotY01 = 0.5f;

            // ============ 描画サイズ ============
            float dstW = 0.0f; // 0以下なら sliceW
            float dstH = 0.0f; // 0以下なら sliceH

            // ============ 色 ============
            unsigned int rgba = 0xFFFFFFFF;
            int gapFillSteps = 0;
        };


        // ビルボード対応版 Sprite 描画
        void DrawSpriteBill(
            const std::string& textureName,
            const HIKARI::Transform2D& t,
            float width, float height,
            CameraMode cam = CameraMode::Inherit,
            unsigned int rgba = 0xFFFFFFFF,
            bool billboard = true, bool useMaskShader = false
        );

        // ビルボード対応版 SpriteRect 描画
        void DrawSpriteRectBill(
            const std::string& textureName,
            int srcX, int srcY, int srcW, int srcH,
            const HIKARI::Transform2D& t,
            float dstW, float dstH,
            CameraMode cam = CameraMode::Inherit,
            unsigned int rgba = 0xFFFFFFFF,
            bool billboard = true,
            bool useMaskShader = false
        );

        void DrawSpritePileStrip(
            const SpritePileStrip& strip,
            const SpritePileConfig& cfg,
            const HIKARI::Transform2D& baseT,
            float baseZ,
            CameraMode cam = CameraMode::Inherit
        );

        // ---- スプライト（ハンドル指定）----
        void DrawSpriteHandle(
            int textureHandle,
            const HIKARI::Transform2D& t,
            float width, float height,
            CameraMode cam = CameraMode::Inherit,
            unsigned int rgba = 0xFFFFFFFF
        );

        void DrawSpriteRectHandle(
            int textureHandle,
            int srcX, int srcY, int srcW, int srcH,
            const HIKARI::Transform2D& t,
            float dstW, float dstH,
            CameraMode cam = CameraMode::Inherit,
            unsigned int rgba = 0xFFFFFFFF
        );

        void DrawSpriteRectHandleVertices(
            int textureHandle,
            int srcX, int srcY, int srcW, int srcH,
            const Vector2& lt, const Vector2& rt,
            const Vector2& lb, const Vector2& rb,
            CameraMode cam = CameraMode::Inherit,
            unsigned int rgba = 0xFFFFFFFF
        );

        // ==== 簡易デフォーム用のグリッド ====
        struct DeformGrid {
            int cols = 0;    // グリッドの列数（横）
            int rows = 0;    // 行数（縦）

            // 各格点の座標（World Space）
            // 要素数 = cols * rows
            std::vector<Vector2> positions;

            // 各格点に対応する UV（0?1）
            // 要素数 = cols * rows
            std::vector<Vector2> uvs;
        };

        // DXテクスチャハンドルから描画
        void DrawDeformGridHandle(
            int dxHandle,
            const DeformGrid& grid,
            CameraMode cam = CameraMode::Inherit,
            unsigned int rgba = 0xFFFFFFFF
        );

        // テクスチャ名から描画（内部で GetDxHandle）
        void DrawDeformGrid(
            const std::string& textureName,
            const DeformGrid& grid,
            CameraMode cam = CameraMode::Inherit,
            unsigned int rgba = 0xFFFFFFFF
        );



        // ---- SPINE ----


        void DrawMeshQuadHandleUV_Vertices(
            int textureHandle,
            const Vector2& lt, const Vector2& rt,
            const Vector2& lb, const Vector2& rb,
            float u_lt, float v_lt,
            float u_rt, float v_rt,
            float u_lb, float v_lb,
            float u_rb, float v_rb,
            CameraMode cam = CameraMode::Inherit,
            unsigned int rgba = 0xFFFFFFFF);


        void DrawMeshQuadHandleUV_Local(
            int textureHandle,
            const HIKARI::Transform2D& t,
            const Vector2& ltLocal, const Vector2& rtLocal,
            const Vector2& lbLocal, const Vector2& rbLocal,
            float u_lt, float v_lt,
            float u_rt, float v_rt,
            float u_lb, float v_lb,
            float u_rb, float v_rb,
            CameraMode cam = CameraMode::Inherit,
            unsigned int rgba = 0xFFFFFFFF,
            bool billboard = false
        );

        // ---- ANIMATION ----
        void DrawSpriteFrame(
            const std::string& textureName,
            const SpriteSheetInfo& sheet,
            int frameIndex,
            const HIKARI::Transform2D& t,
            CameraMode cam = CameraMode::Inherit,
            unsigned int rgba = 0xFFFFFFFF);


        // フレーム + MeshQuad 変形版
        void DrawSpriteFrameEx(
            const std::string& textureName,
            const SpriteSheetInfo& sheet,
            int frameIndex,
            const HIKARI::Transform2D& t,
            const SpriteFrameDeform& deform = SpriteFrameDeform{},
            CameraMode cam = CameraMode::Inherit,
            unsigned int rgba = 0xFFFFFFFF);


        void SetCurrentLayer(RenderLayer layer);
        RenderLayer GetCurrentLayer();
        void BeginFrame();
        void RenderAll();
        void RenderLayerRange(RenderLayer minLayer, RenderLayer maxLayer, bool clearAfter = false);
        void ClearSubmittedCommands();

    } // namespace RENDERER
} // namespace HIKARI
