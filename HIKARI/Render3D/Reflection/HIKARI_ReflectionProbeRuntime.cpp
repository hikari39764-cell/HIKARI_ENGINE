#include "Render3D/Reflection/HIKARI_ReflectionProbeRuntime.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <utility>

#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_DXCheck.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

namespace HIKARI::REFLECTION {

    namespace {
        ReflectionProbeRuntimeData gData{};

        struct ReflectionProbeStateKey {
            bool enabled = false;
            bool valid = false;
            bool hasPrefiltered = false;
            bool hasBrdfLut = false;
            RENDER3D::TextureResourceHandle prefilteredResource{};
            RENDER3D::TextureResourceHandle brdfLutResource{};
            int prefilteredHandle = -1;
            int brdfLutHandle = -1;
            uint32_t prefilteredMipCount = 1;
            uint32_t prefilteredActualMipCount = 0;
            uint32_t brdfLutMipCount = 0;
            DXGI_FORMAT prefilteredFormat = DXGI_FORMAT_UNKNOWN;
            DXGI_FORMAT brdfLutFormat = DXGI_FORMAT_UNKNOWN;
            bool prefilteredMipMismatch = false;
            std::string sourceAssetId{};
            std::string prefilteredPath{};
            std::string brdfLutPath{};
            MATH::Vec3 position{};
            float radius = 0.0f;
            float intensity = 0.0f;
            RuntimeReflectionProbeInfluenceShape influenceShape = RuntimeReflectionProbeInfluenceShape::Sphere;
            RuntimeReflectionProbeProjectionShape projectionShape = RuntimeReflectionProbeProjectionShape::Infinite;
            MATH::Vec3 influenceBoxCenter{};
            MATH::Vec3 influenceBoxSize{};
            MATH::Vec3 projectionBoxCenter{};
            MATH::Vec3 projectionBoxSize{};
            float blendDistance = 0.0f;
            int priority = 0;
        };

        bool gHasLastKey = false;
        ReflectionProbeStateKey gLastKey{};
        uint32_t gRequestedMipCount = 1;
        int gSamplingSuppressDepth = 0;

        bool operator==(const ReflectionProbeStateKey& lhs, const ReflectionProbeStateKey& rhs) {
            return lhs.enabled == rhs.enabled &&
                lhs.valid == rhs.valid &&
                lhs.hasPrefiltered == rhs.hasPrefiltered &&
                lhs.hasBrdfLut == rhs.hasBrdfLut &&
                lhs.prefilteredResource == rhs.prefilteredResource &&
                lhs.brdfLutResource == rhs.brdfLutResource &&
                lhs.prefilteredHandle == rhs.prefilteredHandle &&
                lhs.brdfLutHandle == rhs.brdfLutHandle &&
                lhs.prefilteredMipCount == rhs.prefilteredMipCount &&
                lhs.prefilteredActualMipCount == rhs.prefilteredActualMipCount &&
                lhs.brdfLutMipCount == rhs.brdfLutMipCount &&
                lhs.prefilteredFormat == rhs.prefilteredFormat &&
                lhs.brdfLutFormat == rhs.brdfLutFormat &&
                lhs.prefilteredMipMismatch == rhs.prefilteredMipMismatch &&
                lhs.sourceAssetId == rhs.sourceAssetId &&
                lhs.prefilteredPath == rhs.prefilteredPath &&
                lhs.brdfLutPath == rhs.brdfLutPath &&
                lhs.position.x == rhs.position.x &&
                lhs.position.y == rhs.position.y &&
                lhs.position.z == rhs.position.z &&
                lhs.radius == rhs.radius &&
                lhs.intensity == rhs.intensity &&
                lhs.influenceShape == rhs.influenceShape &&
                lhs.projectionShape == rhs.projectionShape &&
                lhs.influenceBoxCenter.x == rhs.influenceBoxCenter.x &&
                lhs.influenceBoxCenter.y == rhs.influenceBoxCenter.y &&
                lhs.influenceBoxCenter.z == rhs.influenceBoxCenter.z &&
                lhs.influenceBoxSize.x == rhs.influenceBoxSize.x &&
                lhs.influenceBoxSize.y == rhs.influenceBoxSize.y &&
                lhs.influenceBoxSize.z == rhs.influenceBoxSize.z &&
                lhs.projectionBoxCenter.x == rhs.projectionBoxCenter.x &&
                lhs.projectionBoxCenter.y == rhs.projectionBoxCenter.y &&
                lhs.projectionBoxCenter.z == rhs.projectionBoxCenter.z &&
                lhs.projectionBoxSize.x == rhs.projectionBoxSize.x &&
                lhs.projectionBoxSize.y == rhs.projectionBoxSize.y &&
                lhs.projectionBoxSize.z == rhs.projectionBoxSize.z &&
                lhs.blendDistance == rhs.blendDistance &&
                lhs.priority == rhs.priority;
        }

