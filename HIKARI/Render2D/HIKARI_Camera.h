#pragma once
#include "Core/Math/HIKARI_Vector2.h"
#include "Matrix3x3.h"

namespace HIKARI {
    namespace INPUT { class InputSnapshot; }
    namespace CAMERA {

        using EaseFn = float(*)(float); // t ∈ [0,1] → [0,1]

        // 2D カメラ：移動（パン）/ ズーム / 回転、ピボット（基準点）を持ち、
        // 高度なスクリーンシェイクに対応。
        // Renderer が使用する 3x3 のビュー行列を生成する。
        struct State {
            Vector2 position{ 0.0f, 0.0f }; // カメラが注視するワールド座標（パン）
            Vector2 scale{ 1.0f, 1.0f };    // ズーム（1,1 = 等倍）
            float   rotation{ 0.0f };       // ラジアン角
            Vector2 pivot{ 0.0f, 0.0f };    // 回転 / 拡大縮小のスクリーン上の基準点（通常は画面中心）
            float   pitch{ 1.0f };
        };

        // ----- 基本設定 -----
        void     SetScreenCenter(const Vector2& screenCenter);
        void     SetPosition(const Vector2& pos);
        void     SetScale(const Vector2& s);
        void     SetRotation(float rad);
        void     SetPivot(const Vector2& p);
        void     SetPitch(float p); // 范围通常 0.1 ~ 1.0
        float    GetPitch();
        float    GetRotation();
        float    GetBillboardScaleY();
        void     SetScreenSize(int width, int height);  // 画面サイズを設定し、自動的に ScreenCenter を更新
        inline   void SetZoom(float z) { SetScale({ z, z }); }
        inline   void LookAt(const Vector2& world) { SetPosition(world); }

        // ----- シェイク（Ease 使用可能）-----
        struct ShakeParams {
            float ampX = 8.0f;     // ピクセル揺れ(X)
            float ampY = 6.0f;     // ピクセル揺れ(Y)
            float ampRot = 0.02f;  // 回転揺れ（ラジアン / 約 1.1°）
            float freqX = 12.0f;   // 周波数 (Hz)
            float freqY = 9.0f;    // 周波数 (Hz)
            float freqRot = 7.0f;  // 周波数 (Hz)
            float durationSec = 0.35f; // シェイク継続時間（秒）
            EaseFn envelope = nullptr; // 減衰カーブ（NULL = 線形）
        };
        void ShakeEx(const ShakeParams& p);               // 拡張シェイク
        void Shake(float amplitude, float durationSec);   // シンプルシェイク（互換用）
        void StopShake();

        // ===== 追従 / 制御 =====
        void SetFollowTarget(const Vector2* targetWorldPosPtr);
        void SetDeadzoneHalfSize(const Vector2& halfSizePx);    // 画面空間のデッドゾーン半サイズ
        void SetFollowStiffness(float s01);                     // 追従の強さ（0〜1）
        void SetMaxFollowSpeed(float pxPerSec);                 // 最大追従速度（px/秒、0 = 無制限）
        struct RectF { float x, y, width, height; };
        void SetBoundsWorld(const RectF& worldRect);            // ワールド境界を設定（<=0 で無効）

        // ----- 更新 / 取得 -----
        void  Update(
            float deltaTime,
            const INPUT::InputSnapshot* input = nullptr);
        const Matrix3x3& GetViewMatrix();
        State GetState();
        Vector2 GetPosition();
        int GetScreenWidth();
        int GetScreenHeight();

        // ===== デバッグコントロール =====
        void EnableDebugControl(bool enable);
        void SetDebugZoomStep(float step);
        void SetDebugZoomLimits(float minZoom, float maxZoom);
        // ===== 座標変換 =====
        Vector2 WorldToScreen(const Vector2& w);
        Vector2 ScreenToWorld(const Vector2& s);

    } // namespace CAMERA
} // namespace HIKARI
