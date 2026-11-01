#pragma once

#include <string>
#include <string_view>

#include "RenderContracts/Shader/HIKARI_BuiltinShaderArtifactResolver.h"
#include "RenderContracts/Shader/HIKARI_ShaderContract.h"
#include "Core/Hashing/HIKARI_Sha256.h"

namespace HIKARI::ASSETS::SHADER {

inline constexpr std::string_view kCameraDepthOfFieldShaderProgram = "HIKARI.EngineShader.Render3D_CameraDepthOfField";
inline constexpr std::string_view kCameraDepthOfFieldShaderRootCanonical =
    "HIKARI.CameraDepthOfField.Root.v1|0:CBV(b0,space0,Compute)|"
    "1..9:SRVTable(t0..t8,count1,Compute)|10..14:UAVTable(u0..u4,count1,Compute)|"
    "StaticSampler(s0,LinearClamp,Compute)|Flags(None)";
inline constexpr std::string_view kCameraDepthOfFieldShaderInterfaceCanonical =
    "CameraDepthOfFieldCB:160;mat4 jitteredInverseViewProjection;float4 cameraPosition,cameraForward;"
    "float4 focusBand,blurRadiiAndRenderSize,fogDistancesAndVolumeSize,frameAndQuality;"
    "SpriteLayers:premultipliedRGBA16F(near,far,sharp),RG32F(weightedViewDepthAndCoC)(near,far),"
    "R32F(alphaWeightedViewDepth)(sharp);Volume:RGBA16F(scattering,transmittance);"
    "Output:premultiplied scene-linear RGBA16F;fixed bounded gathers;half-resolution sprite blur;"
    "frameAndQuality.z:0=noLayers,1=sharpOnly,2=blurLayers";

inline ShaderAssetContractExpectation MakeCameraDepthOfFieldShaderContract(
    ShaderRuntimeDeviceCapabilities availableCaps) noexcept {
    ShaderAssetContractExpectation contract{};
    contract.engineInterfaceId = "HIKARI.CameraDepthOfField";
    contract.engineInterfaceVersion = 2u;
    contract.rootLayoutId = "HIKARI.CameraDepthOfField.Root";
    contract.rootLayoutVersion = 1u;
    contract.availableDeviceCaps = availableCaps;
    if (!HASH::ComputeSha256(kCameraDepthOfFieldShaderInterfaceCanonical, contract.engineInterfaceHash) ||
        !HASH::ComputeSha256(kCameraDepthOfFieldShaderRootCanonical, contract.rootLayoutHash)) {
        return {};
    }
    return contract;
}

inline bool IsCameraDepthOfFieldBindingAllowed(const ShaderResourceBinding& binding) noexcept {
    using Kind = ShaderResourceBindingKind;
    if (binding.count != 1u || binding.registerSpace != 0u) {
        return false;
    }
    switch (binding.kind) {
    case Kind::ConstantBuffer: return binding.registerIndex == 0u;
    case Kind::ShaderResource: return binding.registerIndex <= 8u;
    case Kind::UnorderedAccess: return binding.registerIndex <= 4u;
    case Kind::Sampler: return binding.registerIndex == 0u;
    }
    return false;
}

inline ShaderAssetResolveResult ResolveCameraDepthOfFieldShaderEntry(
    std::string entry, ShaderRuntimeDeviceCapabilities availableCaps) noexcept {
    auto resolved =
        ResolveBuiltinShaderEntry(kCameraDepthOfFieldShaderProgram, {std::move(entry), ShaderStage::Compute, "cs_6_0"},
                                  MakeCameraDepthOfFieldShaderContract(availableCaps));
    if (resolved) {
        for (const ShaderResourceBinding& binding : resolved.lease.GetEntry()->bindings) {
            if (!IsCameraDepthOfFieldBindingAllowed(binding)) {
                return {ShaderAssetResolveCode::ArtifactInvalid,
                        {},
                        "camera depth-of-field shader binding exceeds its root contract"};
            }
        }
    }
    return resolved;
}

} // namespace HIKARI::ASSETS::SHADER
