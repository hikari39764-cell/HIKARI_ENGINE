#include "Render2D/HIKARI_Camera.h"
#include "Input/Runtime/HIKARI_InputActionIds.h"
#include "Input/Runtime/HIKARI_InputTypes.h"
#include <cmath>
#include <string>

namespace HIKARI {
    namespace CAMERA {

        // ====== 内部状態 ======
        static State     gState{};
        static Vector2   gScreenCenter{ 0.0f, 0.0f };
        static int       gScreenW = 1920, gScreenH = 1080;
        static Matrix3x3 gView = Matrix3x3::MakeIdentity();

        static bool      gDebugControlEnabled = true;
        static float     gDebugZoomStep = 0.02f;
        static float     gDebugZoomMin = 0.05f;
        static float     gDebugZoomMax = 12.0f;

        // ====== シェイク ======
        static float   gShakeTime = 0.0f;   // 経過時間
        static float   gShakeDur = 0.0f;    // 総継続時間
        // 振幅
        static float   gAmpX = 0.0f, gAmpY = 0.0f, gAmpRot = 0.0f;
        // 周波数(Hz)
        static float   gFreqX = 0.0f, gFreqY = 0.0f, gFreqRot = 0.0f;
        // 位相
        static float   gPhaseX = 0.0f, gPhaseY = 0.0f, gPhaseRot = 0.0f;
        // 減衰カーブ
        static EaseFn  gEnvelope = nullptr;

        // ====== 追従 / 制約 ======
        static const Vector2* gFollowTarget = nullptr; // ワールド座標のポインタ
        static Vector2 gDeadzoneHalf = { 160.0f, 90.0f }; // 既定のデッドゾーン半サイズ（320x180 px）
        static float   gFollowStiffness = 0.18f;
        static float   gMaxFollowSpeed = 0.0f; // 0 = 上限なし
        static bool    gHasBounds = false;
        static RectF   gWorldBounds{ 0,0,0,0 };

