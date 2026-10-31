#pragma once

#include <array>
#include <cstdint>

#include <d3d12.h>

namespace HIKARI::GFX {

namespace ShaderDeviceFeature {
inline constexpr uint64_t WaveOperations = 1ull << 0u;
inline constexpr uint64_t MeshShader = 1ull << 1u;
inline constexpr uint64_t RayTracing = 1ull << 2u;
inline constexpr uint64_t SamplerFeedback = 1ull << 3u;
} // namespace ShaderDeviceFeature

struct ShaderDeviceCapabilities {
    uint32_t shaderModel = 0u;
    uint32_t rootSignatureVersion = 0u;
    uint64_t featureMask = 0u;
};

inline D3D_SHADER_MODEL QueryHighestShaderModel(ID3D12Device* device) noexcept {
    if (device == nullptr)
        return static_cast<D3D_SHADER_MODEL>(0);
    constexpr std::array models{
        D3D_SHADER_MODEL_6_7, D3D_SHADER_MODEL_6_6, D3D_SHADER_MODEL_6_5, D3D_SHADER_MODEL_6_4, D3D_SHADER_MODEL_6_3,
        D3D_SHADER_MODEL_6_2, D3D_SHADER_MODEL_6_1, D3D_SHADER_MODEL_6_0, D3D_SHADER_MODEL_5_1,
    };
    for (D3D_SHADER_MODEL candidate : models) {
        D3D12_FEATURE_DATA_SHADER_MODEL query{candidate};
        const HRESULT result = device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &query, sizeof(query));
        if (SUCCEEDED(result))
            return query.HighestShaderModel;
        // 古い Runtime が列挙値を知らない場合だけ下位 Model を試す。
        if (result != E_INVALIDARG)
            break;
    }
    return static_cast<D3D_SHADER_MODEL>(0);
}

inline ShaderDeviceCapabilities QueryShaderDeviceCapabilities(ID3D12Device* device) noexcept {
    ShaderDeviceCapabilities capabilities{};
    if (device == nullptr) {
        return capabilities;
    }
    const uint32_t encoded = static_cast<uint32_t>(QueryHighestShaderModel(device));
    capabilities.shaderModel = ((encoded >> 4u) & 0x0fu) * 10u + (encoded & 0x0fu);
    D3D12_FEATURE_DATA_ROOT_SIGNATURE root{D3D_ROOT_SIGNATURE_VERSION_1_1};
    if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &root, sizeof(root)))) {
        root.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
    }
    capabilities.rootSignatureVersion = root.HighestVersion >= D3D_ROOT_SIGNATURE_VERSION_1_1 ? 11u : 10u;
    D3D12_FEATURE_DATA_D3D12_OPTIONS1 options1{};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS1, &options1, sizeof(options1))) &&
        options1.WaveOps != FALSE) {
        capabilities.featureMask |= ShaderDeviceFeature::WaveOperations;
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))) &&
        options5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
        capabilities.featureMask |= ShaderDeviceFeature::RayTracing;
    }
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7{};
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7)))) {
        if (options7.MeshShaderTier != D3D12_MESH_SHADER_TIER_NOT_SUPPORTED) {
            capabilities.featureMask |= ShaderDeviceFeature::MeshShader;
        }
        if (options7.SamplerFeedbackTier != D3D12_SAMPLER_FEEDBACK_TIER_NOT_SUPPORTED) {
            capabilities.featureMask |= ShaderDeviceFeature::SamplerFeedback;
        }
    }
    return capabilities;
}

} // namespace HIKARI::GFX
