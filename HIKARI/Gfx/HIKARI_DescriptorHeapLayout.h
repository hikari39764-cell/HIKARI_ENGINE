#pragma once

#include <cstdint>
#include <d3d12.h>

#include "Gfx/HIKARI_GfxContext.h"

namespace HIKARI::GFX::DESCRIPTOR {

constexpr UINT kUserSrvCount = 3968;
constexpr UINT kUserSrvBegin = 0;
constexpr UINT kSystemSrvBegin = kUserSrvCount;
// Light probe volume の SH 係数 Texture3D。descriptor table として一括
// バインドするため 9 スロットは必ず連続で確保する。
constexpr UINT kLightProbeShVolumeTextureCount = 9;
constexpr UINT kLightProbeVolumeSetCount = 2;
constexpr UINT kLightProbeVolumeResidentSetCount = 8;
// Ten appended slots complete the frame-1/frame-2 rings whose frame-0
// slots remain in the original fixed block.
constexpr UINT kSystemSrvFixedFrameRingExtraCount = 10u;
constexpr UINT kSystemSrvFixedCount =
    33 + kLightProbeVolumeResidentSetCount * kLightProbeShVolumeTextureCount + kSystemSrvFixedFrameRingExtraCount;
constexpr UINT kSystemSrvDynamicCount = 512;
constexpr UINT kDepthPyramidTransientDescriptorCount = 384;
constexpr UINT kSystemSrvAuxUsedCount = kDepthPyramidTransientDescriptorCount + 1;
constexpr UINT kSystemSrvReservedCount = kSystemSrvFixedCount + kSystemSrvDynamicCount + kSystemSrvAuxUsedCount;
constexpr UINT kMeshGpuSceneTableDescriptorCount = GFX::kFrameResourceCount * 2u;
// Cluster geometry owns a descriptor arena separate from the general
// dynamic system range. Each HCMESH consumes one geometry SRV and one
// metadata SRV, so the arena can admit 512 packed resources without
// competing with frame-local dynamic SRV users.
constexpr UINT kClusterGeometryDescriptorCountPerResource = 2u;
constexpr UINT kClusterGeometryResourceCapacity = 512u;
constexpr UINT kClusterGeometryDescriptorArenaCapacity =
    kClusterGeometryResourceCapacity * kClusterGeometryDescriptorCountPerResource;
constexpr UINT kClusterGeometryDescriptorArenaBegin =
    kSystemSrvBegin + kSystemSrvReservedCount + kMeshGpuSceneTableDescriptorCount;
constexpr UINT kClusterGeometryDescriptorArenaEnd =
    kClusterGeometryDescriptorArenaBegin + kClusterGeometryDescriptorArenaCapacity;
constexpr UINT kSrvHeapCapacity = kClusterGeometryDescriptorArenaEnd;

enum class SystemSrv : UINT {
    SceneColorFrame0 = kSystemSrvBegin + 0,
    SceneDepth = kSystemSrvBegin + 1,
    EditorViewportFrame0 = kSystemSrvBegin + 2,
    ImGuiFont = kSystemSrvBegin + 3,

    IblIrradiance = kSystemSrvBegin + 4,
    IblPrefiltered = kSystemSrvBegin + 5,
    IblBrdfLut = kSystemSrvBegin + 6,
    SceneSurface = kSystemSrvBegin + 7,
    SsaoRawFrame0 = kSystemSrvBegin + 8,
    SsaoBlurredFrame0 = kSystemSrvBegin + 9,
    SsaoResolvedFrame0 = kSystemSrvBegin + 10,
    SceneColorPostOpaqueFrame0 = kSystemSrvBegin + 11,
    SceneColorFrame1 = kSystemSrvBegin + 12,
    SceneColorFrame2 = kSystemSrvBegin + 13,
    SceneColorPostOpaqueFrame1 = kSystemSrvBegin + 14,
    SceneColorPostOpaqueFrame2 = kSystemSrvBegin + 15,
    // 布景の A/B 分屏で凍結した A の画（編集器の出力の複製）。
    EditorViewportSnapshot = kSystemSrvBegin + 16,
    PostSceneDepthFrame0 = kSystemSrvBegin + 17,

