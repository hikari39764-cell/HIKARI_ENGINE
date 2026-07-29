#include "Render3D/Core/MeshRenderer/Bindings/HIKARI_MeshResourceBindings.h"


#include "Render3D/Lighting/HIKARI_IblEnvironment.h"
#include "Render3D/Lighting/HIKARI_LightProbeVolumeRuntime.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Reflection/HIKARI_ReflectionProbeRuntime.h"
#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorAccess.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::MESHRENDERER {
    D3D12_GPU_DESCRIPTOR_HANDLE ResolveSkyCubeSrv(int fallbackTextureHandle) {
        const SKYRENDERER::SkyEnvironmentData& skyData = SKYRENDERER::GetEnvironmentData();
        if (skyData.valid && skyData.hasCubemap && skyData.cubemapSrv.ptr != 0) {
            return skyData.cubemapSrv;
        }

        return RENDER3D::GetTextureResourceSrvGpuHandleFromBackendHandle(fallbackTextureHandle);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE ResolveSceneDepthSrv(
        bool depthAwarePhase,
        const RENDER3D::RenderTextureView& sceneDepth,
        int fallbackTextureHandle) {
        if (depthAwarePhase && sceneDepth.IsSampleable()) {
            return sceneDepth.srv;
        }

        return RENDER3D::GetTextureResourceSrvGpuHandleFromBackendHandle(fallbackTextureHandle);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE ResolveSceneColorSrv(
        const RENDER3D::RenderTextureView& sceneColor,
        int fallbackTextureHandle) {
        if (sceneColor.IsSampleable()) {
            return sceneColor.srv;
        }

        return RENDER3D::GetTextureResourceSrvGpuHandleFromBackendHandle(fallbackTextureHandle);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE ResolveIblIrradianceSrv(int fallbackTextureHandle) {
        const D3D12_GPU_DESCRIPTOR_HANDLE srv = IBL::GetIrradianceSrv();
        if (srv.ptr != 0) {
            return srv;
        }

        return RENDER3D::GetTextureResourceSrvGpuHandleFromBackendHandle(fallbackTextureHandle);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE ResolveIblPrefilteredSrv(int fallbackTextureHandle) {
        const D3D12_GPU_DESCRIPTOR_HANDLE srv = IBL::GetPrefilteredSrv();
        if (srv.ptr != 0) {
            return srv;
        }

        // Sky cubemap を prefiltered IBL の代替として扱わない。
        return RENDER3D::GetTextureResourceSrvGpuHandleFromBackendHandle(fallbackTextureHandle);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE ResolveIblBrdfLutSrv(int fallbackTextureHandle) {
        const D3D12_GPU_DESCRIPTOR_HANDLE srv = IBL::GetBrdfLutSrv();
        if (srv.ptr != 0) {
            return srv;
        }

        if (!REFLECTION::IsReflectionProbeSamplingSuppressed()) {
            const D3D12_GPU_DESCRIPTOR_HANDLE probeSrv =
                REFLECTION::GetSharedBrdfLutSrv();
            if (probeSrv.ptr != 0) {
                return probeSrv;
            }
        }

        return RENDER3D::GetTextureResourceSrvGpuHandleFromBackendHandle(fallbackTextureHandle);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE ResolveReflectionProbePrefilteredSrv(
        size_t slot,
        int fallbackCubeTextureHandle) {
        if (REFLECTION::IsReflectionProbeSamplingSuppressed()) {
            return RENDER3D::GetTextureResourceSrvGpuHandleFromBackendHandle(fallbackCubeTextureHandle);
        }

        const D3D12_GPU_DESCRIPTOR_HANDLE probeSrv =
            REFLECTION::GetViewPrefilteredSrv(slot);
        if (probeSrv.ptr != 0) {
            return probeSrv;
        }

        return RENDER3D::GetTextureResourceSrvGpuHandleFromBackendHandle(fallbackCubeTextureHandle);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE ResolveSsaoSrv(
        const RENDER3D::RenderTextureView& ambientOcclusion,
        int fallbackAoTextureHandle) {
        if (ambientOcclusion.IsSampleable()) {
            return ambientOcclusion.srv;
        }

        return RENDER3D::GetTextureResourceSrvGpuHandleFromBackendHandle(fallbackAoTextureHandle);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE ResolveLightProbeShSrv(
        size_t slot) {
        return RENDER3D::LIGHTPROBE::
            GetViewShVolumeSrvTable(slot);
    }

} // namespace HIKARI::MESHRENDERER
