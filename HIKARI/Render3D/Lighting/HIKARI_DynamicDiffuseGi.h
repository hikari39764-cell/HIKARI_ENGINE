#pragma once
#include <memory>
#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Lighting/HIKARI_LightProbeVolumeRuntime.h"
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"
#include "Render3D/RayTracing/HIKARI_RuntimeRayTracingScene.h"
#include "Render3D/RayTracing/HIKARI_RuntimeRayTracingLighting.h"

namespace HIKARI {
struct SceneEnvironment;
}
namespace HIKARI::MESHRENDERER {
struct MeshMaterialGpuFrameView;
}
namespace HIKARI::RENDER3D {
enum class DynamicGiQuality : uint8_t;
namespace LIGHTING {
struct SceneLightingSet;
}
namespace DYNAMICGI {
enum class Status : uint8_t { Off, Unsupported, PreparingScene, MissingEnvironment, ResourceFailure, Ready };
const char* ToString(Status status) noexcept;
struct Stats {
    Status status = Status::Off;
    uint32_t probes = 0, updatedProbes = 0, primaryRays = 0, localLights = 0, droppedLights = 0;
    uint64_t probeBytes = 0, sequence = 0;
};
struct Frame {
    LIGHTPROBE::LightProbeVolumeRuntimeData volume{};
    D3D12_GPU_DESCRIPTOR_HANDLE shTable{};
    explicit operator bool() const { return volume.valid && shTable.ptr != 0; }
};
// 単一 scene の world-space field。AS は frame pipeline から借用し、GI は所有/構築しない。
// 同一 fence では一度だけ更新し、別 scene の同時 view へ履歴を流用しない。
class DynamicDiffuseGi final {
public:
    DynamicDiffuseGi();
    ~DynamicDiffuseGi();
    Frame Prepare(const GFX::Context& graphics, const RAYTRACING::RuntimeRayTracingFrame& rayScene,
                  const MESHRENDERER::MeshMaterialGpuFrameView& materials,
                  const RAYTRACING::RuntimeRayLightingFrame& lighting, MATH::Vec3 cameraPosition,
                  DynamicGiQuality quality, uint64_t frameSerial);
    void Reset(const GFX::Context& graphics);
    const Stats& GetStats() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace DYNAMICGI
} // namespace HIKARI::RENDER3D
