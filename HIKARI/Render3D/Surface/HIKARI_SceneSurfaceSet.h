#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "Render3D/Core/HIKARI_RenderFrameToken.h"
#include "Render3D/Core/HIKARI_RenderView.h"
#include "Render3D/Depth/HIKARI_DepthPyramidLayer.h"
#include "RenderCore/Resources/HIKARI_RenderTextureView.h"
#include "Render3D/Graph/HIKARI_RenderGraphSurfaceContract.h"

namespace HIKARI::RENDER3D::SURFACE {

struct SceneSurfaceLease {
    GRAPH::RenderGraphSurfaceKey key{};
    RenderFrameToken frame{};
    uint64_t deviceGeneration = 0u;
    uint64_t publicationGeneration = 0u;
    uint64_t storageGeneration = 0u;

    bool IsValid() const noexcept;
    bool MatchesFrameDevice(RenderFrameToken expectedFrame, uint64_t expectedDeviceGeneration) const noexcept;
    bool Matches(SceneSurfaceLease expected) const noexcept;
};

struct SceneSurfacePublication {
    GRAPH::RenderGraphSurfaceKey key{};
    SceneSurfaceProducer producer = SceneSurfaceProducer::None;
    SceneSurfaceAvailability availability = SceneSurfaceAvailability::Unavailable;
    SceneSurfaceLease lease{};
    GRAPH::RenderGraphSurfaceKey aliasSourceKey{};
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

SceneSurfaceFormat ClassifySceneSurfaceFormat(DXGI_FORMAT format) noexcept;

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

    void BeginFrame(uint64_t frameIndex, RenderViewId viewId, uint64_t deviceGeneration) noexcept;
    void Reset(RenderFrameToken nextFrame, uint64_t deviceGeneration) noexcept;

    bool PublishTexture(GRAPH::RenderGraphSurfaceKey key, const RenderTextureView& texture,
                        SceneSurfaceProducer producer,
                        SceneSurfaceAvailability availability = SceneSurfaceAvailability::Produced) noexcept;
    bool PublishTextureAlias(GRAPH::RenderGraphSurfaceKey target, GRAPH::RenderGraphSurfaceKey source,
                             SceneSurfaceProducer producer) noexcept;
    bool PublishDepthPyramid(GRAPH::RenderGraphSurfaceKey key, const DEPTH::DepthPyramidView& pyramid,
                             SceneSurfaceProducer producer,
                             SceneSurfaceAvailability availability = SceneSurfaceAvailability::Produced) noexcept;
    bool Invalidate(GRAPH::RenderGraphSurfaceKey key) noexcept;

    bool IsFrameValid() const noexcept { return frame_.IsValid() && deviceGeneration_ != 0u; }
    bool MatchesFrame(RenderFrameToken expected) const noexcept { return IsFrameValid() && frame_.Matches(expected); }
    RenderFrameToken GetFrameToken() const noexcept { return frame_; }
    uint64_t GetDeviceGeneration() const noexcept { return deviceGeneration_; }

    const RenderTextureView* TryGetTexture(GRAPH::RenderGraphSurfaceKey key) const noexcept;
    const DEPTH::DepthPyramidView* TryGetDepthPyramid(GRAPH::RenderGraphSurfaceKey key) const noexcept;
    const SceneSurfacePublication* TryGetPublication(GRAPH::RenderGraphSurfaceKey key) const noexcept;
    bool IsLeaseCurrent(GRAPH::RenderGraphSurfaceKey key, SceneSurfaceLease lease) const noexcept;
    bool TryDescribe(GRAPH::RenderGraphSurfaceKey key, SceneSurfaceDescriptor& outDescriptor) const noexcept;
    bool IsValid(GRAPH::RenderGraphSurfaceKey key) const noexcept;
    bool HasOpaqueSurface() const noexcept;
    bool HasStylizedFeatureMask() const noexcept;
    bool HasStylizedOutlineStyle() const noexcept;
    bool HasCurrentDepthHierarchy() const noexcept;

private:
    static constexpr size_t kSemanticCount = static_cast<size_t>(GRAPH::RenderGraphSemantic::Count);
    static constexpr size_t kPhaseCount = static_cast<size_t>(GRAPH::RenderGraphSemanticPhase::Count);
    static constexpr size_t kSurfaceSlotCount = kSemanticCount * kPhaseCount;

    bool ValidateTexturePublication(GRAPH::RenderGraphSurfaceKey key, const RenderTextureView& texture,
                                    SceneSurfaceProducer producer,
                                    SceneSurfaceAvailability availability) const noexcept;
    bool ValidateFullResolution(uint32_t width, uint32_t height) const noexcept;
    bool IsPublicationCurrent(const SceneSurfacePublication& publication) const noexcept;
    bool IsDepthPyramidSampleable(const DEPTH::DepthPyramidView& pyramid) const noexcept;
    uint64_t AllocatePublicationGeneration() noexcept;
    uint64_t AllocateStorageGeneration() noexcept;

    RenderFrameToken frame_{};
    uint64_t deviceGeneration_ = 0u;
    uint64_t nextPublicationGeneration_ = 1u;
    uint64_t nextStorageGeneration_ = 1u;
    uint32_t renderWidth_ = 0u;
    uint32_t renderHeight_ = 0u;
    std::array<RenderTextureView, kSurfaceSlotCount> textures_{};
    std::array<DEPTH::DepthPyramidView, kSurfaceSlotCount> depthPyramids_{};
    std::array<SceneSurfacePublication, kSurfaceSlotCount> publications_{};
};

static_assert(std::is_standard_layout_v<SceneSurfaceLease>);
static_assert(std::is_trivially_copyable_v<SceneSurfaceLease>);
static_assert(std::is_standard_layout_v<SceneSurfaceDescriptor>);
static_assert(std::is_trivially_copyable_v<SceneSurfaceDescriptor>);

} // namespace HIKARI::RENDER3D::SURFACE
