#pragma once

// Canonical contract shared by RenderGraph planning and SceneSurface storage.

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace HIKARI::RENDER3D::GRAPH {

enum class RenderGraphSemantic : uint16_t {
    None = 0u,
    SceneDepth,
    SceneColorOpaque,
    SceneColorPostOpaque,
    SceneColorFinal,
    PackedSurface,
    SurfaceFeatureMask,
    MotionVectors,
    MotionMetadata,
    CurrentDepthPyramid,
    VisibilityDepthPyramid,
    HistoryColor,
    HistoryDepth,
    StylizedOutlineStyle,
    AmbientOcclusion,
    ReactiveMask,
    TransparencyMask,
    InvalidDepthMotionMask,
    ProjectDispatchOutput,
    ReflectionFallbackSpecular,
    ReflectionSpecularResponse,
    SubsurfaceDiffuse,
    Count,
};

enum class RenderGraphSemanticPhase : uint8_t {
    None = 0u,
    Imported,
    PreOpaque,
    Opaque,
    PostOpaque,
    Temporal,
    Final,
    HistoryRead,
    HistoryWrite,
    Count,
};

struct RenderGraphSurfaceKey {
    RenderGraphSemantic semantic = RenderGraphSemantic::None;
    RenderGraphSemanticPhase phase = RenderGraphSemanticPhase::None;

    constexpr bool IsValid() const noexcept {
        return semantic > RenderGraphSemantic::None && semantic < RenderGraphSemantic::Count &&
               phase > RenderGraphSemanticPhase::None && phase < RenderGraphSemanticPhase::Count;
    }

    constexpr bool Matches(RenderGraphSurfaceKey expected) const noexcept {
        return IsValid() && expected.IsValid() && semantic == expected.semantic && phase == expected.phase;
    }

    bool operator==(const RenderGraphSurfaceKey&) const noexcept = default;
};

static_assert(std::is_standard_layout_v<RenderGraphSurfaceKey>);
static_assert(std::is_trivially_copyable_v<RenderGraphSurfaceKey>);
static_assert(sizeof(RenderGraphSurfaceKey) == 4u);

constexpr uint32_t RenderGraphSemanticPhaseBit(RenderGraphSemanticPhase phase) noexcept {
    return phase > RenderGraphSemanticPhase::None && phase < RenderGraphSemanticPhase::Count
               ? 1u << static_cast<uint8_t>(phase)
               : 0u;
}

} // namespace HIKARI::RENDER3D::GRAPH

