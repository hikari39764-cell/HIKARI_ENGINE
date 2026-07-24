#pragma once

#include "Scene/Document/Assets/HIKARI_DocumentSceneAssetBindings.h"
#include "Scene/Document/Baking/HIKARI_DocumentSceneBakeCoordinator.h"
#include "Scene/Document/Camera/HIKARI_DocumentSceneCameraRuntimeState.h"
#include "Scene/Document/Editor/HIKARI_DocumentSceneEditorState.h"
#include "Scene/Document/Lighting/HIKARI_DocumentSceneLightingRuntimeState.h"
#include "Scene/Document/Persistence/HIKARI_DocumentSceneIdentityState.h"
#include "Scene/Document/Runtime/HIKARI_DocumentSceneRuntimeState.h"

namespace HIKARI {

    struct DocumentSceneState {
        DocumentSceneIdentityState identity{};
        DocumentSceneCameraRuntimeState camera{};
        DocumentSceneRuntimeState runtime{};
        DocumentSceneAssetBindings assets{};
        DocumentSceneLightingRuntimeState lighting{};
        DocumentSceneEditorState editor{};
        DocumentSceneBakeCoordinator baking{};
    };

} // namespace HIKARI
