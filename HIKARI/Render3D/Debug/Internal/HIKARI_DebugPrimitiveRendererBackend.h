#pragma once

#include <span>

#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"

namespace HIKARI::RENDERER3D::DEBUG::INTERNAL {

    struct SolidVertex3D {
        MATH::Vec3 position{};
        MATH::Vec4 color{};
    };

    void RenderPrimitiveBatches(
        const Camera3D& camera,
        float screenWidth,
        float screenHeight,
        bool editorView,
        std::span<const Stroke3D> xrayStrokes,
        std::span<const Stroke3D> depthStrokes,
        std::span<const SolidVertex3D> solidVertices);

#if defined(HIKARI_WITH_EDITOR)
    void ShutdownEditorViewResources();
#endif

} // namespace HIKARI::RENDERER3D::DEBUG::INTERNAL