namespace HIKARI::RENDER3D::SURFACE {

enum class SceneSurfaceProducer : uint16_t {
    None = 0u,
    PostSceneDepth,
    ForwardOpaque,
    SceneColorCapture,
    SceneSurfaceAux,
    Ssao,
    DepthPyramid,
    TemporalMotion,
    TemporalMask,
    TemporalHistory,
    FeatureComposite,
    EditorInteractive,
    PostFinal,
    ProjectDispatch,
    Count,
};

enum class SceneSurfaceAvailability : uint8_t {
    Unavailable = 0u,
    Imported,
    Produced,
    Alias,
};

enum class SceneSurfaceResourceKind : uint8_t {
    Texture2D = 0u,
    DepthPyramid,
};

enum class SceneSurfaceFormat : uint16_t {
    Unknown = 0u,
    R8Unorm,
    R8G8B8A8Unorm,
    R16G16Float,
    R16G16B16A16Float,
    R32Float,
    R32G32Uint,
    R32G32Float,
};

enum class SceneSurfaceLifetime : uint8_t {
    FrameLocal = 0u,
    History,
    FrameOrHistory,
};

enum class SceneSurfaceFallbackPolicy : uint8_t {
    RequiredFail = 0u,
    SkipProvider,
    NeutralBlack,
    NeutralWhite,
    NoHistory,
};

enum class SceneSurfaceAccess : uint32_t {
    None = 0u,
    Sampled = 1u << 0u,
    ReadDepth = 1u << 1u,
    WriteColor = 1u << 2u,
    WriteDepth = 1u << 3u,
    ReadUav = 1u << 4u,
    WriteUav = 1u << 5u,
    CopySource = 1u << 6u,
    CopyDestination = 1u << 7u,
};

constexpr uint32_t SceneSurfaceAccessBit(SceneSurfaceAccess access) noexcept { return static_cast<uint32_t>(access); }

constexpr uint64_t SceneSurfaceProducerBit(SceneSurfaceProducer producer) noexcept {
    return producer > SceneSurfaceProducer::None && producer < SceneSurfaceProducer::Count
               ? 1ull << static_cast<uint16_t>(producer)
               : 0u;
}

struct SceneSurfaceSemanticDesc {
    GRAPH::RenderGraphSemantic semantic = GRAPH::RenderGraphSemantic::None;
    SceneSurfaceResourceKind resourceKind = SceneSurfaceResourceKind::Texture2D;
    SceneSurfaceFormat format = SceneSurfaceFormat::Unknown;
    SceneSurfaceLifetime lifetime = SceneSurfaceLifetime::FrameLocal;
    SceneSurfaceFallbackPolicy fallback = SceneSurfaceFallbackPolicy::SkipProvider;
    uint32_t allowedPhaseMask = 0u;
    uint32_t allowedAccessMask = 0u;
    uint64_t allowedProducerMask = 0u;
};

constexpr bool SceneSurfaceProducerAllowed(const SceneSurfaceSemanticDesc& contract,
                                           SceneSurfaceProducer producer) noexcept {
    const uint64_t producerBit = SceneSurfaceProducerBit(producer);
    return producerBit != 0u && (contract.allowedProducerMask & producerBit) != 0u;
}

namespace DETAIL {
constexpr uint32_t AllowedSurfacePhases(GRAPH::RenderGraphSemantic semantic) noexcept {
    using Semantic = GRAPH::RenderGraphSemantic;
    using Phase = GRAPH::RenderGraphSemanticPhase;
    const auto bit = [](Phase phase) constexpr { return GRAPH::RenderGraphSemanticPhaseBit(phase); };
    switch (semantic) {
    case Semantic::SceneDepth: return bit(Phase::Imported);
    case Semantic::ReflectionFallbackSpecular:
    case Semantic::ReflectionSpecularResponse:
    case Semantic::SubsurfaceDiffuse:
    case Semantic::SceneColorOpaque: return bit(Phase::Opaque);
    case Semantic::SceneColorPostOpaque: return bit(Phase::PostOpaque);
    case Semantic::SceneColorFinal: return bit(Phase::Final);
    case Semantic::PackedSurface:
    case Semantic::AmbientOcclusion:
    case Semantic::VisibilityDepthPyramid: return bit(Phase::PreOpaque) | bit(Phase::PostOpaque);
    case Semantic::SurfaceFeatureMask:
    case Semantic::CurrentDepthPyramid:
    case Semantic::StylizedOutlineStyle:
    case Semantic::ProjectDispatchOutput: return bit(Phase::PostOpaque);
    case Semantic::MotionVectors:
    case Semantic::MotionMetadata:
    case Semantic::ReactiveMask:
    case Semantic::TransparencyMask:
    case Semantic::InvalidDepthMotionMask: return bit(Phase::Temporal);
    case Semantic::HistoryColor:
    case Semantic::HistoryDepth: return bit(Phase::HistoryRead) | bit(Phase::HistoryWrite);
    case Semantic::None:
    case Semantic::Count: return 0u;
    }
    return 0u;
}

constexpr uint64_t AllowedSurfaceProducers(GRAPH::RenderGraphSemantic semantic) noexcept {
    using Semantic = GRAPH::RenderGraphSemantic;
    using Producer = SceneSurfaceProducer;
    switch (semantic) {
    case Semantic::SceneDepth:
        return SceneSurfaceProducerBit(Producer::PostSceneDepth) | SceneSurfaceProducerBit(Producer::EditorInteractive);
    case Semantic::ReflectionFallbackSpecular:
    case Semantic::ReflectionSpecularResponse:
    case Semantic::SubsurfaceDiffuse: return SceneSurfaceProducerBit(Producer::ForwardOpaque);
    case Semantic::SceneColorOpaque:
        return SceneSurfaceProducerBit(Producer::ForwardOpaque) | SceneSurfaceProducerBit(Producer::SceneColorCapture) |
               SceneSurfaceProducerBit(Producer::EditorInteractive);
    case Semantic::SceneColorPostOpaque:
        return SceneSurfaceProducerBit(Producer::SceneColorCapture) |
               SceneSurfaceProducerBit(Producer::FeatureComposite) |
               SceneSurfaceProducerBit(Producer::EditorInteractive);
    case Semantic::SceneColorFinal: return SceneSurfaceProducerBit(Producer::PostFinal);
    case Semantic::ProjectDispatchOutput: return SceneSurfaceProducerBit(Producer::ProjectDispatch);
    case Semantic::PackedSurface:
    case Semantic::SurfaceFeatureMask:
    case Semantic::StylizedOutlineStyle:
        return SceneSurfaceProducerBit(Producer::SceneSurfaceAux) |
               SceneSurfaceProducerBit(Producer::EditorInteractive);
    case Semantic::AmbientOcclusion:
        return SceneSurfaceProducerBit(Producer::Ssao) | SceneSurfaceProducerBit(Producer::EditorInteractive);
    case Semantic::CurrentDepthPyramid:
    case Semantic::VisibilityDepthPyramid: return SceneSurfaceProducerBit(Producer::DepthPyramid);
    case Semantic::MotionVectors:
    case Semantic::MotionMetadata: return SceneSurfaceProducerBit(Producer::TemporalMotion);
    case Semantic::ReactiveMask:
    case Semantic::TransparencyMask:
    case Semantic::InvalidDepthMotionMask: return SceneSurfaceProducerBit(Producer::TemporalMask);
    case Semantic::HistoryColor:
    case Semantic::HistoryDepth: return SceneSurfaceProducerBit(Producer::TemporalHistory);
    case Semantic::None:
    case Semantic::Count: return 0u;
    }
    return 0u;
}

template <typename... Accesses>
constexpr uint32_t SurfaceAccessMask(Accesses... accesses) noexcept {
    return (0u | ... | SceneSurfaceAccessBit(accesses));
}

constexpr SceneSurfaceSemanticDesc SurfaceDesc(GRAPH::RenderGraphSemantic semantic, SceneSurfaceResourceKind kind,
                                               SceneSurfaceFormat format, SceneSurfaceLifetime lifetime,
                                               SceneSurfaceFallbackPolicy fallback, uint32_t accessMask) noexcept {
    return {semantic,   kind,
            format,     lifetime,
            fallback,   AllowedSurfacePhases(semantic),
            accessMask, AllowedSurfaceProducers(semantic)};
}

using Semantic = GRAPH::RenderGraphSemantic;
using Kind = SceneSurfaceResourceKind;
using Format = SceneSurfaceFormat;
using Lifetime = SceneSurfaceLifetime;
using Fallback = SceneSurfaceFallbackPolicy;
using Access = SceneSurfaceAccess;

inline constexpr std::array<SceneSurfaceSemanticDesc, static_cast<size_t>(GRAPH::RenderGraphSemantic::Count)>
    kSceneSurfaceDescriptions{
        SurfaceDesc(Semantic::None, Kind::Texture2D, Format::Unknown, Lifetime::FrameLocal, Fallback::SkipProvider, 0u),
        SurfaceDesc(Semantic::SceneDepth, Kind::Texture2D, Format::R32Float, Lifetime::FrameLocal,
                    Fallback::RequiredFail, SurfaceAccessMask(Access::Sampled, Access::ReadDepth, Access::WriteDepth)),
        SurfaceDesc(
            Semantic::SceneColorOpaque, Kind::Texture2D, Format::R16G16B16A16Float, Lifetime::FrameLocal,
            Fallback::RequiredFail,
            SurfaceAccessMask(Access::Sampled, Access::WriteColor, Access::CopySource, Access::CopyDestination)),
        SurfaceDesc(
            Semantic::SceneColorPostOpaque, Kind::Texture2D, Format::R16G16B16A16Float, Lifetime::FrameLocal,
            Fallback::SkipProvider,
            SurfaceAccessMask(Access::Sampled, Access::WriteColor, Access::CopySource, Access::CopyDestination)),
        SurfaceDesc(Semantic::SceneColorFinal, Kind::Texture2D, Format::R16G16B16A16Float, Lifetime::FrameLocal,
                    Fallback::SkipProvider,
                    SurfaceAccessMask(Access::Sampled, Access::WriteColor, Access::ReadUav, Access::WriteUav,
                                      Access::CopySource, Access::CopyDestination)),
        SurfaceDesc(Semantic::PackedSurface, Kind::Texture2D, Format::R16G16B16A16Float, Lifetime::FrameLocal,
                    Fallback::SkipProvider, SurfaceAccessMask(Access::Sampled, Access::WriteColor)),
        SurfaceDesc(Semantic::SurfaceFeatureMask, Kind::Texture2D, Format::R32G32Uint, Lifetime::FrameLocal,
                    Fallback::SkipProvider, SurfaceAccessMask(Access::Sampled, Access::WriteColor)),
        SurfaceDesc(Semantic::MotionVectors, Kind::Texture2D, Format::R16G16Float, Lifetime::FrameLocal,
                    Fallback::NoHistory, SurfaceAccessMask(Access::Sampled, Access::WriteColor)),
        SurfaceDesc(Semantic::MotionMetadata, Kind::Texture2D, Format::R32G32Float, Lifetime::FrameLocal,
                    Fallback::NoHistory, SurfaceAccessMask(Access::Sampled, Access::WriteColor)),
        SurfaceDesc(Semantic::CurrentDepthPyramid, Kind::DepthPyramid, Format::R32Float, Lifetime::FrameLocal,
                    Fallback::SkipProvider, SurfaceAccessMask(Access::Sampled, Access::WriteUav)),
        SurfaceDesc(Semantic::VisibilityDepthPyramid, Kind::DepthPyramid, Format::R32Float, Lifetime::FrameOrHistory,
                    Fallback::SkipProvider, SurfaceAccessMask(Access::Sampled, Access::WriteUav)),
        SurfaceDesc(Semantic::HistoryColor, Kind::Texture2D, Format::R16G16B16A16Float, Lifetime::History,
                    Fallback::NoHistory, SurfaceAccessMask(Access::Sampled, Access::WriteColor)),
        SurfaceDesc(Semantic::HistoryDepth, Kind::Texture2D, Format::R32Float, Lifetime::History, Fallback::NoHistory,
                    SurfaceAccessMask(Access::Sampled, Access::WriteColor)),
        SurfaceDesc(Semantic::StylizedOutlineStyle, Kind::Texture2D, Format::R8G8B8A8Unorm, Lifetime::FrameLocal,
                    Fallback::SkipProvider, SurfaceAccessMask(Access::Sampled, Access::WriteColor)),
        SurfaceDesc(Semantic::AmbientOcclusion, Kind::Texture2D, Format::R8Unorm, Lifetime::FrameLocal,
                    Fallback::NeutralWhite, SurfaceAccessMask(Access::Sampled, Access::WriteColor)),
        SurfaceDesc(Semantic::ReactiveMask, Kind::Texture2D, Format::R8Unorm, Lifetime::FrameLocal,
                    Fallback::NeutralBlack, SurfaceAccessMask(Access::Sampled, Access::WriteColor)),
        SurfaceDesc(Semantic::TransparencyMask, Kind::Texture2D, Format::R8Unorm, Lifetime::FrameLocal,
                    Fallback::NeutralBlack, SurfaceAccessMask(Access::Sampled, Access::WriteColor)),
        SurfaceDesc(Semantic::InvalidDepthMotionMask, Kind::Texture2D, Format::R8Unorm, Lifetime::FrameLocal,
                    Fallback::NoHistory, SurfaceAccessMask(Access::Sampled, Access::WriteColor)),
        SurfaceDesc(Semantic::ProjectDispatchOutput, Kind::Texture2D, Format::R16G16B16A16Float, Lifetime::FrameLocal,
                    Fallback::SkipProvider, SurfaceAccessMask(Access::Sampled, Access::WriteUav, Access::CopySource)),
        SurfaceDesc(Semantic::ReflectionFallbackSpecular, Kind::Texture2D, Format::R16G16B16A16Float,
                    Lifetime::FrameLocal, Fallback::SkipProvider,
                    SurfaceAccessMask(Access::Sampled, Access::WriteColor)),
        SurfaceDesc(Semantic::ReflectionSpecularResponse, Kind::Texture2D, Format::R16G16B16A16Float,
                    Lifetime::FrameLocal, Fallback::SkipProvider,
                    SurfaceAccessMask(Access::Sampled, Access::WriteColor)),
        SurfaceDesc(Semantic::SubsurfaceDiffuse, Kind::Texture2D, Format::R16G16B16A16Float, Lifetime::FrameLocal,
                    Fallback::SkipProvider, SurfaceAccessMask(Access::Sampled, Access::WriteColor)),
    };
} // namespace DETAIL

inline const SceneSurfaceSemanticDesc& GetSceneSurfaceSemanticDesc(GRAPH::RenderGraphSemantic semantic) noexcept {
    const size_t index = static_cast<size_t>(semantic);
    return index < DETAIL::kSceneSurfaceDescriptions.size() ? DETAIL::kSceneSurfaceDescriptions[index]
                                                            : DETAIL::kSceneSurfaceDescriptions[0];
}

inline bool IsSceneSurfaceKeyAllowed(GRAPH::RenderGraphSurfaceKey key) noexcept {
    return key.IsValid() && (GetSceneSurfaceSemanticDesc(key.semantic).allowedPhaseMask &
                             GRAPH::RenderGraphSemanticPhaseBit(key.phase)) != 0u;
}

} // namespace HIKARI::RENDER3D::SURFACE
