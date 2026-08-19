#pragma once

// カメラリグサービス ── フレーム単位で複数ソースからのカメラ姿勢提出と
// モディファイア提出を受け付け、優先度付きで最適な姿勢を解決し、
// Camera3D に適用するサービス。シーンカメラのランタイム駆動を一元管理する。

#include <cstdint>
#include <vector>

#include "Scene/Camera/HIKARI_CameraRigTypes.h"

namespace HIKARI {

    class Camera3D;

    namespace CAMERA {

        // カメラリグサービス
        // 毎フレーム、外部ソース（ゲームプレイ、シーケンサー、デバッグカメラ等）から
        // カメラ姿勢とモディファイアを収集し、優先度に基づいて解決する。
        class CameraRigService {
        public:
            // フレーム開始処理 ── 前フレームの提出データをクリアし、
            // 新しいフレームインデックスを設定する
            void BeginFrame(uint64_t frameIndex) noexcept;

            // 全提出データを強制クリア（シーン遷移時などに使用）
            void Clear() noexcept;

            // カメラ姿勢を提出する
            // 同じ cameraObjectId + sourceId の組み合わせが既に存在する場合は上書きされる
            // 戻り値: 提出が受け付けられた場合は true
            bool SubmitPose(const CameraRigSubmission& submission);

            // カメラモディファイアを提出する
            // 姿勢解決後にカメラへ適用するオフセットや FOV 調整を指定する
            // 戻り値: 提出が受け付けられた場合は true
            bool SubmitModifier(
                const CameraModifierSubmission& submission);

            // Submits a secondary subject to the built-in orbit-follow rig.
            // The winning request is resolved per camera and per frame.
            bool SubmitTargetFraming(
                const CameraTargetFramingSubmission& submission);

            // 指定カメラの最適な姿勢を解決する
            // 最も高い優先度の提出が選択され、outSubmission に格納される
            // 戻り値: 有効な姿勢が見つかった場合は true
            bool TryResolvePose(
                SceneObjectId cameraObjectId,
                CameraRigSubmission& outSubmission) const noexcept;

            bool TryResolveTargetFraming(
                SceneObjectId cameraObjectId,
                CameraTargetFramingSubmission& outSubmission) const noexcept;

            // フォールバックカメラを検索する
            // どのソースからも姿勢が提出されていない場合に、
            // シーン内で利用可能なカメラを返す
            bool TryResolveFallbackCamera(
                SceneObjectId& outCameraObjectId) const noexcept;

            // 解決済みの姿勢とモディファイアを Camera3D に適用する
            // duringOverride: オーバーライド中のモディファイアも適用するか
            void ApplyModifiers(
                SceneObjectId cameraObjectId,
                bool duringOverride,
                Camera3D& camera) const;

            // 指定カメラの現在のランタイム状態を取得（診断・デバッグ用）
            CameraRigRuntimeStatus GetStatus(
                SceneObjectId cameraObjectId) const noexcept;

            // 現在のフレームインデックスを返す
            uint64_t GetFrameIndex() const noexcept { return frameIndex_; }

        private:
            // 姿勢が有効な値を持っているか検証
            static bool IsValidPose(const CameraRigPose& pose) noexcept;

            // モディファイアが有効な値を持っているか検証
            static bool IsValidModifier(
                const CameraModifierSubmission& modifier) noexcept;

            static bool IsValidTargetFraming(
                const CameraTargetFramingSubmission& submission) noexcept;

            // 現在のフレームインデックス
            uint64_t frameIndex_ = 0u;

            // 今フレームに提出されたカメラ姿勢一覧
            std::vector<CameraRigSubmission> poses_{};

            // 今フレームに提出されたカメラモディファイア一覧
            std::vector<CameraModifierSubmission> modifiers_{};
            std::vector<CameraTargetFramingSubmission> targetFramings_{};
        };

    } // namespace CAMERA

} // namespace HIKARI
