#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "Scene/HIKARI_ISystem.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"
#include "Scene/Sand/HIKARI_SandSurfaceTypes.h"
#include "Scene/Water/HIKARI_WaterSurfaceQueryService.h"

namespace HIKARI {

    class World;

    namespace SAND {
        class SandSurfaceQueryService;
        struct SandSurfaceRuntimeState;
        using SandSurfaceRuntimeStatePtr =
            std::shared_ptr<SandSurfaceRuntimeState>;
    }

    // 可変 World component を CPU 高さグリッドへ投影し、球体の接触を
    // stamp として適用する。レンダリングはこのグリッドの snapshot だけを
    // 読むため、システムが唯一の変異経路になる。
    class SandSurfaceSystem final :
        public ISystem,
        public WATER::IWaterBathymetryProvider {
    public:
        std::string_view GetName() const override {
            return "SandSurfaceSystem";
        }

        void OnWorldAttached(World& world) override;
        void OnWorldDetached(World& world) override;
        void PreFixedUpdate(
            World& world,
            const FrameContext& frame) override;
        void PreRender(
            World& world,
            const FrameContext& frame) override;
        bool RaycastBathymetry(
            const WATER::WaterBathymetryRay& ray,
            float& outDistance) const noexcept override;

    private:
        void Refresh(World& world);
        void ApplySphereContactStamps(
            World& world,
            float fixedDt);
        void RelaxState(
            SAND::SandSurfaceRuntimeState& state,
            float fixedDt);
        void SyncAuthoredTerrain(
            SAND::SandSurfaceRuntimeState& state,
            const SAND::SandSurfaceTerrain& terrain);
        void RebuildCombinedHeights(
            SAND::SandSurfaceRuntimeState& state);
        void ApplySphereImprint(
            SAND::SandSurfaceRuntimeState& state,
            float localX,
            float localZ,
            float worldRadius,
            float sinkDepth);

        SAND::SandSurfaceQueryService* queryService_ = nullptr;
        WATER::WaterSurfaceQueryService* waterQueryService_ = nullptr;
        std::unordered_map<uint64_t, SAND::SandSurfaceRuntimeStatePtr>
            states_{};
        std::unordered_map<uint64_t, MATH::Vec3> previousSphereCenters_{};
    };

} // namespace HIKARI
