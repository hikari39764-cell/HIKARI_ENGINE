#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>

#include "Render3D/GpuDriven/HIKARI_GpuSceneRegistry.h"
#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"
#include "Scene/HIKARI_SceneRenderCacheSync.h"
#include "Scene/HIKARI_ISystem.h"

namespace HIKARI {

    class AssetRegistry;

    enum class RenderSubmissionRouteMode : uint8_t {
        GpuDrivenMainline,
    };

    const char* ToString(RenderSubmissionRouteMode mode);

    struct RenderSubmissionDebugStats {
        int scannedModelCount = 0;
        int hiddenModelCount = 0;
        int submittedModelCount = 0;
        int culledModelCount = 0;
        int missingBoundsCount = 0;
        int skinnedCullSkippedCount = 0;
        int fallbackWireCount = 0;
        int gpuDrivenForwardBypassCount = 0;
        int gpuDrivenShadowBypassCount = 0;
        int runtimeSpecialModelCount = 0;
        int runtimeSpecialForwardModelCount = 0;
        int runtimeSpecialShadowModelCount = 0;
        RenderSubmissionRouteMode routeMode = RenderSubmissionRouteMode::GpuDrivenMainline;
        bool gpuDrivenMainRouteActive = false;
    };

    class RenderSubmissionSystem final : public ISystem {
    public:
        std::string_view GetName() const override { return "RenderSubmissionSystem"; }

        void PreRender(World& world, const FrameContext& frame) override;

        static void SetAssetContext(
            const AssetRegistry* assetRegistry,
            std::filesystem::path projectRoot);
        static void InvalidateSceneResources(bool clearProceduralCache);
        static RenderSubmissionRouteMode GetRouteMode();
        static const RenderSubmissionDebugStats& GetDebugStats();
        static const RENDER3D::RUNTIME::SceneRenderCache& GetSceneRenderCache();
        static const RENDER3D::RUNTIME::SceneRenderCache::Stats& GetSceneRenderCacheStats();
        static const RENDER3D::GPUDRIVEN::GpuSceneRegistryStats& GetGpuSceneRegistryStats();

    private:
        static RenderSubmissionDebugStats sDebugStats_;
        static RENDER3D::RUNTIME::SceneRenderCache sSceneRenderCache_;
        static RENDER3D::GPUDRIVEN::GpuSceneRegistry sGpuSceneRegistry_;
        static SceneRenderCacheSync sSceneRenderCacheSync_;
        static bool sGpuSceneRegistryValid_;
        static bool sGpuDrivenSceneSyncInitialized_;
        static const AssetRegistry* sAssetRegistry_;
        static std::filesystem::path sProjectRoot_;
    };

} // namespace HIKARI
