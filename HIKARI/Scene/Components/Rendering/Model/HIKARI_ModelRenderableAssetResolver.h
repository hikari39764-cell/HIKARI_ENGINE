#pragma once

#include "Render3D/Procedural/HIKARI_ProceduralModelFactory.h"
#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/HIKARI_GameObject.h"

namespace HIKARI {

    inline const ModelAsset* ResolveRenderableModelAsset(
        const GameObject& object,
        ModelComponent& model) {

        if (const auto* procedural =
                object.GetComponent<ProceduralMeshComponent>()) {
            return PROCEDURAL::GetOrCreateModel(procedural->GetSettings());
        }
        return model.GetModelAsset();
    }

} // namespace HIKARI
