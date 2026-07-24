#pragma once

#include <d3d12.h>

namespace HIKARI::SHADOW::PIPELINE {

    inline constexpr UINT kShadowStaticRootParamCamera = 0;
    inline constexpr UINT kShadowStaticRootParamObject = 1;
    inline constexpr UINT kShadowStaticRootParamBaseColorTexture = 2;
    inline constexpr UINT kShadowStaticRootParamMaterialData = 3;
    inline constexpr UINT kShadowStaticRootParamSurfaceGpuScene = 4;
    inline constexpr UINT kShadowStaticRootParamSurfaceGpuSceneControl = 5;
    inline constexpr UINT kShadowStaticRootParamTexturePool = 6;
    inline constexpr UINT kShadowStaticRootParamMaterialIndex = 7;
    inline constexpr UINT kShadowStaticRootParamObjectData = 8;
    inline constexpr UINT kShadowStaticRootParamClusterGeometryPool = 9;
    inline constexpr UINT kShadowStaticRootParamMeshletVisibleRanges = 10;
    inline constexpr UINT kShadowStaticRootParamMeshletVisibleClusterList = 11;
    inline constexpr UINT kShadowStaticRootParamCullingCamera = 12;
    inline constexpr UINT kShadowStaticRootParamDeformationPalettes = 13;
    inline constexpr UINT kShadowSkinnedRootParamJointPalette = 14;

} // namespace HIKARI::SHADOW::PIPELINE
