#pragma once

#include "Assets/Models/HIKARI_ModelAsset.h"

namespace HIKARI::ASSETS::MODELS {

    inline const TextureAsset3D* FindModelTexture(
        const ModelAsset& asset,
        const TextureSlot& slot) noexcept {

        if (slot.textureIndex < 0 ||
            slot.textureIndex >= static_cast<int>(asset.textures.size())) {
            return nullptr;
        }
        return &asset.textures[static_cast<size_t>(slot.textureIndex)];
    }

    inline TextureAsset3D* FindModelTexture(
        ModelAsset& asset,
        const TextureSlot& slot) noexcept {

        if (slot.textureIndex < 0 ||
            slot.textureIndex >= static_cast<int>(asset.textures.size())) {
            return nullptr;
        }
        return &asset.textures[static_cast<size_t>(slot.textureIndex)];
    }

} // namespace HIKARI::ASSETS::MODELS
