#pragma once

#include <cstdint>

namespace HIKARI {

    enum class RenderDebugView : uint32_t {
        None = 0,
        Normal = 1,
        Tangent = 2,
        LightingOnly = 3,
        BaseColor = 4,
        Roughness = 5,
        Metallic = 6,
        Occlusion = 7,
        Shadow = 8,
        NdotL = 9,
        Emissive = 10,
        SceneDepth = 11,
        SceneColor = 12,
        MotionVectors = 13,
        TemporalHistoryWeight = 14,
        TemporalDepthRejection = 15,
        TemporalReactiveMask = 16,
        TemporalTransparencyMask = 17,
        TemporalDisocclusion = 18,
        TemporalInvalidDepthMotion = 19,
        VolumetricScattering = 20,
        VolumetricTransmittance = 21,
        VolumetricDepthSlice = 22,

        MeshletId = 32,
        ClusterId = 33,
        SurfaceId = 34,
        LodLevel = 35,
        LodHeat = 36,
        DrawBucket = 37,
    };

    inline const char* ToString(RenderDebugView view) {
        switch (view) {
        case RenderDebugView::Normal: return "Normal";
        case RenderDebugView::Tangent: return "Tangent";
        case RenderDebugView::LightingOnly: return "Lighting Only";
        case RenderDebugView::BaseColor: return "Base Color";
        case RenderDebugView::Roughness: return "Roughness";
        case RenderDebugView::Metallic: return "Metallic";
        case RenderDebugView::Occlusion: return "Occlusion";
        case RenderDebugView::Shadow: return "Shadow";
        case RenderDebugView::NdotL: return "NdotL";
        case RenderDebugView::Emissive: return "Emissive";
        case RenderDebugView::SceneDepth: return "Scene Depth";
        case RenderDebugView::SceneColor: return "Scene Color";
        case RenderDebugView::MotionVectors: return "Motion Vectors";
        case RenderDebugView::TemporalHistoryWeight: return "TAA History Weight";
        case RenderDebugView::TemporalDepthRejection: return "TAA Depth Rejection";
        case RenderDebugView::TemporalReactiveMask: return "TAA Reactive Mask";
        case RenderDebugView::TemporalTransparencyMask: return "TAA Transparency Mask";
        case RenderDebugView::TemporalDisocclusion: return "TAA Disocclusion";
        case RenderDebugView::TemporalInvalidDepthMotion: return "Invalid Depth / Motion";
        case RenderDebugView::VolumetricScattering: return "Volumetric Scattering";
        case RenderDebugView::VolumetricTransmittance: return "Volumetric Transmittance";
        case RenderDebugView::VolumetricDepthSlice: return "Volumetric Depth Slice";
        case RenderDebugView::MeshletId: return "Meshlet ID";
        case RenderDebugView::ClusterId: return "Cluster ID";
        case RenderDebugView::SurfaceId: return "Surface ID";
        case RenderDebugView::LodLevel: return "LOD Level";
        case RenderDebugView::LodHeat: return "LOD Heat";
        case RenderDebugView::DrawBucket: return "Draw Bucket";
        case RenderDebugView::None:
        default: return "Lit";
        }
    }

    inline bool IsGeometryRenderDebugView(RenderDebugView view) {
        return view == RenderDebugView::MeshletId ||
            view == RenderDebugView::ClusterId ||
            view == RenderDebugView::SurfaceId ||
            view == RenderDebugView::LodLevel ||
            view == RenderDebugView::LodHeat ||
            view == RenderDebugView::DrawBucket;
    }

    inline bool IsScreenSpaceRenderDebugView(RenderDebugView view) {
        return view == RenderDebugView::SceneColor ||
            view == RenderDebugView::MotionVectors ||
            view == RenderDebugView::TemporalHistoryWeight ||
            view == RenderDebugView::TemporalDepthRejection ||
            view == RenderDebugView::TemporalReactiveMask ||
            view == RenderDebugView::TemporalTransparencyMask ||
            view == RenderDebugView::TemporalDisocclusion ||
            view == RenderDebugView::TemporalInvalidDepthMotion ||
            view == RenderDebugView::VolumetricScattering ||
            view == RenderDebugView::VolumetricTransmittance ||
            view == RenderDebugView::VolumetricDepthSlice;
    }

    inline bool IsTemporalRenderDebugView(RenderDebugView view) {
        return view == RenderDebugView::MotionVectors ||
            view == RenderDebugView::TemporalHistoryWeight ||
            view == RenderDebugView::TemporalDepthRejection ||
            view == RenderDebugView::TemporalReactiveMask ||
            view == RenderDebugView::TemporalTransparencyMask ||
            view == RenderDebugView::TemporalDisocclusion ||
            view == RenderDebugView::TemporalInvalidDepthMotion;
    }

    inline bool IsVolumetricRenderDebugView(RenderDebugView view) {
        return view == RenderDebugView::VolumetricScattering ||
            view == RenderDebugView::VolumetricTransmittance ||
            view == RenderDebugView::VolumetricDepthSlice;
    }

} // namespace HIKARI
