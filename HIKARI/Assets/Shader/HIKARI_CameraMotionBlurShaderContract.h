#pragma once

#include <string>
#include <string_view>

#include "Assets/Shader/HIKARI_BuiltinShaderArtifactResolver.h"
#include "Assets/Shader/HIKARI_ShaderContract.h"
#include "Core/Hashing/HIKARI_Sha256.h"

namespace HIKARI::ASSETS::SHADER {

inline constexpr std::string_view kCameraMotionBlurShaderProgram = "HIKARI.EngineShader.Render3D_CameraMotionBlur";
inline constexpr std::string_view kCameraMotionBlurShaderRootCanonical =
    "HIKARI.CameraMotionBlur.Root.v1|0:RootConstants(b0,space0,12,Compute)|"
    "1..5:SRVTable(t0..t4,count1,Compute)|6..8:UAVTable(u0..u2,count1,Compute)|"
    "StaticSampler(s0,LinearClamp,Compute)|Flags(None)";
inline constexpr std::string_view kCameraMotionBlurShaderInterfaceCanonical =
    "CameraMotionBlurCB:48;float4 sizes(output,render),blur(shutterFraction,maxLengthPixels,samples,orthographic),"
    "depthTiles(near,far,tileCountX,tileCountY);Motion:RG16F current->previous render pixels;"
    "Depth:R32F device depth;TileMax/NeighborMax:RG16F output pixels,tile 32;"
    "Output:scene-linear RGBA16F;reconstruction filter (McGuire 2012)";

inline ShaderAssetContractExpectation MakeCameraMotionBlurShaderContract(
    ShaderRuntimeDeviceCapabilities availableCaps) noexcept {
    ShaderAssetContractExpectation contract{};
    contract.engineInterfaceId = "HIKARI.CameraMotionBlur";
    contract.engineInterfaceVersion = 1u;
    contract.rootLayoutId = "HIKARI.CameraMotionBlur.Root";
    contract.rootLayoutVersion = 1u;
    contract.availableDeviceCaps = availableCaps;
    if (!HASH::ComputeSha256(kCameraMotionBlurShaderInterfaceCanonical, contract.engineInterfaceHash) ||
        !HASH::ComputeSha256(kCameraMotionBlurShaderRootCanonical, contract.rootLayoutHash)) {
        return {};
    }
    return contract;
}

inline bool IsCameraMotionBlurBindingAllowed(const ShaderResourceBinding& binding) noexcept {
    using Kind = ShaderResourceBindingKind;
    if (binding.count != 1u || binding.registerSpace != 0u) {
        return false;
    }
    switch (binding.kind) {
    case Kind::ConstantBuffer: return binding.registerIndex == 0u;
    case Kind::ShaderResource: return binding.registerIndex <= 4u;
    case Kind::UnorderedAccess: return binding.registerIndex <= 2u;
    case Kind::Sampler: return binding.registerIndex == 0u;
    }
    return false;
}

inline ShaderAssetResolveResult ResolveCameraMotionBlurShaderEntry(
    std::string entry, ShaderRuntimeDeviceCapabilities availableCaps) noexcept {
    auto resolved =
        ResolveBuiltinShaderEntry(kCameraMotionBlurShaderProgram, {std::move(entry), ShaderStage::Compute, "cs_6_0"},
                                  MakeCameraMotionBlurShaderContract(availableCaps));
    if (resolved) {
        for (const ShaderResourceBinding& binding : resolved.lease.GetEntry()->bindings) {
            if (!IsCameraMotionBlurBindingAllowed(binding)) {
                return {ShaderAssetResolveCode::ArtifactInvalid,
                        {},
                        "camera motion blur shader binding exceeds its root contract"};
            }
        }
    }
    return resolved;
}

} // namespace HIKARI::ASSETS::SHADER
