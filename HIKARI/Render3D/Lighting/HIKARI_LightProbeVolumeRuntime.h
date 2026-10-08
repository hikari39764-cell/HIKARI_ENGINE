#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include <d3d12.h>

#include "Core/Math/HIKARI_Math3D.h"
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"

namespace HIKARI::RENDER3D::LIGHTPROBE {

inline constexpr size_t kLightProbeVolumeViewSlotCount = LIGHTING::kLightingProviderViewSlotCount;
inline constexpr size_t kLightProbeVolumeResidentBudget = 8u;
// 動的 GI の volume（手置きの区域、近景、遠景）。HIKARI_SkyEnvironmentCommon.hlsli と同じ数。
inline constexpr size_t kDynamicGiVolumeCount = 3u;

struct LightProbeVolumeLoadRequest {
    std::string id{};
    std::filesystem::path path{};
    float intensity = 1.0f;
    float blendDistance = 1.0f;
    float normalBias = 0.2f;
    int priority = 0;
};

struct LightProbeVolumeRuntimeData {
    std::string id{};
    bool enabled = false;
    bool valid = false;
    MATH::Vec3 origin{};
    MATH::Vec3 size{};
    MATH::Vec3 spacing{};
    uint32_t countX = 0;
    uint32_t countY = 0;
    uint32_t countZ = 0;
    uint32_t probeCount = 0;
    float intensity = 1.0f;
    float blendDistance = 1.0f;
    float normalBias = 0.2f;
    int priority = 0;
    std::string sourcePath{};
    uint32_t visibilityTextureDescriptorIndex = UINT32_MAX;
    float visibilityDistanceScale = 0.0f;
    // 動的 probe の描画側は SH9 ではなく probe atlas（visibilityTextureDescriptorIndex）を読む。
    bool dynamic = false;
};

struct IrradianceVolumeGpuData {
    MATH::Vec4 originSampling{};
    MATH::Vec4 spacingIntensity{};
    MATH::Vec4 countsProbeCount{};
    MATH::Vec4 sizeBlendDistance{};
    MATH::Vec4 selectionParams{};
};

IrradianceVolumeGpuData BuildIrradianceVolumeGpuData(const LightProbeVolumeRuntimeData& volume, float samplingMode);

struct LightProbeVolumeViewSelection {
    std::array<size_t, kLightProbeVolumeViewSlotCount> providerIndices{};
    std::array<float, kLightProbeVolumeViewSlotCount> viewInfluences{};
    size_t count = 0;
};

struct LightProbeVolumeDebugState {
    bool valid = false;
    bool srvReady = false;
    bool hasBuffer = false;
    uint32_t residentCount = 0;
    uint32_t selectedCount = 0;
    uint32_t probeCount = 0;
    uint32_t countX = 0;
    uint32_t countY = 0;
    uint32_t countZ = 0;
    uint64_t srvHeapPtr = 0;
    uint64_t bufferPtr = 0;
    std::string sourcePath{};
};

void Reset();

bool LoadLightProbeVolumeSet(std::vector<LightProbeVolumeLoadRequest> requests,
                             std::vector<std::string>* outMessages = nullptr);

const std::vector<LightProbeVolumeRuntimeData>& GetVolumeSet();
void PrepareView(const MATH::Vec3& cameraPosition, const MATH::Vec3& viewFocusPosition);
const LightProbeVolumeViewSelection& GetViewSelection();
const LightProbeVolumeRuntimeData* GetViewVolume(size_t slot);

bool IsLightProbeVolumeSamplingSuppressed();

class ScopedLightProbeVolumeSamplingSuppress {
public:
    ScopedLightProbeVolumeSamplingSuppress();
    ~ScopedLightProbeVolumeSamplingSuppress();

    ScopedLightProbeVolumeSamplingSuppress(const ScopedLightProbeVolumeSamplingSuppress&) = delete;
    ScopedLightProbeVolumeSamplingSuppress& operator=(const ScopedLightProbeVolumeSamplingSuppress&) = delete;

private:
    LightProbeVolumeViewSelection previousSelection_{};
};

// GPU 生成の動的 GI の現在フレームの volume を優先順に公開する。焼き込みの view slot とは別枠。
// リソース所有/生成は caller。scope を抜ければ以前の公開へ戻り、probe 採取の抑制中（capture 等）は空になる。
class ScopedDynamicGiVolumes final {
public:
    explicit ScopedDynamicGiVolumes(std::span<const LightProbeVolumeRuntimeData> volumes);
    ~ScopedDynamicGiVolumes();
    ScopedDynamicGiVolumes(const ScopedDynamicGiVolumes&) = delete;
    ScopedDynamicGiVolumes& operator=(const ScopedDynamicGiVolumes&) = delete;

private:
    std::span<const LightProbeVolumeRuntimeData> previous_{};
};
std::span<const LightProbeVolumeRuntimeData> GetDynamicGiVolumes();

// View slot に選ばれた SH9 Texture3D descriptor table を返す。
D3D12_GPU_DESCRIPTOR_HANDLE GetViewShVolumeSrvTable(size_t slot);

LightProbeVolumeDebugState GetDebugState();
bool IsValid();

} // namespace HIKARI::RENDER3D::LIGHTPROBE
