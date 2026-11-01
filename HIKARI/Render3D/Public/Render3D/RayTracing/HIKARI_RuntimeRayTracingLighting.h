#pragma once
#include <array>
#include <memory>
#include "Render3D/RayTracing/HIKARI_RuntimeRayTracingScene.h"
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"

namespace HIKARI {
struct SceneEnvironment;
}
namespace HIKARI::MESHRENDERER {
struct MeshMaterialGpuFrameView;
}

namespace HIKARI::RENDER3D::RAYTRACING {
// GI と反射で共有する入射照明。probe history / 出力 surface は consumer が所有する。
struct RuntimeRayLightingGpuData {
    MATH::Vec4 sunDirectionIntensity{0, 0, 0, 0}, sunColorRange{0, 0, 0, 0};
    MATH::Vec4 skyTintExposure{0, 0, 0, 0};
    // x 捕獲した空の立方体あり、y 空の回転、w 天空光の倍率。
    MATH::Vec4 skyParams{0, 0, 0, 0}, time{0, 0, 0, 0};
    std::array<uint32_t, 4> scene{}; // hit records, materials, local lights, empty scene
    LIGHTING::CloudSolarVisibilityGpuData clouds{};
};
static_assert(sizeof(RuntimeRayLightingGpuData) == 160);

enum class RuntimeRayLightingStatus : uint8_t { InvalidInput, MissingEnvironment, ResourceFailure, Ready };
struct RuntimeRayLightingFrame {
    RuntimeRayLightingStatus status = RuntimeRayLightingStatus::InvalidInput;
    RuntimeRayLightingGpuData data{};
    RuntimeRayTracingFrame scene{};
    D3D12_GPU_VIRTUAL_ADDRESS localLights = 0, lightCdf = 0;
    D3D12_GPU_DESCRIPTOR_HANDLE sky{};
    uint64_t contentHash = 0;
    uint32_t droppedLights = 0;
    bool Matches(const GFX::Context& graphics, const RuntimeRayTracingFrame& rayScene,
                 RuntimeRayTracingMaterialVersion materials) const noexcept;
};

// Frame pipeline の所有者。全 scene の光源 CDF / upload を一度準備して複数の ray pass が借用する。
class RuntimeRayTracingLighting final {
public:
    RuntimeRayTracingLighting();
    ~RuntimeRayTracingLighting();
    RuntimeRayLightingFrame Prepare(const GFX::Context& graphics, const RuntimeRayTracingFrame& scene,
                                    const MESHRENDERER::MeshMaterialGpuFrameView& materials,
                                    const SceneEnvironment& environment, const LIGHTING::SceneLightingSet& lighting,
                                    MATH::Vec4 time, const LIGHTING::CloudSolarVisibilityGpuData& clouds = {});
    void Reset(const GFX::Context& graphics);

private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace HIKARI::RENDER3D::RAYTRACING
