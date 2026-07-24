#pragma once

namespace HIKARI {
    class ModelAsset;
}

namespace HIKARI::RENDER3D::MODELS {

    class ModelTextureResolver;

    bool BuildModelRuntimeResources(
        ModelAsset& asset,
        const ModelTextureResolver& textureResolver);

    void ReleaseModelRuntimeResources(ModelAsset& asset);

} // namespace HIKARI::RENDER3D::MODELS
