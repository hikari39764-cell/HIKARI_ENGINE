#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "Assets/Models/HIKARI_ModelAsset.h"

namespace HIKARI::ASSETS::COLLISION {

    enum class ModelCollisionGroupingMode : uint8_t {
        AllCombined,
        SelectedCombined,
        SelectedSpatialGroups,
        SelectedIndividually,
    };

    struct ModelCollisionSourceGroup {
        std::vector<int32_t> nodeIndices{};
        float sourceBoundsVolume = 0.0f;
    };

    std::vector<ModelCollisionSourceGroup> BuildModelCollisionSourceGroups(
        const ModelAsset& model,
        ModelCollisionGroupingMode mode,
        std::span<const int32_t> selectedNodeIndices,
        float mergeDistance);

} // namespace HIKARI::ASSETS::COLLISION