    MeshObjectDataFrame0 = kSystemSrvBegin + 18,
    MeshObjectDataFrame1 = kSystemSrvBegin + 19,
    MeshObjectDataFrame2 = kSystemSrvBegin + 20,
    MeshMaterialDataFrame0 = kSystemSrvBegin + 21,
    MeshMaterialDataFrame1 = kSystemSrvBegin + 22,
    MeshMaterialDataFrame2 = kSystemSrvBegin + 23,
    // Retired fixed SurfaceGPUScene slots are reused by the per-frame
    // final scene-color snapshot. Existing descriptor indices do not
    // move, and the active mesh table remains after the bounded system
    // ranges.
    SceneColorFinalFrame0 = kSystemSrvBegin + 24,
    SceneColorFinalFrame1 = kSystemSrvBegin + 25,
    SceneColorFinalFrame2 = kSystemSrvBegin + 26,
    ShadowSurfaceGpuSceneFrame0 = kSystemSrvBegin + 27,
    ShadowSurfaceGpuSceneFrame1 = kSystemSrvBegin + 28,
    ShadowSurfaceGpuSceneFrame2 = kSystemSrvBegin + 29,
    ShadowMaterialDataFrame0 = kSystemSrvBegin + 30,
    ShadowMaterialDataFrame1 = kSystemSrvBegin + 31,
    ShadowMaterialDataFrame2 = kSystemSrvBegin + 32,
    // SH 係数 Texture3D ×9 (連続必須、descriptor table で一括バインド)。
    LightProbeShVolume0 = kSystemSrvBegin + 33,
    LightProbeShVolume1 = kSystemSrvBegin + 34,
    LightProbeShVolume2 = kSystemSrvBegin + 35,
    LightProbeShVolume3 = kSystemSrvBegin + 36,
    LightProbeShVolume4 = kSystemSrvBegin + 37,
    LightProbeShVolume5 = kSystemSrvBegin + 38,
    LightProbeShVolume6 = kSystemSrvBegin + 39,
    LightProbeShVolume7 = kSystemSrvBegin + 40,
    LightProbeShVolume8 = kSystemSrvBegin + 41,

    LightProbeShVolumeSecondary0 = kSystemSrvBegin + 42,
    LightProbeShVolumeSecondary1 = kSystemSrvBegin + 43,
    LightProbeShVolumeSecondary2 = kSystemSrvBegin + 44,
    LightProbeShVolumeSecondary3 = kSystemSrvBegin + 45,
    LightProbeShVolumeSecondary4 = kSystemSrvBegin + 46,
    LightProbeShVolumeSecondary5 = kSystemSrvBegin + 47,
    LightProbeShVolumeSecondary6 = kSystemSrvBegin + 48,
    LightProbeShVolumeSecondary7 = kSystemSrvBegin + 49,
    LightProbeShVolumeSecondary8 = kSystemSrvBegin + 50,

    // Gfx-owned frame rings reuse the existing frame-0 fixed slots and
    // append only frame-1/frame-2 slots in the fixed reservation. This
    // keeps old frame-0 indices stable while avoiding dynamic leases.
    EditorViewportFrame1 = kSystemSrvBegin + 105,
    EditorViewportFrame2 = kSystemSrvBegin + 106,
    PostSceneDepthFrame1 = kSystemSrvBegin + 107,
    PostSceneDepthFrame2 = kSystemSrvBegin + 108,
    SsaoRawFrame1 = kSystemSrvBegin + 109,
    SsaoRawFrame2 = kSystemSrvBegin + 110,
    SsaoBlurredFrame1 = kSystemSrvBegin + 111,
    SsaoBlurredFrame2 = kSystemSrvBegin + 112,
    SsaoResolvedFrame1 = kSystemSrvBegin + 113,
    SsaoResolvedFrame2 = kSystemSrvBegin + 114,

