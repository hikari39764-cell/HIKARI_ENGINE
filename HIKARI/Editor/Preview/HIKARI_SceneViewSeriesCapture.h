#pragma once

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

namespace HIKARI::EDITOR {

// 現在のシーンを定型の角度から順に撮影し、まとめて画像として返す。
// 表示中のビューポートやカメラは使わず、専用の離屏ビューで描く。
class SceneViewSeriesCapture {
public:
    ~SceneViewSeriesCapture();
    EditorOperationResult Start(DocumentSceneBase& scene, const nlohmann::json& parameters);
    EditorOperationResult Poll(DocumentSceneBase* scene);

private:
    struct View {
        std::string label;
        Camera3D camera;
    };

    EditorOperationResult SubmitNext();
    EditorOperationResult Pending() const;
    void Clear();

    RENDER3D::RenderViewId viewId_{};
    std::vector<View> views_;
    std::size_t next_ = 0;
    uint64_t ticket_ = 0;
    uint64_t sceneRevision_ = 0;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    std::filesystem::path folder_;
    nlohmann::json images_ = nlohmann::json::array();
};

} // namespace HIKARI::EDITOR
