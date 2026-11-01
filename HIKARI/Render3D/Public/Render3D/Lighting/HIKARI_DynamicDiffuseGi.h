#pragma once
#include <array>
#include <memory>
#include <span>
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
enum class Status : uint8_t {
    Off,
    Unsupported,
    PreparingScene,
    // 読み込み中などで解決できない面が加速構造に無い（光線が抜けて光が漏れる）。
    MissingOccluders,
    MissingEnvironment,
    ResourceFailure,
    Ready
};
const char* ToString(Status status) noexcept;
struct Stats {
    Status status = Status::Off;
    uint32_t probes = 0, updatedProbes = 0, primaryRays = 0, localLights = 0, droppedLights = 0;
    uint64_t probeBytes = 0, sequence = 0;
};
// 描画側が読む volume（優先順）。描画側は probe atlas だけを読み、SH9 の table は使わない。
struct Frame {
    std::array<LIGHTPROBE::LightProbeVolumeRuntimeData, LIGHTPROBE::kDynamicGiVolumeCount> volumes{};
    size_t count = 0;
    std::span<const LIGHTPROBE::LightProbeVolumeRuntimeData> Volumes() const { return {volumes.data(), count}; }
};
// 単一 scene の world-space field（手置きの実時間区域、相機を追う近景と遠景の cascade）。AS は frame pipeline
// から借用し、GI は所有/構築しない。同一 fence では一度だけ更新し、別 scene の同時 view へ履歴を流用しない。
class DynamicDiffuseGi final {
public:
    DynamicDiffuseGi();
    ~DynamicDiffuseGi();
    Frame Prepare(const GFX::Context& graphics, const RAYTRACING::RuntimeRayTracingFrame& rayScene,
                  const MESHRENDERER::MeshMaterialGpuFrameView& materials,
                  const RAYTRACING::RuntimeRayLightingFrame& lighting,
                  std::span<const LIGHTING::IrradianceVolumeProvider> volumes, MATH::Vec3 cameraPosition,
                  MATH::Vec3 viewTarget, DynamicGiQuality quality, uint64_t frameSerial);
    void Reset(const GFX::Context& graphics);
    const Stats& GetStats() const;

private:
    struct State;
    std::unique_ptr<State> state_;
};
} // namespace DYNAMICGI
} // namespace HIKARI::RENDER3D