    MeshSurfaceGpuSceneFrame0 = kSystemSrvBegin + kSystemSrvReservedCount,
    MeshMassRenderProxyFrame0 = kSystemSrvBegin + kSystemSrvReservedCount + 1,
    MeshSurfaceGpuSceneFrame1 = kSystemSrvBegin + kSystemSrvReservedCount + 2,
    MeshMassRenderProxyFrame1 = kSystemSrvBegin + kSystemSrvReservedCount + 3,
    MeshSurfaceGpuSceneFrame2 = kSystemSrvBegin + kSystemSrvReservedCount + 4,
    MeshMassRenderProxyFrame2 = kSystemSrvBegin + kSystemSrvReservedCount + 5,
};

constexpr UINT kSystemSrvUsedCount = kSystemSrvFixedCount;
constexpr UINT kSystemSrvDynamicBegin = kSystemSrvBegin + kSystemSrvUsedCount;
constexpr UINT kSystemSrvAuxBegin = kSystemSrvDynamicBegin + kSystemSrvDynamicCount;
constexpr UINT kDepthPyramidTransientDescriptorBegin = kSystemSrvAuxBegin;
constexpr UINT kDepthPyramidTransientDescriptorEnd =
    kDepthPyramidTransientDescriptorBegin + kDepthPyramidTransientDescriptorCount;
constexpr UINT kGpuDepthVisibilityTransientDescriptorCount = kDepthPyramidTransientDescriptorCount;
constexpr UINT kGpuDepthVisibilityTransientDescriptorBegin = kDepthPyramidTransientDescriptorBegin;
constexpr UINT kGpuDepthVisibilityTransientDescriptorEnd = kDepthPyramidTransientDescriptorEnd;
constexpr UINT kClusterCullFallbackHzbSrv = kDepthPyramidTransientDescriptorEnd;

constexpr UINT ToIndex(SystemSrv slot) { return static_cast<UINT>(slot); }

constexpr SystemSrv FrameSlot(SystemSrv firstFrameSlot, uint32_t frameIndex) {
    return static_cast<SystemSrv>(ToIndex(firstFrameSlot) + (frameIndex % GFX::kFrameResourceCount));
}

constexpr UINT ToFrameIndex(SystemSrv firstFrameSlot, uint32_t frameIndex) {
    return ToIndex(firstFrameSlot) + (frameIndex % GFX::kFrameResourceCount);
}

constexpr SystemSrv MeshSurfaceGpuSceneFrameSlot(uint32_t frameIndex) {
    switch (frameIndex % GFX::kFrameResourceCount) {
    case 1u: return SystemSrv::MeshSurfaceGpuSceneFrame1;
    case 2u: return SystemSrv::MeshSurfaceGpuSceneFrame2;
    default: return SystemSrv::MeshSurfaceGpuSceneFrame0;
    }
}

constexpr SystemSrv MeshMassRenderProxyFrameSlot(uint32_t frameIndex) {
    switch (frameIndex % GFX::kFrameResourceCount) {
    case 1u: return SystemSrv::MeshMassRenderProxyFrame1;
    case 2u: return SystemSrv::MeshMassRenderProxyFrame2;
    default: return SystemSrv::MeshMassRenderProxyFrame0;
    }
}

constexpr SystemSrv EditorViewportFrameSlot(uint32_t frameIndex) {
    switch (frameIndex % GFX::kFrameResourceCount) {
    case 1u: return SystemSrv::EditorViewportFrame1;
    case 2u: return SystemSrv::EditorViewportFrame2;
    default: return SystemSrv::EditorViewportFrame0;
    }
}

constexpr SystemSrv PostSceneDepthFrameSlot(uint32_t frameIndex) {
    switch (frameIndex % GFX::kFrameResourceCount) {
    case 1u: return SystemSrv::PostSceneDepthFrame1;
    case 2u: return SystemSrv::PostSceneDepthFrame2;
    default: return SystemSrv::PostSceneDepthFrame0;
    }
}

constexpr SystemSrv SsaoRawFrameSlot(uint32_t frameIndex) {
    switch (frameIndex % GFX::kFrameResourceCount) {
    case 1u: return SystemSrv::SsaoRawFrame1;
    case 2u: return SystemSrv::SsaoRawFrame2;
    default: return SystemSrv::SsaoRawFrame0;
    }
}

constexpr SystemSrv SsaoBlurredFrameSlot(uint32_t frameIndex) {
    switch (frameIndex % GFX::kFrameResourceCount) {
    case 1u: return SystemSrv::SsaoBlurredFrame1;
    case 2u: return SystemSrv::SsaoBlurredFrame2;
    default: return SystemSrv::SsaoBlurredFrame0;
    }
}

constexpr SystemSrv SsaoResolvedFrameSlot(uint32_t frameIndex) {
    switch (frameIndex % GFX::kFrameResourceCount) {
    case 1u: return SystemSrv::SsaoResolvedFrame1;
    case 2u: return SystemSrv::SsaoResolvedFrame2;
    default: return SystemSrv::SsaoResolvedFrame0;
    }
}

constexpr SystemSrv SceneColorFrameSlot(bool postOpaque, uint32_t frameIndex) {
    const uint32_t slot = frameIndex % GFX::kFrameResourceCount;
    if (postOpaque) {
        return slot == 0u
                   ? SystemSrv::SceneColorPostOpaqueFrame0
                   : (slot == 1u ? SystemSrv::SceneColorPostOpaqueFrame1 : SystemSrv::SceneColorPostOpaqueFrame2);
    }
    return slot == 0u ? SystemSrv::SceneColorFrame0
                      : (slot == 1u ? SystemSrv::SceneColorFrame1 : SystemSrv::SceneColorFrame2);
}

constexpr SystemSrv SceneColorFinalFrameSlot(uint32_t frameIndex) {
    switch (frameIndex % GFX::kFrameResourceCount) {
    case 1u: return SystemSrv::SceneColorFinalFrame1;
    case 2u: return SystemSrv::SceneColorFinalFrame2;
    default: return SystemSrv::SceneColorFinalFrame0;
    }
}

inline D3D12_CPU_DESCRIPTOR_HANDLE CpuAt(ID3D12DescriptorHeap* heap, UINT descriptorSize, UINT index) {
    D3D12_CPU_DESCRIPTOR_HANDLE handle{};
    if (heap == nullptr) {
        return handle;
    }

    handle = heap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(descriptorSize) * index;
    return handle;
}

inline D3D12_GPU_DESCRIPTOR_HANDLE GpuAt(ID3D12DescriptorHeap* heap, UINT descriptorSize, UINT index) {
    D3D12_GPU_DESCRIPTOR_HANDLE handle{};
    if (heap == nullptr) {
        return handle;
    }

    handle = heap->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<UINT64>(descriptorSize) * index;
    return handle;
}

static_assert(kSystemSrvReservedCount >= kSystemSrvUsedCount + kSystemSrvDynamicCount + kSystemSrvAuxUsedCount);
static_assert(kUserSrvCount == 3968);
// Shader-visible absolute indices are mirrored by the HLSL contract.
// Generic dynamic descriptors are allocator-only; cluster bindings use
// the dedicated shader-visible arena below.
static_assert(kSystemSrvFixedCount == 115);
static_assert(kSystemSrvFixedFrameRingExtraCount == 10u);
static_assert(kSystemSrvDynamicBegin == 4083);
static_assert(kSystemSrvDynamicCount == 512);
static_assert(kSystemSrvAuxBegin == 4595);
static_assert(kClusterGeometryDescriptorArenaCapacity == 1024u);
static_assert(kClusterGeometryDescriptorArenaCapacity % kClusterGeometryDescriptorCountPerResource == 0u);
static_assert(kClusterGeometryDescriptorArenaBegin ==
              kSystemSrvBegin + kSystemSrvReservedCount + kMeshGpuSceneTableDescriptorCount);
static_assert(kClusterGeometryDescriptorArenaEnd == kSrvHeapCapacity);
static_assert(kSystemSrvBegin < kSrvHeapCapacity);
static_assert(kSystemSrvDynamicBegin < kSrvHeapCapacity);
static_assert(kSystemSrvDynamicBegin + kSystemSrvDynamicCount <= kSrvHeapCapacity);
static_assert(kDepthPyramidTransientDescriptorBegin < kSrvHeapCapacity);
static_assert(kDepthPyramidTransientDescriptorEnd <= kSrvHeapCapacity);
static_assert(kGpuDepthVisibilityTransientDescriptorBegin == kDepthPyramidTransientDescriptorBegin);
static_assert(kGpuDepthVisibilityTransientDescriptorEnd == kDepthPyramidTransientDescriptorEnd);
static_assert(kClusterCullFallbackHzbSrv < kSrvHeapCapacity);
static_assert(ToIndex(SystemSrv::SceneColorFrame0) < kSrvHeapCapacity);
static_assert(ToIndex(SystemSrv::SceneColorFrame2) < kSrvHeapCapacity);
static_assert(ToIndex(SystemSrv::SceneColorPostOpaqueFrame0) < kSrvHeapCapacity);
static_assert(ToIndex(SystemSrv::SceneColorPostOpaqueFrame2) < kSrvHeapCapacity);
static_assert(ToIndex(SystemSrv::SceneColorFinalFrame0) < kSrvHeapCapacity);
static_assert(ToIndex(SystemSrv::SceneColorFinalFrame2) < kSrvHeapCapacity);
static_assert(ToIndex(SystemSrv::SceneColorFinalFrame2) + 1u == ToIndex(SystemSrv::ShadowSurfaceGpuSceneFrame0));
static_assert(ToIndex(SystemSrv::SceneDepth) < kSrvHeapCapacity);
static_assert(ToIndex(SystemSrv::EditorViewportFrame0) < kSrvHeapCapacity);
static_assert(ToIndex(SystemSrv::EditorViewportFrame0) == kSystemSrvBegin + 2u);
static_assert(ToIndex(SystemSrv::EditorViewportFrame1) == kSystemSrvBegin + 105u);
static_assert(ToIndex(EditorViewportFrameSlot(0u)) == ToIndex(SystemSrv::EditorViewportFrame0));
static_assert(ToIndex(EditorViewportFrameSlot(1u)) == ToIndex(SystemSrv::EditorViewportFrame1));
static_assert(ToIndex(EditorViewportFrameSlot(2u)) == ToIndex(SystemSrv::EditorViewportFrame2));
static_assert(ToIndex(SystemSrv::LightProbeShVolume0) +
                  kLightProbeVolumeResidentSetCount * kLightProbeShVolumeTextureCount ==
              ToIndex(SystemSrv::EditorViewportFrame1));
static_assert(ToIndex(SystemSrv::EditorViewportFrame2) < kSystemSrvBegin + kSystemSrvFixedCount);
static_assert(ToIndex(SystemSrv::PostSceneDepthFrame0) == kSystemSrvBegin + 17u);
static_assert(ToIndex(SystemSrv::PostSceneDepthFrame1) == ToIndex(SystemSrv::EditorViewportFrame2) + 1u);
static_assert(ToIndex(PostSceneDepthFrameSlot(0u)) == ToIndex(SystemSrv::PostSceneDepthFrame0));
static_assert(ToIndex(PostSceneDepthFrameSlot(2u)) == ToIndex(SystemSrv::PostSceneDepthFrame2));
static_assert(ToIndex(SystemSrv::PostSceneDepthFrame2) < kSystemSrvBegin + kSystemSrvFixedCount);
static_assert(ToIndex(SystemSrv::SsaoRawFrame0) == kSystemSrvBegin + 8u);
static_assert(ToIndex(SystemSrv::SsaoRawFrame1) == ToIndex(SystemSrv::PostSceneDepthFrame2) + 1u);
static_assert(ToIndex(SystemSrv::SsaoRawFrame2) < kSystemSrvBegin + kSystemSrvFixedCount);
static_assert(ToIndex(SystemSrv::SsaoBlurredFrame0) == kSystemSrvBegin + 9u);
static_assert(ToIndex(SystemSrv::SsaoBlurredFrame1) == ToIndex(SystemSrv::SsaoRawFrame2) + 1u);
static_assert(ToIndex(SystemSrv::SsaoBlurredFrame2) < kSystemSrvBegin + kSystemSrvFixedCount);
static_assert(ToIndex(SystemSrv::SsaoResolvedFrame0) == kSystemSrvBegin + 10u);
static_assert(ToIndex(SystemSrv::SsaoResolvedFrame1) == ToIndex(SystemSrv::SsaoBlurredFrame2) + 1u);
static_assert(ToIndex(SsaoResolvedFrameSlot(0u)) == ToIndex(SystemSrv::SsaoResolvedFrame0));
static_assert(ToIndex(SsaoResolvedFrameSlot(2u)) == ToIndex(SystemSrv::SsaoResolvedFrame2));
static_assert(ToIndex(SystemSrv::SsaoResolvedFrame2) < kSystemSrvBegin + kSystemSrvFixedCount);
static_assert(ToIndex(SystemSrv::ImGuiFont) < kSrvHeapCapacity);
static_assert(ToIndex(SystemSrv::IblIrradiance) < kSrvHeapCapacity);
static_assert(ToIndex(SystemSrv::IblPrefiltered) < kSrvHeapCapacity);
static_assert(ToIndex(SystemSrv::IblBrdfLut) < kSrvHeapCapacity);
static_assert(ToIndex(SystemSrv::LightProbeShVolume8) - ToIndex(SystemSrv::LightProbeShVolume0) + 1 ==
              kLightProbeShVolumeTextureCount);
static_assert(ToIndex(SystemSrv::LightProbeShVolumeSecondary8) - ToIndex(SystemSrv::LightProbeShVolumeSecondary0) + 1 ==
              kLightProbeShVolumeTextureCount);
static_assert(ToIndex(SystemSrv::SceneSurface) < kSrvHeapCapacity);
static_assert(ToIndex(SystemSrv::SsaoRawFrame0) < kSrvHeapCapacity);
static_assert(ToIndex(SystemSrv::SsaoBlurredFrame0) < kSrvHeapCapacity);
static_assert(ToIndex(SystemSrv::SsaoResolvedFrame0) < kSrvHeapCapacity);
static_assert(ToIndex(SystemSrv::PostSceneDepthFrame0) < kSrvHeapCapacity);
static_assert(ToIndex(SystemSrv::MeshObjectDataFrame0) < kSrvHeapCapacity);
static_assert(ToIndex(SystemSrv::MeshObjectDataFrame2) < kSrvHeapCapacity);
static_assert(ToIndex(SystemSrv::MeshMaterialDataFrame0) < kSrvHeapCapacity);
static_assert(ToIndex(SystemSrv::MeshMaterialDataFrame2) < kSrvHeapCapacity);
static_assert(ToIndex(SystemSrv::MeshSurfaceGpuSceneFrame0) < kSrvHeapCapacity);
static_assert(ToIndex(SystemSrv::MeshSurfaceGpuSceneFrame2) < kSrvHeapCapacity);
static_assert(ToIndex(SystemSrv::ShadowSurfaceGpuSceneFrame0) < kSrvHeapCapacity);
static_assert(ToIndex(SystemSrv::ShadowSurfaceGpuSceneFrame2) < kSrvHeapCapacity);
static_assert(ToIndex(SystemSrv::MeshMassRenderProxyFrame2) < kSrvHeapCapacity);
static_assert(kClusterGeometryDescriptorArenaBegin < kSrvHeapCapacity);
static_assert(kClusterGeometryDescriptorArenaEnd <= kSrvHeapCapacity);
static_assert(ToIndex(SystemSrv::MeshMassRenderProxyFrame2) + 1u == kClusterGeometryDescriptorArenaBegin);
static_assert(ToIndex(SystemSrv::MeshMassRenderProxyFrame0) == ToIndex(SystemSrv::MeshSurfaceGpuSceneFrame0) + 1u &&
              ToIndex(SystemSrv::MeshMassRenderProxyFrame1) == ToIndex(SystemSrv::MeshSurfaceGpuSceneFrame1) + 1u &&
              ToIndex(SystemSrv::MeshMassRenderProxyFrame2) == ToIndex(SystemSrv::MeshSurfaceGpuSceneFrame2) + 1u);
static_assert(ToIndex(SystemSrv::ShadowMaterialDataFrame0) < kSrvHeapCapacity);
static_assert(ToIndex(SystemSrv::ShadowMaterialDataFrame2) < kSrvHeapCapacity);
static_assert(ToIndex(SystemSrv::LightProbeShVolume8) ==
                  ToIndex(SystemSrv::LightProbeShVolume0) + kLightProbeShVolumeTextureCount - 1,
              "SH volume texture slots must stay contiguous for the descriptor table");
static_assert(ToIndex(SystemSrv::LightProbeShVolumeSecondary8) ==
                  ToIndex(SystemSrv::LightProbeShVolume0) +
                      kLightProbeVolumeSetCount * kLightProbeShVolumeTextureCount - 1,
              "SH volume sets must stay contiguous for view selection");
static_assert(ToIndex(SystemSrv::LightProbeShVolumeSecondary8) < kSystemSrvBegin + kSystemSrvFixedCount);
static_assert(ToIndex(SystemSrv::LightProbeShVolume0) +
                      kLightProbeVolumeResidentSetCount * kLightProbeShVolumeTextureCount <=
                  kSystemSrvBegin + kSystemSrvFixedCount,
              "Resident light probe descriptor tables must stay in the fixed range");

} // namespace HIKARI::GFX::DESCRIPTOR
