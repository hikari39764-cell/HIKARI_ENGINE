#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Lighting/HIKARI_LocalLightList.h"
#include "RenderCore/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {
struct GpuDrivenSceneSource;
}

namespace HIKARI::RENDER3D {
enum class LocalShadowQuality : uint8_t;
}

namespace HIKARI::RENDER3D::WORLD_SPRITES {
class WorldSpritePass;
}
namespace HIKARI::SHADOW {

struct LocalShadowBudget {
    uint32_t tileResolution = 512;
    uint32_t maximumFaces = 8;
};

LocalShadowBudget ResolveLocalShadowBudget(RENDER3D::LocalShadowQuality quality);

struct LocalLightShadowFrame {
    RENDER3D::TextureResourceHandle atlas{};
    RENDER3D::LIGHTING::LocalShadowGpuData data{};
    std::array<RENDER3D::LIGHTING::LocalLightShadowAssignment, RENDER3D::LIGHTING::kMaximumLocalShadowFaceCount>
        assignments{};
    uint32_t assignmentCount = 0;
};

struct LocalLightShadowStats {
    uint32_t requestedLights = 0;
    uint32_t selectedLights = 0;
    uint32_t budgetRejectedLights = 0;
    uint32_t renderedFaces = 0;
    uint32_t reusedFaces = 0;
    uint32_t failedLights = 0;
    uint64_t allocatedDepthBytes = 0;
    // 画質の面の上限と使った面（点光は 6 面、スポットは 1 面）。
    uint32_t budgetFaces = 0;
    uint32_t usedFaces = 0;
    // 影を描いた灯と、面の上限で外れた灯（SceneLightProvider::id）。編集器の灯の一覧が状態の表示に使う。
    std::vector<std::string> shadowedLightIds;
    std::vector<std::string> budgetRejectedLightIds;
};

void RenderLocalLightShadows(const GFX::Context& graphics, const RENDER3D::LIGHTING::SceneLightingSet& lighting,
                             const Camera3D& camera, const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& scene,
                             LocalShadowBudget budget, RENDER3D::WORLD_SPRITES::WorldSpritePass* sprites = nullptr);
const LocalLightShadowFrame* GetLocalLightShadowFrame(const GFX::Context& graphics,
                                                      const RENDER3D::LIGHTING::SceneLightingSet& lighting,
                                                      const Camera3D& camera);
const LocalLightShadowStats& GetLocalLightShadowStats();
void ResetLocalLightShadows();

} // namespace HIKARI::SHADOW
