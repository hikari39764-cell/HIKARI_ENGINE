#pragma once

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Resources/HIKARI_ClusterGeometryResourceSystem.h"

namespace HIKARI::RENDER3D::CLUSTER {
struct ClusterRayTracingRange {
    uint32_t surfaceIndex = 0;
    uint32_t firstPrimitive = 0;
    uint32_t primitiveCount = 0;
    uint32_t firstCluster = 0;
    uint32_t clusterCount = 0;
};

struct ClusterRayTracingChunk {
    std::vector<ClusterRayTracingRange> ranges;
    uint32_t primitiveCount = 0;
};

// Surface の root range は全 section の LOD0。縮小 LOD を重ねて trace しない。
// 大きな surface も primitive 境界で分割し、一つの AS build が予算を超えない。
bool BuildClusterRayTracingChunks(const ClusterGeometryResourceRecord& source, uint32_t maximumTrianglesPerChunk,
                                  uint32_t maximumRangesPerChunk, uint32_t maximumChunkCount,
                                  std::vector<ClusterRayTracingChunk>& output);

// 頂点/属性は既存 HCMESH GPU buffer を共有し、RT 用 global index だけ生成する。
// LOD/材質/インスタンス選択と BLAS の常駐予算は runtime RT scene 側の責務。
struct ClusterRayTracingGeometry final {
    ClusterRayTracingGeometry() = default;
    ~ClusterRayTracingGeometry() { Reset(GFX::GetCurrentContext()); }
    ClusterRayTracingGeometry(const ClusterRayTracingGeometry&) = delete;
    ClusterRayTracingGeometry& operator=(const ClusterRayTracingGeometry&) = delete;
    Microsoft::WRL::ComPtr<ID3D12Resource> geometry;
    Microsoft::WRL::ComPtr<ID3D12Resource> indices;
    ClusterGeometryPackedLayout layout{};
    uint64_t sourceRevision = 0;
    uint64_t deviceGeneration = 0;
    uint64_t indexAllocationBytes = 0;

    void Reset(const GFX::Context& graphics);
    // deformedPositions が 0 でなければ、頂点位置はそこ（float3 の並び、変形後）から読む。添字は共有する。
    bool DescribeTriangles(uint32_t firstPrimitive, uint32_t primitiveCount, D3D12_RAYTRACING_GEOMETRY_DESC& output,
                           D3D12_GPU_VIRTUAL_ADDRESS deformedPositions = 0) const;
};

class ClusterRayTracingGeometryBuilder final {
public:
    ClusterRayTracingGeometryBuilder() = default;
    ~ClusterRayTracingGeometryBuilder();
    ClusterRayTracingGeometryBuilder(const ClusterRayTracingGeometryBuilder&) = delete;
    ClusterRayTracingGeometryBuilder& operator=(const ClusterRayTracingGeometryBuilder&) = delete;
    bool Build(const GFX::Context& graphics, const ClusterGeometryResourceRecord& source, ID3D12Resource* geometry,
               uint64_t maximumIndexAllocationBytes, ClusterRayTracingGeometry& output);
    void Reset(const GFX::Context& graphics);

private:
    bool EnsurePipeline(const GFX::Context& graphics);
    Microsoft::WRL::ComPtr<ID3D12RootSignature> root_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
    ID3D12Device* device_ = nullptr;
    uint64_t deviceGeneration_ = 0;
};
} // namespace HIKARI::RENDER3D::CLUSTER
