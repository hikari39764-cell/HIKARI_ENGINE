#pragma once

#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/ValueTypes.h"

namespace HIKARI {

    struct GameProjectSdkAccess;

    // Frame-local read-only terrain classification. It exposes no terrain
    // chunks, authoring grids, deformation state, or renderer resources.
    class TerrainView {
    public:
        TerrainView() = default;
        bool IsValid() const noexcept { return world_ != nullptr; }

        HIKARI_GAME_PROJECT_SDK_API bool IsSandSurfaceAt(
            GameProjectFloat3 worldPosition,
            float maximumVerticalDistance = 1.25f) const noexcept;

    private:
        friend struct GameProjectSdkAccess;
        explicit TerrainView(void* world) noexcept : world_(world) {}
        void* world_ = nullptr;
    };

} // namespace HIKARI
