#pragma once

#include <array>
#include <cstdint>

#include <d3d12.h>

namespace HIKARI::GFX {

    struct ShaderDeviceCapabilities {
        uint32_t shaderModel = 0u;
        uint32_t rootSignatureVersion = 0u;
        uint64_t featureMask = 0u;
    };

    inline ShaderDeviceCapabilities QueryShaderDeviceCapabilities(
        ID3D12Device* device) noexcept {
        ShaderDeviceCapabilities capabilities{};
        if (device == nullptr) {
            return capabilities;
        }
        constexpr std::array models{
            D3D_SHADER_MODEL_6_7,
            D3D_SHADER_MODEL_6_6,
            D3D_SHADER_MODEL_6_5,
            D3D_SHADER_MODEL_6_0,
        };
        for (D3D_SHADER_MODEL candidate : models) {
            D3D12_FEATURE_DATA_SHADER_MODEL query{ candidate };
            if (SUCCEEDED(device->CheckFeatureSupport(
                    D3D12_FEATURE_SHADER_MODEL,
                    &query,
                    sizeof(query)))) {
                const uint32_t encoded =
                    static_cast<uint32_t>(query.HighestShaderModel);
                capabilities.shaderModel =
                    ((encoded >> 4u) & 0x0fu) * 10u +
                    (encoded & 0x0fu);
                break;
            }
        }
        D3D12_FEATURE_DATA_ROOT_SIGNATURE root{
            D3D_ROOT_SIGNATURE_VERSION_1_1
        };
        if (FAILED(device->CheckFeatureSupport(
                D3D12_FEATURE_ROOT_SIGNATURE,
                &root,
                sizeof(root)))) {
            root.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
        }
        capabilities.rootSignatureVersion =
            root.HighestVersion >= D3D_ROOT_SIGNATURE_VERSION_1_1
                ? 11u
                : 10u;
        return capabilities;
    }

} // namespace HIKARI::GFX
