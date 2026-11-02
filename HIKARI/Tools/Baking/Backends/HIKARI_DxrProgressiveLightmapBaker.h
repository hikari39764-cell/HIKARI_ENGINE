#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/RayTracing/HIKARI_DxrAccelerationStructure.h"
#include "Render3D/Material/HIKARI_GpuMaterialSnapshot.h"
#include "Render3D/Lighting/Transport/HIKARI_LightingTransportContract.h"
#include "Render3D/Lighting/Transport/HIKARI_LightingTransportScene.h"

namespace HIKARI::GFX {
struct Context;
}

namespace HIKARI::TOOLS::BAKING {

enum class ProgressiveLightmapBakeState : uint8_t {
    Idle,
    Sampling,
    ReadbackPending,
    Completed,
    Failed,
};

// GPU の texel ごとの積算（HIKARI_DxrLightmapCS.hlsl の LightmapAccumulation と同じ並び）。radiance.rgb は裏面に
// 当たらなかった標本の放射輝度の和、w は全標本数。statistics.x は最初の光線が裏面に当たった標本数、z は最初の光線が
// 空へ抜けた標本数。y は光照贴图では表の標本の輝度の二乗和、探針では最初に当たるまでの距離の和で、w はその二乗和。
struct DxrTransportAccumulation {
    MATH::Vec4 radiance{};
    MATH::Vec4 statistics{};
};

// 探針一つの方向の数（16x16 の八面体。実行時の可視性の atlas と同じ並び）。
inline constexpr uint32_t kDxrProbeDirectionCount = 256u;

struct ProgressiveLightmapBakeStatus {
    ProgressiveLightmapBakeState state = ProgressiveLightmapBakeState::Idle;
    uint32_t completedSamples = 0u;
    uint32_t requestedSamples = 0u;
    uint32_t coveredTexelCount = 0u;
    uint32_t overlapTexelCount = 0u;
    std::string message{};
};

class DxrProgressiveLightmapBaker final {
public:
    ~DxrProgressiveLightmapBaker();
    // インライン ray query が使えるか（光照贴图と探針の焼き込みはどちらもこれが要る）。
    static bool IsAvailable(const GFX::Context& graphics) noexcept;

    bool PrepareScene(const GFX::Context& graphics, const RENDER3D::LIGHTING::TRANSPORT::LightingTransportScene& scene,
                      std::string* outMessage = nullptr);

    bool BeginTarget(const GFX::Context& graphics, const RENDER3D::LIGHTING::TRANSPORT::LightingTransportScene& scene,
                     const RENDER3D::LIGHTING::TRANSPORT::LightingTransportLightmapPage& target,
                     const RENDER3D::LIGHTING::TRANSPORT::LightingTransportBakeSettings& settings,
                     std::string* outMessage = nullptr);

    // 探針（位置ごとに kDxrProbeDirectionCount の方向へ、届く放射輝度と最初に当たる距離を光照贴图と同じ経路で積む）。
    // maximumDistance は当たらなかった方向の距離（可視性の正規化の上限）。
    bool BeginProbeTarget(const GFX::Context& graphics, std::span<const MATH::Vec3> positions, float maximumDistance,
                          const RENDER3D::LIGHTING::TRANSPORT::LightingTransportBakeSettings& settings,
                          std::string* outMessage = nullptr);

    bool RecordSamples(const GFX::Context& graphics, uint32_t maximumSamplesThisFrame = 4u);
    bool QueueReadback(const GFX::Context& graphics, std::string* outMessage = nullptr);
    bool TrySave(const GFX::Context& graphics, const std::filesystem::path& outputPath,
                 std::string* outMessage = nullptr);
    // 探針の積算（探針ごとに kDxrProbeDirectionCount 個）。読み戻しが済むまで false。
    bool TryReadProbes(const GFX::Context& graphics, std::vector<DxrTransportAccumulation>& outCells);

    const ProgressiveLightmapBakeStatus& GetStatus() const noexcept;
    void ResetTarget();
    void Reset();

    // Frozen CPU/GPU transfer records. Public only so translation-unit
    // helpers can validate their exact StructuredBuffer layout.
    struct GpuTriangle;
    struct GpuLight;
    struct DispatchConstants;

private:
    bool CreatePipeline(ID3D12Device* device, std::string* outMessage);
    // texel を上げ、積算を零にして標本の記録を始める（光照贴图と探針の共通）。
    bool StartSampling(const GFX::Context& graphics, const void* texelData, size_t texelByteSize,
                       std::string* outMessage);
    bool ReadbackReady(const GFX::Context& graphics) const noexcept;
    bool SaveReadbackDds(const std::filesystem::path& outputPath, std::string* outMessage);
    UINT64 AccumulationBytes() const noexcept;

    GFX::RAYTRACING::DxrTriangleAccelerationStructure accelerationStructure_{};
    RENDER3D::MATERIAL::GpuMaterialSnapshot materialSnapshot_{};
    RENDER3D::TextureResourceSnapshot textureSnapshot_{};
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_{};
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> triangleBuffer_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> lightBuffer_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> atmosphereBuffer_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> texelBuffer_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> outputBuffer_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> outputInitializationBuffer_{};
    Microsoft::WRL::ComPtr<ID3D12Resource> readbackBuffer_{};

    RENDER3D::LIGHTING::TRANSPORT::LightingTransportEnvironment environment_{};
    std::vector<uint8_t> validTexels_{};
    std::vector<uint32_t> texelOwnerIndices_{};
    std::vector<ASSETS::LIGHTING::LightmapAtlasAllocation> atlasAllocations_{};
    ProgressiveLightmapBakeStatus status_{};
    uint32_t width_ = 0u;
    uint32_t height_ = 0u;
    uint32_t bounceCount_ = 0u;
    uint32_t triangleCount_ = 0u;
    uint32_t opaqueTriangleCount_ = 0u;
    uint32_t lightCount_ = 0u;
    uint32_t skyDescriptorIndex_ = UINT32_MAX;
    float probeMaximumDistance_ = 0.0f; // 0 は光照贴图の目標
    uint64_t readbackFenceValue_ = 0u;
    bool scenePrepared_ = false;
};

} // namespace HIKARI::TOOLS::BAKING