        const char* BoolText(bool value) {
            return value ? "true" : "false";
        }

        const char* ShapeText(RuntimeReflectionProbeInfluenceShape shape) {
            return shape == RuntimeReflectionProbeInfluenceShape::Box ? "Box" : "Sphere";
        }

        const char* ShapeText(RuntimeReflectionProbeProjectionShape shape) {
            return shape == RuntimeReflectionProbeProjectionShape::Box ? "Box" : "Infinite";
        }

        bool HasUsableBoxSize(const MATH::Vec3& size) {
            return size.x > 0.001f && size.y > 0.001f && size.z > 0.001f;
        }

        MATH::Vec3 SanitizeBoxSize(const MATH::Vec3& size) {
            return {
                std::max(0.0f, size.x),
                std::max(0.0f, size.y),
                std::max(0.0f, size.z)
            };
        }

        bool IsValidSrv(D3D12_GPU_DESCRIPTOR_HANDLE handle) {
            return handle.ptr != 0;
        }

        ReflectionProbeStateKey MakeStateKey() {
            ReflectionProbeStateKey key{};
            key.enabled = gData.enabled;
            key.valid = gData.valid;
            key.hasPrefiltered = gData.hasPrefiltered;
            key.hasBrdfLut = gData.hasBrdfLut;
            key.prefilteredResource = gData.prefilteredResource;
            key.brdfLutResource = gData.brdfLutResource;
            key.prefilteredHandle = gData.prefilteredHandle;
            key.brdfLutHandle = gData.brdfLutHandle;
            key.prefilteredMipCount = gData.prefilteredMipCount;
            key.prefilteredActualMipCount = gData.prefilteredActualMipCount;
            key.brdfLutMipCount = gData.brdfLutMipCount;
            key.prefilteredFormat = gData.prefilteredFormat;
            key.brdfLutFormat = gData.brdfLutFormat;
            key.prefilteredMipMismatch = gData.prefilteredMipMismatch;
            key.sourceAssetId = gData.sourceAssetId;
            key.prefilteredPath = gData.prefilteredPath;
            key.brdfLutPath = gData.brdfLutPath;
            key.position = gData.position;
            key.radius = gData.radius;
            key.intensity = gData.intensity;
            key.influenceShape = gData.influenceShape;
            key.projectionShape = gData.projectionShape;
            key.influenceBoxCenter = gData.influenceBoxCenter;
            key.influenceBoxSize = gData.influenceBoxSize;
            key.projectionBoxCenter = gData.projectionBoxCenter;
            key.projectionBoxSize = gData.projectionBoxSize;
            key.blendDistance = gData.blendDistance;
            key.priority = gData.priority;
            return key;
        }

