#pragma once

#include <json.hpp>

#include "Editor/HIKARI_EditorContext.h"
#include "Scene/HIKARI_SceneComponentId.h"
#include "Scene/HIKARI_SceneObjectId.h"

namespace HIKARI {

    class DocumentSceneBase;

    namespace EDITOR {

        struct TerrainSurfaceWorkspaceResult {
            SceneObjectId objectId{};
            SceneComponentId componentId{};
            nlohmann::json beforePatch = nlohmann::json::object();
            nlohmann::json afterPatch = nlohmann::json::object();
            bool componentChanged = false;
            bool editActive = false;
        };

        class TerrainSurfaceWorkspaceController {
        public:
            void DrawDockSpace(bool resetDefaultDockLayout) const;
            TerrainSurfaceWorkspaceResult Draw(
                DocumentSceneBase& scene,
                EditorContext& context);
        };

    } // namespace EDITOR
} // namespace HIKARI
