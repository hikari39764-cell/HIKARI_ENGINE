#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <json.hpp>

#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Core/HIKARI_RenderView.h"

namespace HIKARI {
class DocumentSceneBase;
}

namespace HIKARI::RENDER3D::EDITORVIEW {
enum class EditorInteractiveShadingMode : uint8_t;
}

namespace HIKARI::EDITOR {

// 現在のシーンを定型の角度から順に撮影し、まとめて画像として返す。
// 表示中のビューポートやカメラは使わず、専用の離屏ビューで描く。
class SceneViewSeriesCapture {
public:
    ~SceneViewSeriesCapture();
    EditorOperationResult Start(DocumentSceneBase& scene, const nlohmann::json& parameters);
    EditorOperationResult Poll(DocumentSceneBase* scene);
    void Clear();

private:
    struct View {
        std::string label;
        Camera3D camera;
    };
    // 撮影中に人や智能体が場面を編集したか（止めずに結果で知らせる）。
    bool sceneEdited_ = false;
    // unlit は照明を使わず底色を写す（暗い場面で色を確かめる時）。値初期化は先頭の Lit。
    RENDER3D::EDITORVIEW::EditorInteractiveShadingMode shading_{};

    EditorOperationResult SubmitNext();
    EditorOperationResult Pending() const;
    bool ExposureSettled();

    RENDER3D::RenderViewId viewId_{};
    std::vector<View> views_;
    std::size_t next_ = 0;
    uint64_t ticket_ = 0;
    // 照明つきの撮影は、主ビューの自動露出が落ち着くまで最初の角度を出さない（時刻や天気を変えた直後）。
    bool waitingForExposure_ = false;
    std::chrono::steady_clock::time_point exposureWaitStart_{};
    float lastExposureLog2_ = 0.0f;
    uint64_t sceneRevision_ = 0;
    uint64_t worldIdentity_ = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    std::filesystem::path folder_;
    nlohmann::json images_ = nlohmann::json::array();
};

} // namespace HIKARI::EDITOR
