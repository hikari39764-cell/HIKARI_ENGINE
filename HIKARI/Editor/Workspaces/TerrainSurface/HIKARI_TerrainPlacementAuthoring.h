#pragma once

#include <cstddef>
#include <vector>

#include "Scene/HIKARI_SceneObjectId.h"

namespace HIKARI {

    class DocumentSceneBase;
    struct EditorSelection;

    namespace EDITOR {

        struct TerrainPlacementResult {
            std::vector<SceneObjectId> changedObjectIds{};
            std::size_t skippedLocked = 0u;
            std::size_t skippedOutsideTerrain = 0u;
            std::size_t skippedHierarchyChildren = 0u;
        };

        // Places selected hierarchy roots on the authored terrain height.
        // Only object transforms are changed; terrain data and render presets
        // remain untouched. surfaceOffset may be negative to bury a prop base
        // slightly and visually fuse it with the ground.
        TerrainPlacementResult PlaceSelectedObjectsOnTerrain(
            DocumentSceneBase& scene,
            const EditorSelection& selection,
            SceneObjectId terrainObjectId,
            float surfaceOffset);

    } // namespace EDITOR
} // namespace HIKARI