        void RefreshResolvedHandles() {
            gData.prefilteredHandle =
                RENDER3D::GetTextureResourceBackendHandle(gData.prefilteredResource);
            gData.brdfLutHandle =
                RENDER3D::GetTextureResourceBackendHandle(gData.brdfLutResource);

            gData.prefilteredSrv =
                RENDER3D::GetTextureResourceSrvGpuHandle(gData.prefilteredResource);
            gData.brdfLutSrv =
                RENDER3D::GetTextureResourceSrvGpuHandle(gData.brdfLutResource);

            gData.prefilteredActualMipCount =
                RENDER3D::GetTextureResourceMipCount(gData.prefilteredResource);
            gData.brdfLutMipCount =
                RENDER3D::GetTextureResourceMipCount(gData.brdfLutResource);
            gData.prefilteredFormat =
                RENDER3D::GetTextureResourceFormat(gData.prefilteredResource);
            gData.brdfLutFormat =
                RENDER3D::GetTextureResourceFormat(gData.brdfLutResource);

            // Probe は prefiltered cubemap かつ mip 2 以上の時だけ有効にする。
            gData.hasPrefiltered = IsValidSrv(gData.prefilteredSrv) &&
                RENDER3D::GetTextureResourceDimension(gData.prefilteredResource) ==
                    RENDER3D::TextureResourceDimension::TextureCube &&
                gData.prefilteredActualMipCount >= 2;
            gData.hasBrdfLut = IsValidSrv(gData.brdfLutSrv) &&
                RENDER3D::GetTextureResourceDimension(gData.brdfLutResource) ==
                    RENDER3D::TextureResourceDimension::Texture2D &&
                gData.brdfLutMipCount >= 1;

            const uint32_t requestedMipCount = std::max<uint32_t>(1u, gRequestedMipCount);
            if (gData.hasPrefiltered) {
                gData.prefilteredMipCount = std::max<uint32_t>(
                    1u,
                    std::min<uint32_t>(requestedMipCount, gData.prefilteredActualMipCount));
                gData.prefilteredMipMismatch = requestedMipCount != gData.prefilteredActualMipCount;
            }
            else {
                gData.prefilteredMipCount = 1;
                gData.prefilteredMipMismatch = false;
            }

            const bool influenceBoxValid =
                gData.influenceShape != RuntimeReflectionProbeInfluenceShape::Box ||
                HasUsableBoxSize(gData.influenceBoxSize);
            const bool projectionBoxValid =
                gData.projectionShape != RuntimeReflectionProbeProjectionShape::Box ||
                HasUsableBoxSize(gData.projectionBoxSize);

            gData.valid =
                gData.enabled &&
                gData.hasPrefiltered &&
                gData.radius > 0.001f &&
                gData.intensity > 0.0f &&
                influenceBoxValid &&
                projectionBoxValid;
        }

        void LogStateIfChanged() {
            const ReflectionProbeStateKey key = MakeStateKey();
            if (gHasLastKey && key == gLastKey) {
                return;
            }

            gLastKey = key;
            gHasLastKey = true;

            std::ostringstream oss;
            oss << "[Environment][ReflectionProbe] state changed"
                << " enabled=" << BoolText(gData.enabled)
                << " valid=" << BoolText(gData.valid)
                << " prefiltered=" << BoolText(gData.hasPrefiltered)
                << " brdf=" << BoolText(gData.hasBrdfLut)
                << " handles=" << gData.prefilteredHandle << "/" << gData.brdfLutHandle
                << " mips=" << gData.prefilteredMipCount
                << " actualMips=" << gData.prefilteredActualMipCount << "/" << gData.brdfLutMipCount
                << " formats=" << GFX::FormatToString(gData.prefilteredFormat) << "/"
                << GFX::FormatToString(gData.brdfLutFormat)
                << " position=" << gData.position.x << "," << gData.position.y << "," << gData.position.z
                << " radius=" << gData.radius
                << " intensity=" << gData.intensity
                << " influenceShape=" << ShapeText(gData.influenceShape)
                << " projectionShape=" << ShapeText(gData.projectionShape)
                << " influenceBox=" << gData.influenceBoxCenter.x << "," << gData.influenceBoxCenter.y << "," << gData.influenceBoxCenter.z
                << "/" << gData.influenceBoxSize.x << "," << gData.influenceBoxSize.y << "," << gData.influenceBoxSize.z
                << " projectionBox=" << gData.projectionBoxCenter.x << "," << gData.projectionBoxCenter.y << "," << gData.projectionBoxCenter.z
                << "/" << gData.projectionBoxSize.x << "," << gData.projectionBoxSize.y << "," << gData.projectionBoxSize.z
                << " blendDistance=" << gData.blendDistance
                << " priority=" << gData.priority
                << " source=" << gData.sourceAssetId
                << " prefilteredPath=" << gData.prefilteredPath
                << " brdfPath=" << gData.brdfLutPath;
            HIKARI_LOG_INFO(oss.str());

            if (gData.enabled && !gData.hasPrefiltered) {
                HIKARI_LOG_WARN("[Environment][ReflectionProbe] prefiltered cubemap is invalid or missing.");
            }
            if (gData.prefilteredMipMismatch) {
                HIKARI_LOG_WARN(
                    "[Environment][ReflectionProbe] prefiltered mip count mismatch. requested=" +
                    std::to_string(std::max<uint32_t>(1u, gRequestedMipCount)) +
                    " actual=" + std::to_string(gData.prefilteredActualMipCount));
            }
        }
    }

