#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "Render3D/Core/HIKARI_RenderFrameToken.h"
#include "Render3D/Core/HIKARI_RenderView.h"
#include "Render3D/Depth/HIKARI_DepthPyramidLayer.h"
#include "Render3D/Resources/HIKARI_RenderTextureView.h"
#include "Render3D/Graph/HIKARI_RenderGraphSurfaceContract.h"

namespace HIKARI::RENDER3D::SURFACE {

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
