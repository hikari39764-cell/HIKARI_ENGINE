#pragma once

#include "Editor/Gizmos/HIKARI_EditorTransformGizmo.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"

struct ImVec2;

namespace HIKARI {

    class DocumentSceneBase;

    namespace EDITOR {
        namespace DOCUMENT_SCENE {

            void DrawViewportDebugOptions(
                ViewportOverlayState& overlays,
                ViewportPerformanceState& performance);
            bool DrawRenderDebugViewCombo(
                const char* label,
                ViewportDebugViewState& debugView,
                float width);
            void DrawLightOverlayIcons(
                DocumentSceneBase& scene,
                const ViewportOverlayState& overlays,
                const ImVec2& viewportOrigin,
                const ImVec2& viewportSize);

        } // namespace DOCUMENT_SCENE
    } // namespace EDITOR

} // namespace HIKARI