    void Reset() {
        gData = {};
        gRequestedMipCount = 1;
        RefreshResolvedHandles();
        LogStateIfChanged();
    }

    void SetActiveProbeResources(
        bool enabled,
        RENDER3D::TextureResourceHandle prefilteredResource,
        RENDER3D::TextureResourceHandle brdfLutResource,
        uint32_t prefilteredMipCount,
        const MATH::Vec3& position,
        float radius,
        float intensity,
        RuntimeReflectionProbeInfluenceShape influenceShape,
        RuntimeReflectionProbeProjectionShape projectionShape,
        const MATH::Vec3& influenceBoxCenter,
        const MATH::Vec3& influenceBoxSize,
        const MATH::Vec3& projectionBoxCenter,
        const MATH::Vec3& projectionBoxSize,
        float blendDistance,
        int priority,
        std::string sourceAssetId,
        std::string prefilteredPath,
        std::string brdfLutPath) {

        gData.enabled = enabled;
        gData.prefilteredResource = prefilteredResource;
        gData.brdfLutResource = brdfLutResource;
        gRequestedMipCount = std::max<uint32_t>(1u, prefilteredMipCount);
        gData.position = position;
        gData.radius = std::max(0.0f, radius);
        gData.intensity = std::max(0.0f, intensity);
        gData.influenceShape = influenceShape;
        gData.projectionShape = projectionShape;
        gData.influenceBoxCenter = influenceBoxCenter;
        gData.influenceBoxSize = SanitizeBoxSize(influenceBoxSize);
        gData.projectionBoxCenter = projectionBoxCenter;
        gData.projectionBoxSize = SanitizeBoxSize(projectionBoxSize);
        gData.blendDistance = std::max(0.0f, blendDistance);
        gData.priority = priority;
        gData.sourceAssetId = std::move(sourceAssetId);
        gData.prefilteredPath = std::move(prefilteredPath);
        gData.brdfLutPath = std::move(brdfLutPath);

        RefreshResolvedHandles();
        LogStateIfChanged();
    }

    void SetActiveProbe(
        bool enabled,
        int prefilteredHandle,
        int brdfLutHandle,
        uint32_t prefilteredMipCount,
        const MATH::Vec3& position,
        float radius,
        float intensity,
        RuntimeReflectionProbeInfluenceShape influenceShape,
        RuntimeReflectionProbeProjectionShape projectionShape,
        const MATH::Vec3& influenceBoxCenter,
        const MATH::Vec3& influenceBoxSize,
        const MATH::Vec3& projectionBoxCenter,
        const MATH::Vec3& projectionBoxSize,
        float blendDistance,
        int priority,
        std::string sourceAssetId,
        std::string prefilteredPath,
        std::string brdfLutPath) {

        SetActiveProbeResources(
            enabled,
            RENDER3D::RegisterTextureResourceFromBackendHandle(prefilteredHandle),
            RENDER3D::RegisterTextureResourceFromBackendHandle(brdfLutHandle),
            prefilteredMipCount,
            position,
            radius,
            intensity,
            influenceShape,
            projectionShape,
            influenceBoxCenter,
            influenceBoxSize,
            projectionBoxCenter,
            projectionBoxSize,
            blendDistance,
            priority,
            std::move(sourceAssetId),
            std::move(prefilteredPath),
            std::move(brdfLutPath));
    }

    const ReflectionProbeRuntimeData& GetActiveProbe() {
        RefreshResolvedHandles();
        LogStateIfChanged();
        return gData;
    }

    bool IsReflectionProbeSamplingSuppressed() {
        return gSamplingSuppressDepth > 0;
    }

    ScopedReflectionProbeSamplingSuppress::ScopedReflectionProbeSamplingSuppress() {
        ++gSamplingSuppressDepth;
    }

    ScopedReflectionProbeSamplingSuppress::~ScopedReflectionProbeSamplingSuppress() {
        gSamplingSuppressDepth = std::max(0, gSamplingSuppressDepth - 1);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetPrefilteredSrv() {
        if (IsReflectionProbeSamplingSuppressed()) {
            return {};
        }
        RefreshResolvedHandles();
        LogStateIfChanged();
        return gData.prefilteredSrv;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetBrdfLutSrv() {
        if (IsReflectionProbeSamplingSuppressed()) {
            return {};
        }
        RefreshResolvedHandles();
        LogStateIfChanged();
        return gData.brdfLutSrv;
    }

} // namespace HIKARI::REFLECTION
