#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "Assets/Models/HIKARI_ModelAsset.h"

namespace HIKARI::ASSETS::COLLISION {

    struct ModelCollisionMeshData {
        std::vector<MATH::Vec3> vertices{};
        std::vector<uint32_t> indices{};
        std::vector<int32_t> sourceNodeIndices{};
        Bounds bounds{};

        bool IsUsable() const noexcept;
    };

    bool ExtractModelCollisionMesh(
        const ModelAsset& model,
        std::span<const int32_t> sourceNodeIndices,
        ModelCollisionMeshData& outMesh,
        std::string& outMessage);

    std::vector<int32_t> CollectRenderableModelNodeIndices(
        const ModelAsset& model);

} // namespace HIKARI::ASSETS::COLLISION
