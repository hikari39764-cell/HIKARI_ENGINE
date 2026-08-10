#pragma once

#if defined(HIKARI_WITH_EDITOR)

#include <cstdint>

namespace HIKARI::RENDER3D::EDITORVIEW {

    struct EditorInteractiveViewRequest;

    namespace INTERNAL {

        void RenderGuideOverlay(
            const EditorInteractiveViewRequest& request,
            uint32_t width,
            uint32_t height);

    } // namespace INTERNAL

} // namespace HIKARI::RENDER3D::EDITORVIEW

#endif
