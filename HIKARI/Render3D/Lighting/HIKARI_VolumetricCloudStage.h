#pragma once

#include <cstdint>
#include <d3d12.h>
#include "Core/Math/HIKARI_Math3D.h"
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"

#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"

namespace HIKARI {
struct SceneEnvironment;
}

namespace HIKARI::RENDER3D::PIPELINE {
struct RenderFrameContext;
}

namespace HIKARI::RENDER3D::TEMPORAL {
struct TemporalFrameState;
}

namespace HIKARI::RENDER3D::CLOUDS {

struct VolumetricCloudStageStats {
    bool requested = false;
    bool active = false;
    bool pipelineReady = false;
    bool resourcesReady = false;
    bool noiseReady = false;
    bool historyValid = false;
    bool rendered = false;
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
    // 1 フレームで追跡する解像度（再構成の 2x2 ごとに 1 画素）と、時間方向に再構成する解像度。
    uint32_t traceWidth = 0;
    uint32_t traceHeight = 0;
    uint32_t historyWidth = 0;
    uint32_t historyHeight = 0;
    uint32_t maxSteps = 0;
    uint32_t lightSamples = 0;
    uint32_t qualityIndex = 0;
    uint32_t traceDispatchCount = 0;
    uint64_t workingSetBytes = 0;
    uint64_t resizeCount = 0;
    uint64_t historyResetCount = 0;
    uint64_t resourceFailureCount = 0;
    bool solarVisibilityReady = false;
    bool skyMapReady = false;
    uint32_t solarResolution = 0;
    uint32_t solarSteps = 0;
    uint64_t solarBytes = 0;
};

struct CloudSolarVisibilityFrame {
    LIGHTING::CloudSolarVisibilityGpuData data{};
    D3D12_GPU_DESCRIPTOR_HANDLE srv{};
    uint64_t fenceValue = 0;
    explicit operator bool() const noexcept { return data.originDescriptor.w > 0 && srv.ptr != 0; }
};

// 環境光へ渡す雲の空（天空 LUT と同じ写像）。rgb は空気遠近込みの乗算済み絶対輝度 [nits]、a は不透明度。
// version は雲の空が変わった時だけ進み、環境光の作り直しを決める。
struct CloudSkyView {
    D3D12_GPU_DESCRIPTOR_HANDLE srv{};
    uint64_t version = 0;
    explicit operator bool() const noexcept { return srv.ptr != 0; }
};

// 雲の下の空気の照らされ方の概算。空気遠近（雲や物体の手前の大気の散乱光）が使う。
struct CloudShadingEstimate {
    // 雲を抜けて空気へ届く主光源の割合。
    float sunVisibility = 1.0f;
    // 雲の下から見上げた空（雲を含む）の平均輝度 [nits]。
    MATH::Vec3 ambientRadiance{};
};

// 雲が見えるか（雲の層が有効で、天気の被覆率がある）。
bool HasVisibleClouds(const SceneEnvironment& environment) noexcept;

// 天気の被覆率・雲型・濃さと主光源から、雲の下の空気の照らされ方を見積もる。雲が無ければ晴天のまま。
CloudShadingEstimate EstimateCloudShading(const SceneEnvironment& environment);

// 雲による照明の入力を作る。地表へ落とす雲の影の表（前向き描画・霧・光線追跡がこのフレームで使う）を返し、
// 環境光へ渡す雲の空（次のフレームの空の描画が使う）を更新する。
CloudSolarVisibilityFrame PrepareCloudLightingFrame(const PIPELINE::RenderFrameContext& frame,
                                                    const TEMPORAL::TemporalFrameState& temporalFrame,
                                                    const SceneEnvironment& environment,
                                                    VolumetricCloudQuality quality);

// 最後に描いた雲の空。雲が見えなくなると空になる。
CloudSkyView GetCloudSkyView();

// 半分の解像度で雲を追跡し、時間方向に混ぜて場面へ合成する。
bool ExecuteVolumetricCloudStage(const PIPELINE::RenderFrameContext& frame,
                                 const TEMPORAL::TemporalFrameState& temporalFrame, const SceneEnvironment& environment,
                                 VolumetricCloudQuality quality);

const VolumetricCloudStageStats& GetVolumetricCloudStageStats();
void ShutdownVolumetricCloudStage();

} // namespace HIKARI::RENDER3D::CLOUDS
