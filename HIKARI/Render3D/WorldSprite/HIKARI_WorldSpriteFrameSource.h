#pragma once

#include <cstdint>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Models/HIKARI_ModelAssetTypes.h"
#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI {

    class AssetRegistry;

} // namespace HIKARI

namespace HIKARI::RENDER3D::WORLD_SPRITES {

    enum class WorldSpriteFacingMode : uint8_t {
        Fixed,
        CameraFacing,
        FixedYAxis,
    };

    enum class WorldSpriteAlphaMode : uint8_t {
        Auto,
        Opaque,
        Mask,
        Blend,
    };

    struct WorldSpriteInstance {
        uint64_t objectId = 0u;
        uint32_t stableOrder = 0u;
        MATH::Vec3 worldPosition{};
        MATH::Quat worldRotation{};
        MATH::Vec3 worldScale{ 1.f, 1.0f, 1.0f };
        MATH::Vec2 size{ 1.0f, 1.0f };
        MATH::Vec2 pivot{ 0.5f, 0.5f };
        MATH::Vec3 tint{ 1.0f, 1.0f, 1.0f };
        AssetGuid textureAssetGuid{};
        MATH::Vec4 uvRect{0, 0, 1, 1};
        float roughness = 0.85f, specular = 0.25f;
        bool lit = false, pointSampling = false;
        bool castShadow = true, receiveShadow = true;
        float opacity = 1.0f;
        float alphaCutoff = 0.5f;
        int32_t transparentSortPriority = 0;
        WorldSpriteFacingMode facing =
            WorldSpriteFacingMode::CameraFacing;
        WorldSpriteAlphaMode alphaMode = WorldSpriteAlphaMode::Auto;
        bool preserveAspect = true;
        Bounds worldBounds{};
    };

    struct WorldSpriteFrameSource {
        std::vector<WorldSpriteInstance> instances{};
        const AssetRegistry* assetRegistry = nullptr;
        uint64_t worldIdentity = 0u;
        uint64_t frameIndex = 0u;

        bool HasFrameWork() const noexcept {
            return !instances.empty();
        }

        void Clear() {
            instances.clear();
            assetRegistry = nullptr;
            worldIdentity = 0u;
            frameIndex = 0u;
        }
    };

} // namespace HIKARI::RENDER3D::WORLD_SPRITES
