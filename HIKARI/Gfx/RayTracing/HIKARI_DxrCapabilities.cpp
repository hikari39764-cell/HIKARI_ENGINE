#include "Gfx/RayTracing/HIKARI_DxrCapabilities.h"
#include "Gfx/HIKARI_ShaderDeviceCapabilities.h"

#include <d3d12.h>
#include <wrl/client.h>

namespace HIKARI::GFX::RAYTRACING {

    DxrCapabilities QueryDxrCapabilities(
        ID3D12Device* device) noexcept {

        DxrCapabilities capabilities{};
        if (device == nullptr) {
            return capabilities;
        }

        Microsoft::WRL::ComPtr<ID3D12Device5> device5{};
        capabilities.deviceInterfaceAvailable =
            SUCCEEDED(device->QueryInterface(
                IID_PPV_ARGS(device5.GetAddressOf())));

        D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
        if (SUCCEEDED(device->CheckFeatureSupport(
                D3D12_FEATURE_D3D12_OPTIONS5,
                &options5,
                sizeof(options5)))) {
            if (options5.RaytracingTier >=
                D3D12_RAYTRACING_TIER_1_1) {
                capabilities.tier = DxrSupportTier::Tier11;
                capabilities.inlineRayQueryAvailable = true;
            } else if (options5.RaytracingTier >=
                D3D12_RAYTRACING_TIER_1_0) {
                capabilities.tier = DxrSupportTier::Tier10;
            }
        }

        capabilities.shaderModel =
            static_cast<uint32_t>(QueryHighestShaderModel(device));
        return capabilities;
    }

    const char* ToString(DxrSupportTier tier) noexcept {
        switch (tier) {
        case DxrSupportTier::Tier10:
            return "DXR 1.0";
        case DxrSupportTier::Tier11:
            return "DXR 1.1";
        case DxrSupportTier::Unsupported:
        default:
            return "Unsupported";
        }
    }

} // namespace HIKARI::GFX::RAYTRACING
