#ifndef HIKARI_CLUSTER_CULL_CONTRACT_INCLUDED
#define HIKARI_CLUSTER_CULL_CONTRACT_INCLUDED

#include "Include/HIKARI_ClusterGeometryConfig.h"

struct ClusterCullInput
{
    float4x4 clusterWorld;
    float4 boundsCenterRadius;
    uint gpuSceneInstanceIndex;
    uint clusterGeometrySrvDescriptorIndex;
    uint clusterGeometryMetadataSrvDescriptorIndex;
    uint firstCluster;
    uint clusterCount;
    uint clusterSurfaceIndex;
    uint passKind;
    uint flags;
    uint clusterIndexCount;
    uint firstPage;
    uint pageCount;
    uint pageTaskBaseIndex;
    uint lodIndex;
    uint sectionIndex;
    uint clusterOffsetBytes;
    uint vertexOffsetBytes;
    uint vertexCount;
    uint meshletPrimitiveOffsetBytes;
    uint meshletPrimitiveCount;
    uint geometryClusterCount;
};

struct ClusterCullVisibleRange
{
    uint gpuSceneInstanceIndex;
    uint clusterGeometrySrvDescriptorIndex;
    uint firstCluster;
    uint clusterCount;
    uint clusterSurfaceIndex;
    uint passKind;
    uint flags;
    uint clusterGeometryMetadataSrvDescriptorIndex;
    uint lodIndex;
    uint pageIndex;
    uint drawBucket;
    uint sectionIndex;
    uint clusterOffsetBytes;
    uint vertexOffsetBytes;
    uint vertexCount;
    uint meshletPrimitiveOffsetBytes;
    uint meshletPrimitiveCount;
    uint geometryClusterCount;
    uint reserved0;
    uint reserved1;
};

struct ClusterCullMeshletDispatchArgument
{
    uint4 rootConstants;
    uint threadGroupCountX;
    uint threadGroupCountY;
    uint threadGroupCountZ;
    uint reserved0;
};

// clusterWorld は task に埋め込まず gpuSceneInstanceIndex 経由で GPU scene
// から読み直す (expand 書き込み + cull 読み出しの帯域を半減させる)。
struct ClusterCullPageTask
{
    uint gpuSceneInstanceIndex;
    uint clusterGeometrySrvDescriptorIndex;
    uint firstCluster;
    uint endCluster;
    uint clusterSurfaceIndex;
    uint passKind;
    uint flags;
    uint pageIndex;
    uint lodIndex;
    uint sectionIndex;
    uint clusterOffsetBytes;
    uint vertexOffsetBytes;
    uint vertexCount;
    uint meshletPrimitiveOffsetBytes;
    uint meshletPrimitiveCount;
    uint geometryClusterCount;
    uint reserved0;
    uint reserved1;
    uint reserved2;
    uint clusterGeometryMetadataSrvDescriptorIndex;
};

cbuffer ClusterCullFrameCB : register(b0)
{
    float4x4 gClusterCullViewProj;
    float4x4 gClusterCullHzbViewProj;
    float4 gClusterCullCameraPosition;
    uint gClusterCullInputCount;
    uint gClusterCullVisibleRangeCapacity;
    uint gClusterCullCandidateCommandCapacity;
    uint gClusterCullEnableFrustumCull;
    uint gClusterCullCandidateCommandBucketCapacity;
    uint gClusterCullClusterSrvPoolBegin;
    uint gClusterCullClusterSrvPoolCount;
    uint gClusterCullEnableConeCull;
    uint gClusterCullEnableDebugCounters;
    uint gClusterCullSurfaceGpuSceneBaseIndex;
    uint gClusterCullPassKind;
    uint gClusterCullPageTaskCapacity;
    uint gClusterCullMergeGapIndexLimit;
    uint gClusterCullMergeRunGapIndexBudget;
    uint gClusterCullMergeMaxIndexSpan;
    uint gClusterCullMergeClusterGapLimit;
    float gClusterCullLodTargetErrorNdc;
    uint gClusterCullEnableLodErrorSelection;
    uint gClusterCullPageTaskGroupSize;
    uint gClusterCullClusterHzbMinScreenPixels;
    uint gClusterCullEnableHzbOcclusion;
    uint gClusterCullHzbWidth;
    uint gClusterCullHzbHeight;
    uint gClusterCullHzbMipCount;
    float gClusterCullHzbDepthBias;
    float gClusterCullHzbMaxScreenRadiusPixels;
    uint gClusterCullOcclusionHistoryCapacity;
    uint gClusterCullTemporalFrameIndex;
    uint gClusterCullHzbOcclusionConfirmFrames;
    uint gClusterCullHzbAllowLargeRectOcclusion;
    uint gClusterCullHzbTestBudget;
    uint gClusterCullVisibleClusterListCapacity;
    uint gClusterCullMeshletFineCullingOwner;
    uint gClusterCullCandidateCommandReserved;
    float gClusterCullLodTransitionRelaxPerLevel;
    float gClusterCullLodErrorRelaxPerLevel;
};

RWStructuredBuffer<ClusterCullVisibleRange> gClusterCullVisibleRanges : register(u0);
RWByteAddressBuffer gClusterCullCounters : register(u1);
RWStructuredBuffer<ClusterCullPageTask> gClusterCullPageTasks : register(u3);
RWStructuredBuffer<uint3> gClusterCullDispatchArguments : register(u4);
RWStructuredBuffer<ClusterCullMeshletDispatchArgument> gClusterCullMeshletDispatchArguments : register(u5);
RWByteAddressBuffer gClusterCullOcclusionHistory : register(u6);

