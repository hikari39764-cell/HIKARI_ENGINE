#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "Render3D/Core/HIKARI_RenderFrameToken.h"
#include "Render3D/Core/HIKARI_RenderView.h"
#include "Render3D/Depth/HIKARI_DepthPyramidLayer.h"
#include "Render3D/Graph/HIKARI_RenderGraph.h"
#include "Render3D/Resources/HIKARI_RenderTextureView.h"

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
    };

    enum class SceneSurfaceLifetime : uint8_t {
        FrameLocal = 0u,
        History,
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
        ReadWriteUav = 1u << 4u,
        CopySource = 1u << 5u,
        CopyDestination = 1u << 6u,
    };

    constexpr uint32_t SceneSurfaceAccessBit(
        SceneSurfaceAccess access) noexcept {
        return static_cast<uint32_t>(access);
    }

    constexpr uint64_t SceneSurfaceProducerBit(
        SceneSurfaceProducer producer) noexcept {
        return 1ull << static_cast<uint16_t>(producer);
    }

    struct SceneSurfaceSemanticDesc {
        GRAPH::RenderGraphSemantic semantic =
            GRAPH::RenderGraphSemantic::None;
        SceneSurfaceResourceKind resourceKind =
            SceneSurfaceResourceKind::Texture2D;
        SceneSurfaceFormat format = SceneSurfaceFormat::Unknown;
        SceneSurfaceLifetime lifetime = SceneSurfaceLifetime::FrameLocal;
        SceneSurfaceFallbackPolicy fallback =
            SceneSurfaceFallbackPolicy::SkipProvider;
        uint32_t allowedAccessMask = 0u;
        uint64_t allowedProducerMask = 0u;
    };

    namespace DETAIL {
        constexpr uint64_t AllowedSurfaceProducers(
            GRAPH::RenderGraphSemantic semantic) noexcept {
            using Semantic = GRAPH::RenderGraphSemantic;
            using Producer = SceneSurfaceProducer;
            switch (semantic) {
            case Semantic::SceneDepth:
                return SceneSurfaceProducerBit(Producer::PostSceneDepth) |
                    SceneSurfaceProducerBit(Producer::EditorInteractive);
            case Semantic::SceneColorOpaque:
                return SceneSurfaceProducerBit(Producer::ForwardOpaque) |
                    SceneSurfaceProducerBit(Producer::SceneColorCapture) |
                    SceneSurfaceProducerBit(Producer::EditorInteractive);
            case Semantic::SceneColorPostOpaque:
                return SceneSurfaceProducerBit(Producer::SceneColorCapture) |
                    SceneSurfaceProducerBit(Producer::FeatureComposite) |
                    SceneSurfaceProducerBit(Producer::EditorInteractive);
            case Semantic::SceneColorFinal:
                return SceneSurfaceProducerBit(Producer::PostFinal);
            case Semantic::PackedSurface:
            case Semantic::SurfaceFeatureMask:
            case Semantic::StylizedOutlineStyle:
                return SceneSurfaceProducerBit(Producer::SceneSurfaceAux) |
                    SceneSurfaceProducerBit(Producer::EditorInteractive);
            case Semantic::AmbientOcclusion:
                return SceneSurfaceProducerBit(Producer::Ssao) |
                    SceneSurfaceProducerBit(Producer::EditorInteractive);
            case Semantic::CurrentDepthPyramid:
            case Semantic::VisibilityDepthPyramid:
                return SceneSurfaceProducerBit(Producer::DepthPyramid);
            case Semantic::MotionVectors:
            case Semantic::MotionMetadata:
                return SceneSurfaceProducerBit(Producer::TemporalMotion);
            case Semantic::ReactiveMask:
            case Semantic::TransparencyMask:
            case Semantic::InvalidDepthMotionMask:
                return SceneSurfaceProducerBit(Producer::TemporalMask);
            case Semantic::HistoryColor:
            case Semantic::HistoryDepth:
                return SceneSurfaceProducerBit(Producer::TemporalHistory);
            case Semantic::None:
            case Semantic::Count:
                return 0u;
            }
            return 0u;
        }

        template<typename... Accesses>
        constexpr uint32_t SurfaceAccessMask(Accesses... accesses) noexcept {
            return (0u | ... | SceneSurfaceAccessBit(accesses));
        }

        constexpr SceneSurfaceSemanticDesc SurfaceDesc(
            GRAPH::RenderGraphSemantic semantic,
            SceneSurfaceResourceKind kind,
            SceneSurfaceFormat format,
            SceneSurfaceLifetime lifetime,
            SceneSurfaceFallbackPolicy fallback,
            uint32_t accessMask) noexcept {
            return {
                semantic,
                kind,
                format,
                lifetime,
                fallback,
                accessMask,
                AllowedSurfaceProducers(semantic)
            };
        }

        using Semantic = GRAPH::RenderGraphSemantic;
        using Kind = SceneSurfaceResourceKind;
        using Format = SceneSurfaceFormat;
        using Lifetime = SceneSurfaceLifetime;
        using Fallback = SceneSurfaceFallbackPolicy;
        using Access = SceneSurfaceAccess;

        inline constexpr std::array<
            SceneSurfaceSemanticDesc,
            static_cast<size_t>(GRAPH::RenderGraphSemantic::Count)>
            kSceneSurfaceDescriptions{
                SurfaceDesc(Semantic::None, Kind::Texture2D, Format::Unknown,
                    Lifetime::FrameLocal, Fallback::SkipProvider, 0u),
                SurfaceDesc(Semantic::SceneDepth, Kind::Texture2D,
                    Format::R32Float, Lifetime::FrameLocal,
                    Fallback::RequiredFail,
                    SurfaceAccessMask(Access::Sampled, Access::ReadDepth,
                        Access::WriteDepth)),
                SurfaceDesc(Semantic::SceneColorOpaque, Kind::Texture2D,
                    Format::R16G16B16A16Float, Lifetime::FrameLocal,
                    Fallback::RequiredFail,
                    SurfaceAccessMask(Access::Sampled, Access::WriteColor,
                        Access::CopySource, Access::CopyDestination)),
                SurfaceDesc(Semantic::SceneColorPostOpaque, Kind::Texture2D,
                    Format::R16G16B16A16Float, Lifetime::FrameLocal,
                    Fallback::SkipProvider,
                    SurfaceAccessMask(Access::Sampled, Access::WriteColor,
                        Access::CopySource, Access::CopyDestination)),
                SurfaceDesc(Semantic::SceneColorFinal, Kind::Texture2D,
                    Format::R16G16B16A16Float, Lifetime::FrameLocal,
                    Fallback::SkipProvider,
                    SurfaceAccessMask(Access::Sampled, Access::WriteColor,
                        Access::ReadWriteUav, Access::CopySource)),
                SurfaceDesc(Semantic::PackedSurface, Kind::Texture2D,
                    Format::R16G16B16A16Float, Lifetime::FrameLocal,
                    Fallback::SkipProvider,
                    SurfaceAccessMask(Access::Sampled, Access::WriteColor)),
                SurfaceDesc(Semantic::SurfaceFeatureMask, Kind::Texture2D,
                    Format::R32G32Uint, Lifetime::FrameLocal,
                    Fallback::SkipProvider,
                    SurfaceAccessMask(Access::Sampled, Access::WriteColor)),
                SurfaceDesc(Semantic::MotionVectors, Kind::Texture2D,
                    Format::R16G16Float, Lifetime::FrameLocal,
                    Fallback::NoHistory,
                    SurfaceAccessMask(Access::Sampled, Access::WriteColor)),
                SurfaceDesc(Semantic::MotionMetadata, Kind::Texture2D,
                    Format::R16G16Float, Lifetime::FrameLocal,
                    Fallback::NoHistory,
                    SurfaceAccessMask(Access::Sampled, Access::WriteColor)),
                SurfaceDesc(Semantic::CurrentDepthPyramid,
                    Kind::DepthPyramid, Format::R32Float,
                    Lifetime::FrameLocal, Fallback::SkipProvider,
                    SurfaceAccessMask(Access::Sampled,
                        Access::ReadWriteUav)),
                SurfaceDesc(Semantic::VisibilityDepthPyramid,
                    Kind::DepthPyramid, Format::R32Float, Lifetime::History,
                    Fallback::SkipProvider,
                    SurfaceAccessMask(Access::Sampled,
                        Access::ReadWriteUav)),
                SurfaceDesc(Semantic::HistoryColor, Kind::Texture2D,
                    Format::R16G16B16A16Float, Lifetime::History,
                    Fallback::NoHistory,
                    SurfaceAccessMask(Access::Sampled, Access::WriteColor)),
                SurfaceDesc(Semantic::HistoryDepth, Kind::Texture2D,
                    Format::R32Float, Lifetime::History,
                    Fallback::NoHistory,
                    SurfaceAccessMask(Access::Sampled, Access::WriteColor)),
                SurfaceDesc(Semantic::StylizedOutlineStyle, Kind::Texture2D,
                    Format::R8G8B8A8Unorm, Lifetime::FrameLocal,
                    Fallback::SkipProvider,
                    SurfaceAccessMask(Access::Sampled, Access::WriteColor)),
                SurfaceDesc(Semantic::AmbientOcclusion, Kind::Texture2D,
                    Format::R8Unorm, Lifetime::FrameLocal,
                    Fallback::NeutralWhite,
                    SurfaceAccessMask(Access::Sampled, Access::WriteColor)),
                SurfaceDesc(Semantic::ReactiveMask, Kind::Texture2D,
                    Format::R8Unorm, Lifetime::FrameLocal,
                    Fallback::NeutralBlack,
                    SurfaceAccessMask(Access::Sampled, Access::WriteColor)),
                SurfaceDesc(Semantic::TransparencyMask, Kind::Texture2D,
                    Format::R8Unorm, Lifetime::FrameLocal,
                    Fallback::NeutralBlack,
                    SurfaceAccessMask(Access::Sampled, Access::WriteColor)),
                SurfaceDesc(Semantic::InvalidDepthMotionMask,
                    Kind::Texture2D, Format::R8Unorm,
                    Lifetime::FrameLocal, Fallback::NoHistory,
                    SurfaceAccessMask(Access::Sampled, Access::WriteColor)),
            };
    }

    struct SceneSurfaceLease {
        RenderFrameToken frame{};
        uint64_t deviceGeneration = 0u;
        uint64_t publicationGeneration = 0u;
        uint64_t storageGeneration = 0u;

        bool IsValid() const noexcept;
        bool MatchesFrameDevice(
            RenderFrameToken expectedFrame,
            uint64_t expectedDeviceGeneration) const noexcept;
        bool Matches(SceneSurfaceLease expected) const noexcept;
    };

    struct SceneSurfacePublication {
        GRAPH::RenderGraphSemantic semantic =
            GRAPH::RenderGraphSemantic::None;
        SceneSurfaceProducer producer = SceneSurfaceProducer::None;
        SceneSurfaceAvailability availability =
            SceneSurfaceAvailability::Unavailable;
        SceneSurfaceLease lease{};
        GRAPH::RenderGraphSemantic aliasSourceSemantic =
            GRAPH::RenderGraphSemantic::None;
        SceneSurfaceLease aliasSourceLease{};

        bool IsAvailable() const noexcept;
    };

    struct SceneSurfaceDescriptor {
        SceneSurfaceSemanticDesc semantic{};
        SceneSurfacePublication publication{};
        uint32_t width = 0u;
        uint32_t height = 0u;
        uint32_t mipCount = 0u;
        bool sampleable = false;
    };

    inline const SceneSurfaceSemanticDesc& GetSceneSurfaceSemanticDesc(
        GRAPH::RenderGraphSemantic semantic) noexcept {
        const size_t index = static_cast<size_t>(semantic);
        return index < DETAIL::kSceneSurfaceDescriptions.size()
            ? DETAIL::kSceneSurfaceDescriptions[index]
            : DETAIL::kSceneSurfaceDescriptions[0];
    }
    SceneSurfaceFormat ClassifySceneSurfaceFormat(
        DXGI_FORMAT format) noexcept;

    // Frame-local semantic registry. GPU allocation, barriers, descriptors,
    // and release stay with producers; consumers can only borrow a validated
    // publication carrying frame, device, logical version, and storage lease.
    struct SceneSurfaceSet final {
    public:
        SceneSurfaceSet() noexcept = default;
        SceneSurfaceSet(const SceneSurfaceSet&) = delete;
        SceneSurfaceSet& operator=(const SceneSurfaceSet&) = delete;
        SceneSurfaceSet(SceneSurfaceSet&&) = delete;
        SceneSurfaceSet& operator=(SceneSurfaceSet&&) = delete;

        void BeginFrame(
            uint64_t frameIndex,
            RenderViewId viewId,
            uint64_t deviceGeneration) noexcept;
        void Reset(
            RenderFrameToken nextFrame,
            uint64_t deviceGeneration) noexcept;

        bool PublishTexture(
            GRAPH::RenderGraphSemantic semantic,
            const RenderTextureView& texture,
            SceneSurfaceProducer producer,
            SceneSurfaceAvailability availability =
                SceneSurfaceAvailability::Produced) noexcept;
        bool PublishTextureAlias(
            GRAPH::RenderGraphSemantic targetSemantic,
            GRAPH::RenderGraphSemantic sourceSemantic,
            SceneSurfaceProducer producer) noexcept;
        bool PublishDepthPyramid(
            GRAPH::RenderGraphSemantic semantic,
            const DEPTH::DepthPyramidView& pyramid,
            SceneSurfaceProducer producer,
            SceneSurfaceAvailability availability =
                SceneSurfaceAvailability::Produced) noexcept;
        bool Invalidate(
            GRAPH::RenderGraphSemantic semantic) noexcept;

        bool IsFrameValid() const noexcept {
            return frame_.IsValid() && deviceGeneration_ != 0u;
        }
        bool MatchesFrame(RenderFrameToken expected) const noexcept {
            return IsFrameValid() && frame_.Matches(expected);
        }
        RenderFrameToken GetFrameToken() const noexcept { return frame_; }
        uint64_t GetDeviceGeneration() const noexcept {
            return deviceGeneration_;
        }

        const RenderTextureView* TryGetTexture(
            GRAPH::RenderGraphSemantic semantic) const noexcept;
        const RenderTextureView* TryGetTexture(
            GRAPH::RenderGraphSemantic semantic,
            RenderFrameToken expectedFrame) const noexcept;
        const DEPTH::DepthPyramidView* TryGetDepthPyramid(
            GRAPH::RenderGraphSemantic semantic) const noexcept;
        const DEPTH::DepthPyramidView* TryGetDepthPyramid(
            GRAPH::RenderGraphSemantic semantic,
            RenderFrameToken expectedFrame) const noexcept;
        const SceneSurfacePublication* TryGetPublication(
            GRAPH::RenderGraphSemantic semantic) const noexcept;
        bool IsLeaseCurrent(
            GRAPH::RenderGraphSemantic semantic,
            SceneSurfaceLease lease) const noexcept;
        bool TryDescribe(
            GRAPH::RenderGraphSemantic semantic,
            SceneSurfaceDescriptor& outDescriptor) const noexcept;

        bool IsValid(
            GRAPH::RenderGraphSemantic semantic) const noexcept;
        bool IsValid(
            GRAPH::RenderGraphSemantic semantic,
            RenderFrameToken expectedFrame) const noexcept;
        bool HasOpaqueSurface() const noexcept;
        bool HasStylizedFeatureMask() const noexcept;
        bool HasStylizedOutlineStyle() const noexcept;
        bool HasCurrentDepthHierarchy() const noexcept;

    private:
        static constexpr size_t kSemanticCount =
            static_cast<size_t>(GRAPH::RenderGraphSemantic::Count);

        bool ValidateTexturePublication(
            GRAPH::RenderGraphSemantic semantic,
            const RenderTextureView& texture,
            SceneSurfaceProducer producer,
            SceneSurfaceAvailability availability) const noexcept;
        bool ValidateFullResolution(
            uint32_t width,
            uint32_t height) const noexcept;
        bool IsPublicationCurrent(
            const SceneSurfacePublication& publication) const noexcept;
        bool IsDepthPyramidSampleable(
            const DEPTH::DepthPyramidView& pyramid) const noexcept;
        uint64_t AllocatePublicationGeneration() noexcept;
        uint64_t AllocateStorageGeneration() noexcept;

        RenderFrameToken frame_{};
        uint64_t deviceGeneration_ = 0u;
        uint64_t nextPublicationGeneration_ = 1u;
        uint64_t nextStorageGeneration_ = 1u;
        uint32_t renderWidth_ = 0u;
        uint32_t renderHeight_ = 0u;
        std::array<RenderTextureView, kSemanticCount> textures_{};
        std::array<DEPTH::DepthPyramidView, kSemanticCount>
            depthPyramids_{};
        std::array<SceneSurfacePublication, kSemanticCount>
            publications_{};
    };

    static_assert(std::is_standard_layout_v<SceneSurfaceLease>);
    static_assert(std::is_trivially_copyable_v<SceneSurfaceLease>);
    static_assert(std::is_standard_layout_v<SceneSurfaceDescriptor>);
    static_assert(std::is_trivially_copyable_v<SceneSurfaceDescriptor>);

} // namespace HIKARI::RENDER3D::SURFACE