        // ====== ユーティリティ ======
        static inline float Clamp(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
        static inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
        static inline float Hash01(float x) { float s = std::sin(x * 12.9898f) * 43758.5453f; return s - std::floor(s); }

        static inline Vector2 TransformPoint(const Vector2& p, const Matrix3x3& m) {
            return { p.x * m.m[0][0] + p.y * m.m[1][0] + m.m[2][0],
                     p.x * m.m[0][1] + p.y * m.m[1][1] + m.m[2][1] };
        }
        static Matrix3x3 InverseAffine(const Matrix3x3& m) {
            float a = m.m[0][0], c = m.m[1][0], tx = m.m[2][0];
            float b = m.m[0][1], d = m.m[1][1], ty = m.m[2][1];
            float det = a * d - b * c;
            if (std::fabs(det) < 1e-6f) return Matrix3x3::MakeIdentity();
            float invDet = 1.0f / det;
            Matrix3x3 r{};
            r.m[0][0] = d * invDet; r.m[0][1] = -b * invDet; r.m[0][2] = 0.0f;
            r.m[1][0] = -c * invDet; r.m[1][1] = a * invDet; r.m[1][2] = 0.0f;
            r.m[2][0] = (c * ty - d * tx) * invDet;
            r.m[2][1] = (b * tx - a * ty) * invDet;
            r.m[2][2] = 1.0f;
            return r;
        }
        static inline Vector2 ViewportHalfWorld() {

            return {
                (gScreenW * 0.5f) / gState.scale.x,
                (gScreenH * 0.5f) / (gState.scale.y * gState.pitch)
            };
        }
        static void ApplyDebugControl(
            float dt,
            const INPUT::InputSnapshot* input) {
            (void)dt;
            if (!gDebugControlEnabled || input == nullptr) return;

            // --- 1. ??平移 (??修改：屏幕空? -> 世界空?) ---
            const bool dragging = input->IsDown(
                INPUT::ActionIds::EditorCameraPanHeld);

            // ?取原始鼠?移?量
            const std::array<float, 2> pan = input->GetAxis2D(
                INPUT::ActionIds::EditorCameraPan);
            float rawDragX = dragging ? pan[0] : 0.0f;
            float rawDragY = dragging ? pan[1] : 0.0f;

            if (std::fabs(rawDragX) > 0.0001f || std::fabs(rawDragY) > 0.0001f) {
                // ?算当前旋?角度的 sin/cos
                float c = std::cos(gState.rotation);
                float s = std::sin(gState.rotation);

                // 【核心修?】旋?向量
                // 当相机旋?后，屏幕的"上"不再是地?的"上"。
                // 我?需要把屏幕的移?量逆向旋?回世界坐?系。
                float worldMoveX = rawDragX * c - rawDragY * s;
                float worldMoveY = rawDragX * s + rawDragY * c;

                // 【核心修?】Pitch 深度??
                // 当?角?平?(Pitch小)，屏幕上垂直移? 1 像素代表在地面上移?了很?。
                // 除以 pitch 可以?一手感。
                float p = (gState.pitch < 0.1f) ? 0.1f : gState.pitch;
                worldMoveY /= p;

                // ?用移?
                gState.position.x -= worldMoveX / gState.scale.x;
                gState.position.y -= worldMoveY / gState.scale.y;
            }

            // --- 2. ?放 (保持不?) ---
            float zoomInput = input->GetAxis1D(
                INPUT::ActionIds::EditorCameraZoom);
            if (std::fabs(zoomInput) > 0.0001f) {
                float factor = 1.0f + zoomInput * gDebugZoomStep;
                if (factor < 0.1f) factor = 0.1f;
                gState.scale.x = Clamp(gState.scale.x * factor, gDebugZoomMin, gDebugZoomMax);
                gState.scale.y = Clamp(gState.scale.y * factor, gDebugZoomMin, gDebugZoomMax);
            }

            // --- 3. 旋??角 (保持不?) ---
            const bool looking = input->IsDown(
                INPUT::ActionIds::EditorCameraLookHeld);
            if (looking) {
                const std::array<float, 2> look = input->GetAxis2D(
                    INPUT::ActionIds::EditorCameraLook);
                float lookX = -look[0];
                float lookY = -look[1];

                if (std::fabs(lookX) > 0.0001f || std::fabs(lookY) > 0.0001f) {
                    const float rotSpeed = 0.003f;
                    const float pitchSpeed = 0.003f;

                    // 1. 旋? (Yaw)
                    gState.rotation += lookX * rotSpeed;

                    // 2. 俯仰 (Pitch)
                    gState.pitch += lookY * pitchSpeed;
                    gState.pitch = Clamp(gState.pitch, 0.2f, 1.0f);
                }
            }
        }
        // ====== 基本 Set/Get ======
        void SetScreenCenter(const Vector2& screenCenter) { gScreenCenter = screenCenter; }
        void SetPosition(const Vector2& pos) { gState.position = pos; }
        void SetScale(const Vector2& s) { gState.scale = s; }
        void SetRotation(float rad) { gState.rotation = rad; }
        void SetPivot(const Vector2& p) { gState.pivot = p; }
        void SetPitch(float p) { gState.pitch = Clamp(p, 0.1f, 1.0f); }
        float GetPitch() { return gState.pitch; }
        float GetRotation() { return gState.rotation; }

        float GetBillboardScaleY() {
            float p = (gState.pitch < 0.01f) ? 0.01f : gState.pitch;
            return 1.0f / p;
        }
        // ====== 拡張 ======
        void SetScreenSize(int w, int h) {
            if (w <= 0 || h <= 0) return;
            gScreenW = w; gScreenH = h;
            gScreenCenter = { w * 0.5f, h * 0.5f };
            gState.pivot = gScreenCenter;
        }

        // ====== シェイク ======
        void ShakeEx(const ShakeParams& p) {
            gShakeTime = 0.0f;
            gShakeDur = (p.durationSec < 0.0f ? 0.0f : p.durationSec);

            gAmpX = p.ampX;   gAmpY = p.ampY;   gAmpRot = p.ampRot;
            gFreqX = p.freqX;  gFreqY = p.freqY;  gFreqRot = p.freqRot;

            // 初期位相のランダム化（同一パターンの回避）
            float seed = gShakeTime + gShakeDur + gAmpX * 0.37f + gAmpY * 0.73f + gAmpRot * 1.11f;
            gPhaseX = Hash01(seed + 1.23f) * 6.2831853f;
            gPhaseY = Hash01(seed + 4.56f) * 6.2831853f;
            gPhaseRot = Hash01(seed + 7.89f) * 6.2831853f;

            gEnvelope = p.envelope; // nullptr を許可
        }
        void Shake(float amplitude, float durationSec) {
            ShakeParams p;
            p.ampX = amplitude;
            p.ampY = amplitude * 0.8f;
            p.ampRot = amplitude * 0.0025f;
            p.freqX = 12.0f; p.freqY = 9.0f; p.freqRot = 7.0f;
            p.durationSec = durationSec;
            p.envelope = nullptr;
            ShakeEx(p);
        }
        void StopShake() {
            gShakeTime = 0.0f; gShakeDur = 0.0f;
            gAmpX = gAmpY = gAmpRot = 0.0f;
            gFreqX = gFreqY = gFreqRot = 0.0f;
        }

        // ====== 追従 / 制約 ======
        void SetFollowTarget(const Vector2* targetWorldPosPtr) { gFollowTarget = targetWorldPosPtr; }
        void SetDeadzoneHalfSize(const Vector2& halfSizePx) { gDeadzoneHalf = halfSizePx; }
        void SetFollowStiffness(float s01) {
            if (s01 < 0.0f) s01 = 0.0f; if (s01 > 1.0f) s01 = 1.0f; gFollowStiffness = s01;
        }
        void SetMaxFollowSpeed(float pxPerSec) { gMaxFollowSpeed = (pxPerSec < 0.0f ? 0.0f : pxPerSec); }
        void SetBoundsWorld(const RectF& wr) { gWorldBounds = wr; gHasBounds = (wr.width > 0.0f && wr.height > 0.0f); }

        // ====== 更新 ======
        void Update(float dt, const INPUT::InputSnapshot* input) {
            if (dt < 0.0f) dt = 0.0f;

            // (0) シェイクの計算
            Vector2 shakeOffset{ 0.0f, 0.0f };
            float   shakeAngle = 0.0f;

            if (gShakeDur > 0.0f && (gAmpX > 0.0f || gAmpY > 0.0f || gAmpRot > 0.0f)) {
                gShakeTime += dt;
                float u = (gShakeDur > 0.0f) ? (gShakeTime / gShakeDur) : 1.0f;
                u = Clamp01(u);

                // 残り時間に応じたエンベロープ（減衰）を適用
                float env = (gEnvelope ? gEnvelope(1.0f - u) : (1.0f - u));

                // 位相更新
                const float TAU = 6.28318530718f;
                gPhaseX += TAU * gFreqX * dt;
                gPhaseY += TAU * gFreqY * dt;
                gPhaseRot += TAU * gFreqRot * dt;

                // 正弦波合成
                shakeOffset.x = gAmpX * env * std::sin(gPhaseX);
                shakeOffset.y = gAmpY * env * std::cos(gPhaseY);
                shakeAngle = gAmpRot * env * std::sin(gPhaseRot);

                if (u >= 1.0f) { StopShake(); }
            }

            // (1) 追従処理
            if (gFollowTarget) {
                Vector2 cam = gState.position;

                // 相対変位（ワールド）
                Vector2 delta{ gFollowTarget->x - cam.x, gFollowTarget->y - cam.y };

                // デッドゾーンをワールドスケールに換算（ズーム依存）
                Vector2 halfWorld{ gDeadzoneHalf.x / gState.scale.x, gDeadzoneHalf.y / gState.scale.y };

                Vector2 move{};
                if (std::fabs(delta.x) > halfWorld.x) move.x = delta.x - (delta.x > 0 ? halfWorld.x : -halfWorld.x);
                if (std::fabs(delta.y) > halfWorld.y) move.y = delta.y - (delta.y > 0 ? halfWorld.y : -halfWorld.y);

                // スムーズ追従
                cam.x += move.x * gFollowStiffness;
                cam.y += move.y * gFollowStiffness;

                // 最大速度制限
                if (gMaxFollowSpeed > 0.0f) {
                    Vector2 diff{ cam.x - gState.position.x, cam.y - gState.position.y };
                    float len2 = diff.x * diff.x + diff.y * diff.y;
                    float maxStep = gMaxFollowSpeed * dt;
                    if (len2 > maxStep * maxStep) {
                        float len = std::sqrt(len2);
                        cam.x = gState.position.x + diff.x * (maxStep / len);
                        cam.y = gState.position.y + diff.y * (maxStep / len);
                    }
                }

                // ビューポート半径に基づく境界制限
                if (gHasBounds) {
                    Vector2 vh = ViewportHalfWorld();
                    float minX = gWorldBounds.x + vh.x;
                    float maxX = gWorldBounds.x + gWorldBounds.width - vh.x;
                    float minY = gWorldBounds.y + vh.y;
                    float maxY = gWorldBounds.y + gWorldBounds.height - vh.y;
                    if (minX <= maxX) cam.x = Clamp(cam.x, minX, maxX);
                    if (minY <= maxY) cam.y = Clamp(cam.y, minY, maxY);
                }

                gState.position = cam;
            }

            // (1.5) デバッグ入力による手動パン / ズーム
            ApplyDebugControl(dt, input);

            // A. 基本移動
            Matrix3x3 mNegPos = Matrix3x3::MakeTranslate(-gState.position.x, -gState.position.y);
            Matrix3x3 mNegPivot = Matrix3x3::MakeTranslate(-gState.pivot.x, -gState.pivot.y);

            // B. 回転（World Rotation）
            //    先に回さないと、「カメラの向き」ではなく「紙の歪み」が回ってしまう
            Matrix3x3 mR = Matrix3x3::MakeRotate(gState.rotation + shakeAngle);

            // C. 俯仰（Pitch / Squashing）
            //    X軸はそのまま(1.0)、Y軸を pitch 倍して潰す
            Matrix3x3 mPitch = Matrix3x3::MakeScale(1.0f, gState.pitch);

            // D. ズーム（Camera Zoom）
            //    潰れた絵全体を拡大縮小する
            Matrix3x3 mS = Matrix3x3::MakeScale(gState.scale.x, gState.scale.y);

            // E. 画面中心へ戻す ＋ シェイク移動
            Matrix3x3 mPivot = Matrix3x3::MakeTranslate(gState.pivot.x, gState.pivot.y);
            Matrix3x3 mScreen = Matrix3x3::MakeTranslate(gScreenCenter.x, gScreenCenter.y);
            Matrix3x3 mShake = Matrix3x3::MakeTranslate(shakeOffset.x, shakeOffset.y);

            gView = mNegPos * mR * mPitch * mS * mScreen * mShake;
        }

        // ====== 取得 ======
        const Matrix3x3& GetViewMatrix() { return gView; }
        State            GetState() { return gState; }

        Vector2 GetPosition()
        {
            return gState.position;
        }

        int GetScreenWidth() {
            return gScreenW;
        }
        int GetScreenHeight() {
            return gScreenH;
        }

        void EnableDebugControl(bool enable) { gDebugControlEnabled = enable; }
        void SetDebugZoomStep(float step) { gDebugZoomStep = step; }
        void SetDebugZoomLimits(float minZoom, float maxZoom) {
            gDebugZoomMin = (minZoom < 0.0001f) ? 0.0001f : minZoom;
            gDebugZoomMax = (maxZoom < gDebugZoomMin) ? gDebugZoomMin : maxZoom;
        }
        // ====== 座標変換 ======
        Vector2 WorldToScreen(const Vector2& w) { return TransformPoint(w, gView); }
        Vector2 ScreenToWorld(const Vector2& s) { return TransformPoint(s, InverseAffine(gView)); }

    } // namespace CAMERA
} // namespace HIKARI
