#pragma once

#include "Assets/Models/Loading/Gltf/HIKARI_GltfAccessorReader.h"

namespace HIKARI {
    class ModelAsset;
}

namespace HIKARI::ASSETS::MODELS::GLTF {

    void ReadSceneHierarchyAndSkins(
        const Json& root,
        const Json& accessors,
        const Json& bufferViews,
        const BufferStorage& buffers,
        ModelAsset& asset);

} // namespace HIKARI::ASSETS::MODELS::GLTF
