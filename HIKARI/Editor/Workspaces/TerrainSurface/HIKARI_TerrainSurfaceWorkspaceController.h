#pragma once

#include "Editor/HIKARI_EditorContext.h"

namespace HIKARI {

    class DocumentSceneBase;

    namespace EDITOR {

        class TerrainSurfaceWorkspaceController {
        public:
            void DrawDockSpace(bool resetDefaultDockLayout) const;
            void Draw(DocumentSceneBase& scene, EditorContext& context);
        };

    } // namespace EDITOR
} // namespace HIKARI
