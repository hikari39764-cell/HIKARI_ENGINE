#include "Render3D/Views/HIKARI_EditorInteractiveViewGuides.h"

#if defined(HIKARI_WITH_EDITOR)

#include <algorithm>
#include <array>
#include <span>
#include <vector>

#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"

namespace HIKARI::RENDER3D::EDITORVIEW::INTERNAL {

void RenderGuideOverlay(const EditorInteractiveViewRequest& request, uint32_t width, uint32_t height) {
    const EditorInteractiveViewGuideOverlay& overlay = request.guideOverlay;
    if (!overlay.showGroundGrid && !overlay.showWorldAxes) {
        return;
    }

    std::vector<RENDERER3D::DEBUG::Stroke3D> depthStrokes{};
    if (overlay.showGroundGrid) {
        const int halfCount = std::clamp(overlay.gridHalfCount, 1, 100);
        const float spacing = std::clamp(overlay.gridSpacing, 0.01f, 100.0f);
        const float extent = static_cast<float>(halfCount) * spacing;
        const float thickness = std::clamp(overlay.gridThicknessPixels, 0.5f, 4.0f);
        depthStrokes.reserve(static_cast<size_t>((halfCount * 2 + 1) * 2));
        for (int index = -halfCount; index <= halfCount; ++index) {
            const float position = static_cast<float>(index) * spacing;
            const uint32_t color = index == 0 ? 0x718094B8u : 0x46536572u;
            depthStrokes.push_back({{-extent, 0.0f, position},
                                    {extent, 0.0f, position},
                                    color,
                                    RENDERER3D::DEBUG::DebugDepthMode::DepthTest,
                                    thickness});
            depthStrokes.push_back({{position, 0.0f, -extent},
                                    {position, 0.0f, extent},
                                    color,
                                    RENDERER3D::DEBUG::DebugDepthMode::DepthTest,
                                    thickness});
        }
    }
    if (overlay.showWorldAxes) {
        const float length = std::clamp(overlay.axisLength, 0.05f, 100.0f);
        const float thickness = std::clamp(overlay.axisThicknessPixels, 0.5f, 6.0f);
        const std::array<RENDERER3D::DEBUG::Stroke3D, 3u> axisStrokes{{
            {{}, {length, 0.0f, 0.0f}, 0xF25555FFu, RENDERER3D::DEBUG::DebugDepthMode::DepthTest, thickness},
            {{}, {0.0f, length, 0.0f}, 0x58D677FFu, RENDERER3D::DEBUG::DebugDepthMode::DepthTest, thickness},
            {{}, {0.0f, 0.0f, length}, 0x538CF5FFu, RENDERER3D::DEBUG::DebugDepthMode::DepthTest, thickness},
        }};
        depthStrokes.insert(depthStrokes.end(), axisStrokes.begin(), axisStrokes.end());
    }

    RENDERER3D::DEBUG::RenderEditorViewStrokes(request.cameraFrame.camera, static_cast<float>(width),
                                               static_cast<float>(height), depthStrokes);
}

} // namespace HIKARI::RENDER3D::EDITORVIEW::INTERNAL

#endif
