#pragma once

#include "Scene/HIKARI_SceneObjectId.h"

namespace HIKARI {

    struct SceneDocument;
    struct SceneObjectData;

    SceneObjectId AllocateSceneObjectId(
        const SceneDocument& document);
    SceneObjectData* FindSceneObjectData(
        SceneDocument& document,
        SceneObjectId id) noexcept;
    const SceneObjectData* FindSceneObjectData(
        const SceneDocument& document,
        SceneObjectId id) noexcept;

} // namespace HIKARI