#include "Include/HIKARI_SurfaceGpuScene.hlsli"
#include "Include/Cluster/HIKARI_ClusterGpuData.hlsli"
#include "Include/HIKARI_RenderDescriptorLayout.hlsli"

static const uint HIKARI_CLUSTER_DRAW_BUCKET_BACK_FACE = 0u;
static const uint HIKARI_CLUSTER_DRAW_BUCKET_DOUBLE_SIDED = 1u;
static const uint HIKARI_CLUSTER_DRAW_BUCKET_COUNT = 2u;
static const uint HIKARI_CLUSTER_CULL_PASS_FORWARD_OPAQUE = 0u;
static const uint HIKARI_CLUSTER_CULL_PASS_FORWARD_DEPTH_AWARE = 1u;
static const uint HIKARI_CLUSTER_CULL_PASS_FORWARD_TRANSPARENT = 2u;
static const uint HIKARI_CLUSTER_CULL_PASS_SHADOW = 3u;
static const uint HIKARI_CLUSTER_CULL_PASS_DEPTH_PREPASS = 4u;
static const uint HIKARI_CLUSTER_CULL_PASS_COUNT = 5u;

static const uint HIKARI_CLUSTER_CULL_COUNTER_INPUT_COUNT = 0u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_VISIBLE_RANGE_COUNT = 4u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_VISIBLE_CLUSTER_COUNT = 8u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_OVERFLOW_COUNT = 12u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_BACK_FACE_DRAW_COUNT = 16u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_BACK_FACE_DRAW_OVERFLOW_COUNT = 24u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_DOUBLE_SIDED_DRAW_OVERFLOW_COUNT = 28u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_INPUT_FRUSTUM_CULLED_COUNT = 32u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_PAGE_TESTED_COUNT = 36u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_PAGE_FRUSTUM_CULLED_COUNT = 40u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_PAGE_TASK_COUNT = 64u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_PAGE_TASK_OVERFLOW_COUNT = 68u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_LOD0_SELECTED_COUNT = 80u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_LOD1_SELECTED_COUNT = 84u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_LOD2_SELECTED_COUNT = 88u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_LOD3_PLUS_SELECTED_COUNT = 92u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_PAGE_OCCLUSION_TESTED_COUNT = 96u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_PAGE_OCCLUSION_CULLED_COUNT = 100u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_HZB_PASS_REJECTED_COUNT = 112u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_HZB_AABB_REJECTED_COUNT = 116u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_HZB_QUERY_ACCEPTED_COUNT = 124u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_HZB_TRY_COUNT = 128u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_HZB_ALLOWED_COUNT = 132u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_HZB_INVALID_REJECTED_COUNT = 136u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_HZB_NEAR_PLANE_REJECTED_COUNT = 140u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_HZB_OFFSCREEN_REJECTED_COUNT = 144u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_HZB_LARGE_RECT_COUNT = 148u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_HZB_AABB_ACCEPTED_COUNT = 152u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_HZB_TEMPORAL_PENDING_COUNT = 160u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_HZB_TEMPORAL_CONFIRMED_COUNT = 164u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_HZB_TEMPORAL_RESET_COUNT = 168u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_HZB_TEMPORAL_COLLISION_COUNT = 172u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_HZB_RAW_OCCLUDED_COUNT = 176u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_HZB_LARGE_RECT_SKIPPED_COUNT = 180u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_PAGE_HZB_SMALL_SCREEN_SKIPPED_COUNT = 184u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_HZB_BUDGET_SKIPPED_COUNT = 204u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_PASS_BASE = 224u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_PASS_STRIDE = 16u;
static const uint HIKARI_CLUSTER_CULL_TEMPORAL_CONFIRM_MAX_FRAME_GAP = 4u;
static const uint HIKARI_CLUSTER_CULL_TEMPORAL_VISIBLE_RESET_FRAMES = 2u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_PASS_BACK_FACE_DRAW_COUNT = 0u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_PASS_DOUBLE_SIDED_DRAW_COUNT = 4u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_PASS_BACK_FACE_DRAW_OVERFLOW_COUNT = 8u;
static const uint HIKARI_CLUSTER_CULL_COUNTER_PASS_DOUBLE_SIDED_DRAW_OVERFLOW_COUNT = 12u;

// CullPageTasksCS は 1 lane = 1 page task。64 tasks/group が X 次元の
// 上限 (D3D12: 65535) を超えたら Y 次元に折り返す。Finalize CS と CS 本体で
// 同じ幅を共有する。
static const uint HIKARI_CLUSTER_CULL_PAGE_TASK_DISPATCH_MAX_X = 65535u;
static const uint HIKARI_CLUSTER_CULL_FINE_GROUP_SIZE = 64u;
static const uint HIKARI_CLUSTER_CULL_VISIBLE_RANGE_FLAG_AS_FINE_CULL = 8u;
static const uint HIKARI_CLUSTER_CULL_HZB_QUERY_OK = 0u;
static const uint HIKARI_CLUSTER_CULL_HZB_QUERY_REJECT_INVALID = 1u;
static const uint HIKARI_CLUSTER_CULL_HZB_QUERY_REJECT_NEAR_PLANE = 2u;
static const uint HIKARI_CLUSTER_CULL_HZB_QUERY_REJECT_OFFSCREEN = 3u;
static const uint HIKARI_CLUSTER_CULL_HZB_QUERY_FLAG_LARGE_RECT = 1u;

ByteAddressBuffer gClusterGeometryPool[
    HIKARI_RENDER_CLUSTER_GEOMETRY_SRV_COUNT] : register(t0, space1);
Texture2D<float> gClusterCullHzb : register(t18);

#endif
