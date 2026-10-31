#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/Shader/HIKARI_ShaderContract.h"

namespace HIKARI::ASSETS::SHADER {

inline constexpr std::string_view kTemporalEngineInterfaceId = "HIKARI.Temporal";
inline constexpr std::string_view kTemporalInterfaceDependency =
    "EngineSdk/Include/Temporal/HIKARI_TemporalShaderInterface.hlsli";
inline constexpr std::string_view kTemporalEngineInterfaceHash =
    "5f47c056f3ad74984780eae3e3d710ac3f63ee978d2dce0c83a05502582e9620";

enum class TemporalShaderPass : uint8_t { Geometry, Masks, MotionVector, TaaResolve, StableDepth, ScreenComposite, Count };

struct TemporalShaderDefinition {
    std::string_view programId;
    std::string_view rootLayoutId;
    std::string_view rootCanonical;
};

inline constexpr std::array<TemporalShaderDefinition, 6> kTemporalShaders{{
    {"HIKARI.EngineShader.Temporal_GeometryMotion", "HIKARI.Temporal.Geometry.Root",
     "HIKARI.Temporal.Geometry.Root.v1|0:CBV(b0,space0,All)|1:CBV(b1,space0,All)|Pipeline(Graphics)|Flags(AllowInputAssembler)"},
    {"HIKARI.EngineShader.Temporal_Masks", "HIKARI.Temporal.Masks.Root",
     "HIKARI.Temporal.Masks.Root.v1|0:Table(SRV(t0,space0,count1),Pixel)|1:Table(SRV(t1,space0,count1),Pixel)|2:Table(SRV(t2,space0,count1),Pixel)|3:Constants(b1,space0,count60,Pixel)|StaticSampler(s0,PointClamp,Pixel)|Pipeline(Graphics)|Flags(None)"},
    {"HIKARI.EngineShader.Temporal_MotionVector", "HIKARI.Temporal.MotionVector.Root",
     "HIKARI.Temporal.MotionVector.Root.v1|0:CBV(b0,space0,All)|1:Table(SRV(t0,space0,count1),Pixel)|Pipeline(Graphics)|Flags(None)"},
    {"HIKARI.EngineShader.Temporal_TaaResolve", "HIKARI.Temporal.TaaResolve.Root",
     "HIKARI.Temporal.TaaResolve.Root.v1|0:CBV(b0,space0,All)|1:Table(SRV(t0,space0,count1),Pixel)|2:Table(SRV(t1,space0,count1),Pixel)|3:Table(SRV(t2,space0,count1),Pixel)|4:Table(SRV(t3,space0,count1),Pixel)|5:Table(SRV(t4,space0,count1),Pixel)|6:Table(SRV(t5,space0,count1),Pixel)|7:Table(SRV(t6,space0,count1),Pixel)|8:Table(SRV(t7,space0,count1),Pixel)|9:Table(SRV(t8,space0,count1),Pixel)|10:Table(SRV(t9,space0,count1),Pixel)|StaticSampler(s0,LinearClamp,Pixel)|Pipeline(Graphics)|Flags(None)"},
    {"HIKARI.EngineShader.Temporal_StableDepth", "HIKARI.Temporal.StableDepth.Root",
     "HIKARI.Temporal.StableDepth.Root.v1|0:Constants(b0,space0,count12,Pixel)|1:Table(SRV(t0,space0,count1),Pixel)|2:Table(SRV(t1,space0,count1),Pixel)|3:Table(SRV(t2,space0,count1),Pixel)|Pipeline(Graphics)|Flags(None)"},
    {"HIKARI.EngineShader.Temporal_ScreenComposite", "HIKARI.Temporal.ScreenComposite.Root",
     "HIKARI.Temporal.ScreenComposite.Root.v1|0:CBV(b0,space0,All)|1:Table(SRV(t0,space0,count1),Pixel)|2:Table(SRV(t1,space0,count1),Pixel)|3:Table(SRV(t2,space0,count1),Pixel)|4:Table(SRV(t3,space0,count1),Pixel)|5:Table(SRV(t4,space0,count1),Pixel)|6:Table(SRV(t5,space0,count1),Pixel)|7:Table(SRV(t6,space0,count1),Pixel)|8:Table(SRV(t7,space0,count1),Pixel)|9:Table(SRV(t8,space0,count1),Pixel)|StaticSampler(s0,LinearClamp,Pixel)|Pipeline(Graphics)|Flags(None)"},
}};

inline ShaderAssetContractExpectation MakeTemporalShaderContract(TemporalShaderPass pass,
                                                                 ShaderRuntimeDeviceCapabilities capabilities) {
    const auto index = static_cast<size_t>(pass);
    if (index >= kTemporalShaders.size()) {
        return {};
    }
    const TemporalShaderDefinition& definition = kTemporalShaders[index];
    return MakeShaderContract({kTemporalEngineInterfaceId,
                               1u,
                               kTemporalEngineInterfaceHash,
                               definition.rootLayoutId,
                               1u,
                               {},
                               definition.rootCanonical},
                              capabilities);
}

inline bool ValidateTemporalShaderArtifact(const ShaderCookedArtifact& artifact, std::string& diagnostic) {
    size_t index = 0u;
    while (index < kTemporalShaders.size() && kTemporalShaders[index].programId != artifact.programId) {
        ++index;
    }
    if (index == kTemporalShaders.size()) {
        diagnostic = "Unknown Temporal shader program";
        return false;
    }
    const auto pass = static_cast<TemporalShaderPass>(index);
    const auto expected = MakeTemporalShaderContract(pass, {60u, 10u, 0u});
    if (!MatchesShaderContract(artifact, expected) ||
        artifact.entries.size() != (pass == TemporalShaderPass::Geometry ? 3u : 2u)) {
        diagnostic = "Temporal shader identity or root contract mismatch";
        return false;
    }
    if (!DependsOnShaderInterface(artifact, kTemporalInterfaceDependency, expected.engineInterfaceHash)) {
        diagnostic = "Temporal shader interface dependency mismatch";
        return false;
    }
    uint32_t entryMask = 0u;
    for (const auto& entry : artifact.entries) {
        const bool pixel = entry.entry.id == "PSMain";
        const bool skinned = pass == TemporalShaderPass::Geometry && entry.entry.id == "VSSkinned";
        const bool vertex = entry.entry.id == (pass == TemporalShaderPass::Geometry ? "VSStatic" : "VSMain");
        const uint32_t bit = pixel ? 1u : (skinned ? 2u : 4u);
        if ((!pixel && !skinned && !vertex) || (entryMask & bit) != 0u ||
            !MatchesEntryPoint(entry, entry.entry.id, entry.entry.id, pixel ? ShaderStage::Pixel : ShaderStage::Vertex,
                               pixel ? "ps_6_0" : "vs_6_0")) {
            diagnostic = "Temporal shader entry mismatch";
            return false;
        }
        entryMask |= bit;
        std::vector<ShaderBindingExpectation> bindings{};
        const auto add = [&](ShaderResourceBindingKind kind, uint32_t reg) {
            bindings.push_back({kind, reg, ShaderStageMask(entry.entry.stage)});
        };
        if (pass == TemporalShaderPass::Geometry) {
            add(ShaderResourceBindingKind::ConstantBuffer, 0u);
            if (skinned) {
                add(ShaderResourceBindingKind::ConstantBuffer, 1u);
            }
        } else if (pixel) {
            // 遮罩は b1 に場景キャプチャを映す面の四隅、t2 に面の手前を確かめる深度を持つ。
            add(ShaderResourceBindingKind::ConstantBuffer, pass == TemporalShaderPass::Masks ? 1u : 0u);
            // 安定深度は t0 深度、t1 運動、t2 前の結果。画面の貼り直しは t0 深度と t1..t8 キャプチャ、s0 で引く。
            const uint32_t count = pass == TemporalShaderPass::TaaResolve
                                       ? 10u
                                       : (pass == TemporalShaderPass::ScreenComposite
                                              ? 9u
                                              : (pass == TemporalShaderPass::Masks ||
                                                         pass == TemporalShaderPass::StableDepth
                                                     ? 3u
                                                     : 1u));
            for (uint32_t reg = 0u; reg < count; ++reg) {
                add(ShaderResourceBindingKind::ShaderResource, reg);
            }
            if (pass == TemporalShaderPass::ScreenComposite) {
                add(ShaderResourceBindingKind::Sampler, 0u);
            }
            // TAA は Load による補間を使う。ルートに残る s0 は DXIL に現れない。
        }
        if (!HasExactBindings(entry, bindings)) {
            diagnostic = "Temporal shader reflection binding mismatch";
            return false;
        }
    }
    diagnostic.clear();
    return true;
}

} // namespace HIKARI::ASSETS::SHADER
