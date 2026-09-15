#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <d3d12.h>

#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"

namespace HIKARI::RENDER3D::LIGHTPROBE {

    inline constexpr size_t kLightProbeVolumeViewSlotCount =
        LIGHTING::kLightingProviderViewSlotCount;
    inline constexpr size_t kLightProbeVolumeResidentBudget = 8u;

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
        // 動的 probe は SH0.a に信頼度、SH1..3.a に cell 相対の relocation を保持する。
        bool dynamic = false;
    };

    struct IrradianceVolumeGpuData {
        MATH::Vec4 originSampling{};
        MATH::Vec4 spacingIntensity{};
        MATH::Vec4 countsProbeCount{};
        MATH::Vec4 sizeBlendDistance{};
        MATH::Vec4 selectionParams{};
    };

    IrradianceVolumeGpuData BuildIrradianceVolumeGpuData(
        const LightProbeVolumeRuntimeData& volume, float samplingMode);

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

    bool LoadLightProbeVolumeSet(
        std::vector<LightProbeVolumeLoadRequest> requests,
        std::vector<std::string>* outMessages = nullptr);

    const std::vector<LightProbeVolumeRuntimeData>& GetVolumeSet();
    void PrepareView(
        const MATH::Vec3& cameraPosition,
        const MATH::Vec3& viewFocusPosition);
    const LightProbeVolumeViewSelection& GetViewSelection();
    const LightProbeVolumeRuntimeData* GetViewVolume(size_t slot);

    bool IsLightProbeVolumeSamplingSuppressed();

    class ScopedLightProbeVolumeSamplingSuppress {
    public:
        ScopedLightProbeVolumeSamplingSuppress();
        ~ScopedLightProbeVolumeSamplingSuppress();

        ScopedLightProbeVolumeSamplingSuppress(
            const ScopedLightProbeVolumeSamplingSuppress&) = delete;
        ScopedLightProbeVolumeSamplingSuppress& operator=(
            const ScopedLightProbeVolumeSamplingSuppress&) = delete;

    private:
        LightProbeVolumeViewSelection previousSelection_{};
    };

    // GPU 生成の現在フレームだけを先頭 slot に公開する。残り slot は既存 baked set。
    // リソース所有/生成は caller。scope を抜ければ capture 等の旧選択へ戻る。
    class ScopedLightProbeVolumeOverride final {
    public:
        ScopedLightProbeVolumeOverride(const LightProbeVolumeRuntimeData& data,
            D3D12_GPU_DESCRIPTOR_HANDLE shTable);
        ~ScopedLightProbeVolumeOverride();
        ScopedLightProbeVolumeOverride(const ScopedLightProbeVolumeOverride&) = delete;
        ScopedLightProbeVolumeOverride& operator=(const ScopedLightProbeVolumeOverride&) = delete;
        const LightProbeVolumeRuntimeData& GetData() const { return *data_; }
        D3D12_GPU_DESCRIPTOR_HANDLE GetTable() const { return table_; }
    private:
        const LightProbeVolumeRuntimeData* data_ = nullptr;
        D3D12_GPU_DESCRIPTOR_HANDLE table_{};
        ScopedLightProbeVolumeOverride* previous_ = nullptr;
        bool active_ = false;
    };

    // View slot に選ばれた SH9 Texture3D descriptor table を返す。
    D3D12_GPU_DESCRIPTOR_HANDLE GetViewShVolumeSrvTable(size_t slot);

    LightProbeVolumeDebugState GetDebugState();
    bool IsValid();

} // namespace HIKARI::RENDER3D::LIGHTPROBE
