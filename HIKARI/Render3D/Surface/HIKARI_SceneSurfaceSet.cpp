#include "Render3D/Surface/HIKARI_SceneSurfaceSet.h"
namespace HIKARI::RENDER3D::SURFACE {
    namespace {
        constexpr size_t SemanticIndex(
            GRAPH::RenderGraphSemantic semantic) noexcept {
            return static_cast<size_t>(semantic);
        }

        constexpr size_t kSemanticCount =
            static_cast<size_t>(GRAPH::RenderGraphSemantic::Count);

        bool IsKnownSemantic(
            GRAPH::RenderGraphSemantic semantic) noexcept {
            const size_t index = SemanticIndex(semantic);
            return semantic != GRAPH::RenderGraphSemantic::None &&
                index < DETAIL::kSceneSurfaceDescriptions.size();
        }
    }

    bool SceneSurfaceLease::IsValid() const noexcept {
        return frame.IsValid() &&
            deviceGeneration != 0u &&
            publicationGeneration != 0u &&
            storageGeneration != 0u;
    }

    bool SceneSurfaceLease::MatchesFrameDevice(
        RenderFrameToken expectedFrame,
        uint64_t expectedDeviceGeneration) const noexcept {
        return IsValid() &&
            frame.Matches(expectedFrame) &&
            deviceGeneration == expectedDeviceGeneration;
    }

    bool SceneSurfaceLease::Matches(
        SceneSurfaceLease expected) const noexcept {
        return IsValid() && expected.IsValid() &&
            frame.Matches(expected.frame) &&
            deviceGeneration == expected.deviceGeneration &&
            publicationGeneration == expected.publicationGeneration &&
            storageGeneration == expected.storageGeneration;
    }

    bool SceneSurfacePublication::IsAvailable() const noexcept {
        return semantic != GRAPH::RenderGraphSemantic::None &&
            producer != SceneSurfaceProducer::None &&
            availability != SceneSurfaceAvailability::Unavailable &&
            lease.IsValid();
    }

    SceneSurfaceFormat ClassifySceneSurfaceFormat(
        DXGI_FORMAT format) noexcept {
        switch (format) {
        case DXGI_FORMAT_R8_UNORM:
            return SceneSurfaceFormat::R8Unorm;
        case DXGI_FORMAT_R8G8B8A8_UNORM:
            return SceneSurfaceFormat::R8G8B8A8Unorm;
        case DXGI_FORMAT_R16G16_FLOAT:
            return SceneSurfaceFormat::R16G16Float;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
            return SceneSurfaceFormat::R16G16B16A16Float;
        case DXGI_FORMAT_R32_FLOAT:
        case DXGI_FORMAT_D32_FLOAT:
            return SceneSurfaceFormat::R32Float;
        case DXGI_FORMAT_R32G32_UINT:
            return SceneSurfaceFormat::R32G32Uint;
        default:
            return SceneSurfaceFormat::Unknown;
        }
    }

    void SceneSurfaceSet::BeginFrame(
        uint64_t frameIndex,
        RenderViewId viewId,
        uint64_t deviceGeneration) noexcept {
        Reset(
            AllocateRenderFrameToken(frameIndex, viewId),
            deviceGeneration);
    }

    void SceneSurfaceSet::Reset(
        RenderFrameToken nextFrame,
        uint64_t deviceGeneration) noexcept {
        frame_ = nextFrame;
        deviceGeneration_ = deviceGeneration;
        nextPublicationGeneration_ = 1u;
        nextStorageGeneration_ = 1u;
        renderWidth_ = 0u;
        renderHeight_ = 0u;
        textures_ = {};
        depthPyramids_ = {};
        publications_ = {};
        if (!frame_.IsValid() || deviceGeneration_ == 0u) {
            frame_ = {};
            deviceGeneration_ = 0u;
        }
    }

    bool SceneSurfaceSet::PublishTexture(
        GRAPH::RenderGraphSemantic semantic,
        const RenderTextureView& texture,
        SceneSurfaceProducer producer,
        SceneSurfaceAvailability availability) noexcept {
        if (!ValidateTexturePublication(
                semantic,
                texture,
                producer,
                availability) ||
            TryGetPublication(semantic) != nullptr) {
            return false;
        }
        if (renderWidth_ == 0u || renderHeight_ == 0u) {
            renderWidth_ = texture.width;
            renderHeight_ = texture.height;
        }

        const size_t index = SemanticIndex(semantic);
        textures_[index] = texture;
        depthPyramids_[index] = {};
        SceneSurfacePublication publication{};
        publication.semantic = semantic;
        publication.producer = producer;
        publication.availability = availability;
        publication.lease.frame = frame_;
        publication.lease.deviceGeneration = deviceGeneration_;
        publication.lease.publicationGeneration =
            AllocatePublicationGeneration();
        publication.lease.storageGeneration =
            AllocateStorageGeneration();
        publications_[index] = publication;
        return publication.IsAvailable();
    }

    bool SceneSurfaceSet::PublishTextureAlias(
        GRAPH::RenderGraphSemantic targetSemantic,
        GRAPH::RenderGraphSemantic sourceSemantic,
        SceneSurfaceProducer producer) noexcept {
        if (!IsKnownSemantic(targetSemantic) ||
            producer == SceneSurfaceProducer::None ||
            (GetSceneSurfaceSemanticDesc(targetSemantic).
                allowedProducerMask &
             SceneSurfaceProducerBit(producer)) == 0u ||
            TryGetPublication(targetSemantic) != nullptr) {
            return false;
        }
        const SceneSurfacePublication* sourcePublication =
            TryGetPublication(sourceSemantic);
        const RenderTextureView* sourceTexture =
            TryGetTexture(sourceSemantic);
        if (sourcePublication == nullptr ||
            sourceTexture == nullptr ||
            sourcePublication->availability ==
                SceneSurfaceAvailability::Alias ||
            GetSceneSurfaceSemanticDesc(targetSemantic).resourceKind !=
                SceneSurfaceResourceKind::Texture2D ||
            GetSceneSurfaceSemanticDesc(targetSemantic).format !=
                ClassifySceneSurfaceFormat(sourceTexture->format)) {
            return false;
        }

        const size_t index = SemanticIndex(targetSemantic);
        textures_[index] = *sourceTexture;
        SceneSurfacePublication publication{};
        publication.semantic = targetSemantic;
        publication.producer = producer;
        publication.availability = SceneSurfaceAvailability::Alias;
        publication.lease.frame = frame_;
        publication.lease.deviceGeneration = deviceGeneration_;
        publication.lease.publicationGeneration =
            AllocatePublicationGeneration();
        publication.lease.storageGeneration =
            sourcePublication->lease.storageGeneration;
        publication.aliasSourceSemantic = sourceSemantic;
        publication.aliasSourceLease = sourcePublication->lease;
        publications_[index] = publication;
        return publication.IsAvailable();
    }

    bool SceneSurfaceSet::PublishDepthPyramid(
        GRAPH::RenderGraphSemantic semantic,
        const DEPTH::DepthPyramidView& pyramid,
        SceneSurfaceProducer producer,
        SceneSurfaceAvailability availability) noexcept {
        if (!IsKnownSemantic(semantic) ||
            GetSceneSurfaceSemanticDesc(semantic).resourceKind !=
                SceneSurfaceResourceKind::DepthPyramid ||
            producer == SceneSurfaceProducer::None ||
            (GetSceneSurfaceSemanticDesc(semantic).allowedProducerMask &
             SceneSurfaceProducerBit(producer)) == 0u ||
            availability == SceneSurfaceAvailability::Unavailable ||
            !pyramid.publication.Matches(frame_) ||
            !IsDepthPyramidSampleable(pyramid) ||
            (semantic ==
                    GRAPH::RenderGraphSemantic::CurrentDepthPyramid &&
                (pyramid.viewKind !=
                        DEPTH::DepthPyramidViewKind::CurrentFrame ||
                 pyramid.sourceKind !=
                        DEPTH::DepthPyramidSourceKind::SceneDepth)) ||
            (semantic ==
                    GRAPH::RenderGraphSemantic::VisibilityDepthPyramid &&
                pyramid.sourceKind !=
                    DEPTH::DepthPyramidSourceKind::VisibilityPrepass &&
                pyramid.sourceKind !=
                    DEPTH::DepthPyramidSourceKind::SceneDepth) ||
            TryGetPublication(semantic) != nullptr) {
            return false;
        }

        const size_t index = SemanticIndex(semantic);
        depthPyramids_[index] = pyramid;
        textures_[index] = {};
        SceneSurfacePublication publication{};
        publication.semantic = semantic;
        publication.producer = producer;
        publication.availability = availability;
        publication.lease.frame = frame_;
        publication.lease.deviceGeneration = deviceGeneration_;
        publication.lease.publicationGeneration =
            AllocatePublicationGeneration();
        publication.lease.storageGeneration =
            AllocateStorageGeneration();
        publications_[index] = publication;
        return publication.IsAvailable();
    }

    bool SceneSurfaceSet::Invalidate(
        GRAPH::RenderGraphSemantic semantic) noexcept {
        if (!IsKnownSemantic(semantic)) {
            return false;
        }
        const size_t index = SemanticIndex(semantic);
        const SceneSurfaceLease invalidatedLease =
            publications_[index].lease;
        textures_[index] = {};
        depthPyramids_[index] = {};
        publications_[index] = {};
        if (invalidatedLease.IsValid()) {
            for (size_t aliasIndex = 0u;
                 aliasIndex < publications_.size();
                 ++aliasIndex) {
                const SceneSurfacePublication& candidate =
                    publications_[aliasIndex];
                if (candidate.availability ==
                        SceneSurfaceAvailability::Alias &&
                    candidate.aliasSourceSemantic == semantic &&
                    candidate.aliasSourceLease.Matches(invalidatedLease)) {
                    textures_[aliasIndex] = {};
                    depthPyramids_[aliasIndex] = {};
                    publications_[aliasIndex] = {};
                }
            }
        }
        return true;
    }

    const RenderTextureView* SceneSurfaceSet::TryGetTexture(
        GRAPH::RenderGraphSemantic semantic) const noexcept {
        if (!IsKnownSemantic(semantic) ||
            GetSceneSurfaceSemanticDesc(semantic).resourceKind !=
                SceneSurfaceResourceKind::Texture2D) {
            return nullptr;
        }
        const size_t index = SemanticIndex(semantic);
        return IsPublicationCurrent(publications_[index]) &&
            textures_[index].IsSampleable()
                ? &textures_[index]
                : nullptr;
    }

    const RenderTextureView* SceneSurfaceSet::TryGetTexture(
        GRAPH::RenderGraphSemantic semantic,
        RenderFrameToken expectedFrame) const noexcept {
        return MatchesFrame(expectedFrame)
            ? TryGetTexture(semantic)
            : nullptr;
    }

    const DEPTH::DepthPyramidView* SceneSurfaceSet::TryGetDepthPyramid(
        GRAPH::RenderGraphSemantic semantic) const noexcept {
        if (!IsKnownSemantic(semantic) ||
            GetSceneSurfaceSemanticDesc(semantic).resourceKind !=
                SceneSurfaceResourceKind::DepthPyramid) {
            return nullptr;
        }
        const size_t index = SemanticIndex(semantic);
        return IsPublicationCurrent(publications_[index]) &&
            IsDepthPyramidSampleable(depthPyramids_[index])
                ? &depthPyramids_[index]
                : nullptr;
    }

    const DEPTH::DepthPyramidView* SceneSurfaceSet::TryGetDepthPyramid(
        GRAPH::RenderGraphSemantic semantic,
        RenderFrameToken expectedFrame) const noexcept {
        return MatchesFrame(expectedFrame)
            ? TryGetDepthPyramid(semantic)
            : nullptr;
    }

    const SceneSurfacePublication* SceneSurfaceSet::TryGetPublication(
        GRAPH::RenderGraphSemantic semantic) const noexcept {
        if (!IsKnownSemantic(semantic)) {
            return nullptr;
        }
        const SceneSurfacePublication& publication =
            publications_[SemanticIndex(semantic)];
        return IsPublicationCurrent(publication)
            ? &publication
            : nullptr;
    }

    bool SceneSurfaceSet::IsLeaseCurrent(
        GRAPH::RenderGraphSemantic semantic,
        SceneSurfaceLease lease) const noexcept {
        const SceneSurfacePublication* publication =
            TryGetPublication(semantic);
        return publication != nullptr &&
            publication->lease.Matches(lease);
    }

    bool SceneSurfaceSet::TryDescribe(
        GRAPH::RenderGraphSemantic semantic,
        SceneSurfaceDescriptor& outDescriptor) const noexcept {
        outDescriptor = {};
        const SceneSurfacePublication* publication =
            TryGetPublication(semantic);
        if (publication == nullptr) {
            return false;
        }
        outDescriptor.semantic = GetSceneSurfaceSemanticDesc(semantic);
        outDescriptor.publication = *publication;
        if (outDescriptor.semantic.resourceKind ==
                SceneSurfaceResourceKind::Texture2D) {
            const RenderTextureView* texture = TryGetTexture(semantic);
            if (texture == nullptr) {
                return false;
            }
            outDescriptor.width = texture->width;
            outDescriptor.height = texture->height;
            outDescriptor.mipCount = texture->mipCount;
            outDescriptor.sampleable = texture->IsSampleable();
            return outDescriptor.sampleable;
        }
        const DEPTH::DepthPyramidView* pyramid =
            TryGetDepthPyramid(semantic);
        if (pyramid == nullptr) {
            return false;
        }
        outDescriptor.width = pyramid->width;
        outDescriptor.height = pyramid->height;
        outDescriptor.mipCount = pyramid->mipCount;
        outDescriptor.sampleable = IsDepthPyramidSampleable(*pyramid);
        return outDescriptor.sampleable;
    }

    bool SceneSurfaceSet::IsValid(
        GRAPH::RenderGraphSemantic semantic) const noexcept {
        return TryGetTexture(semantic) != nullptr ||
            TryGetDepthPyramid(semantic) != nullptr;
    }

    bool SceneSurfaceSet::IsValid(
        GRAPH::RenderGraphSemantic semantic,
        RenderFrameToken expectedFrame) const noexcept {
        return MatchesFrame(expectedFrame) && IsValid(semantic);
    }

    bool SceneSurfaceSet::HasOpaqueSurface() const noexcept {
        return TryGetTexture(GRAPH::RenderGraphSemantic::SceneDepth) !=
                nullptr &&
            TryGetTexture(GRAPH::RenderGraphSemantic::SceneColorOpaque) !=
                nullptr &&
            TryGetTexture(GRAPH::RenderGraphSemantic::PackedSurface) !=
                nullptr;
    }

    bool SceneSurfaceSet::HasStylizedFeatureMask() const noexcept {
        return TryGetTexture(
            GRAPH::RenderGraphSemantic::SurfaceFeatureMask) != nullptr;
    }

    bool SceneSurfaceSet::HasStylizedOutlineStyle() const noexcept {
        const RenderTextureView* style = TryGetTexture(
            GRAPH::RenderGraphSemantic::StylizedOutlineStyle);
        const RenderTextureView* mask = TryGetTexture(
            GRAPH::RenderGraphSemantic::SurfaceFeatureMask);
        return style != nullptr && mask != nullptr &&
            style->width == mask->width &&
            style->height == mask->height;
    }

    bool SceneSurfaceSet::HasCurrentDepthHierarchy() const noexcept {
        const RenderTextureView* sceneDepth = TryGetTexture(
            GRAPH::RenderGraphSemantic::SceneDepth);
        const RenderTextureView* packedSurface = TryGetTexture(
            GRAPH::RenderGraphSemantic::PackedSurface);
        const DEPTH::DepthPyramidView* pyramid = TryGetDepthPyramid(
            GRAPH::RenderGraphSemantic::CurrentDepthPyramid);
        return sceneDepth != nullptr &&
            packedSurface != nullptr &&
            pyramid != nullptr &&
            pyramid->viewKind == DEPTH::DepthPyramidViewKind::CurrentFrame &&
            pyramid->sourceKind == DEPTH::DepthPyramidSourceKind::SceneDepth &&
            pyramid->sourceWidth == sceneDepth->width &&
            pyramid->sourceHeight == sceneDepth->height &&
            packedSurface->width == sceneDepth->width &&
            packedSurface->height == sceneDepth->height;
    }

    bool SceneSurfaceSet::ValidateTexturePublication(
        GRAPH::RenderGraphSemantic semantic,
        const RenderTextureView& texture,
        SceneSurfaceProducer producer,
        SceneSurfaceAvailability availability) const noexcept {
        if (!IsFrameValid() ||
            !IsKnownSemantic(semantic) ||
            producer == SceneSurfaceProducer::None ||
            availability == SceneSurfaceAvailability::Unavailable ||
            !texture.IsSampleable()) {
            return false;
        }
        const SceneSurfaceSemanticDesc& desc =
            GetSceneSurfaceSemanticDesc(semantic);
        return desc.resourceKind == SceneSurfaceResourceKind::Texture2D &&
            (desc.allowedProducerMask &
             SceneSurfaceProducerBit(producer)) != 0u &&
            (desc.format == SceneSurfaceFormat::Unknown ||
             desc.format == ClassifySceneSurfaceFormat(texture.format)) &&
            ValidateFullResolution(texture.width, texture.height);
    }

    bool SceneSurfaceSet::ValidateFullResolution(
        uint32_t width,
        uint32_t height) const noexcept {
        return width != 0u && height != 0u &&
            ((renderWidth_ == 0u && renderHeight_ == 0u) ||
             (renderWidth_ == width && renderHeight_ == height));
    }

    bool SceneSurfaceSet::IsPublicationCurrent(
        const SceneSurfacePublication& publication) const noexcept {
        if (!publication.IsAvailable() ||
            !publication.lease.MatchesFrameDevice(
                frame_,
                deviceGeneration_)) {
            return false;
        }
        if (publication.availability !=
                SceneSurfaceAvailability::Alias) {
            return true;
        }
        if (!IsKnownSemantic(publication.aliasSourceSemantic)) {
            return false;
        }
        const SceneSurfacePublication& source =
            publications_[SemanticIndex(
                publication.aliasSourceSemantic)];
        return source.IsAvailable() &&
            source.lease.Matches(publication.aliasSourceLease);
    }

    bool SceneSurfaceSet::IsDepthPyramidSampleable(
        const DEPTH::DepthPyramidView& pyramid) const noexcept {
        return pyramid.publication.Matches(frame_) &&
            pyramid.valid &&
            pyramid.viewKind != DEPTH::DepthPyramidViewKind::Invalid &&
            pyramid.sourceKind != DEPTH::DepthPyramidSourceKind::Unknown &&
            pyramid.depthConvention ==
                DEPTH::DepthPyramidDepthConvention::StandardD3DLessMax &&
            pyramid.pyramidSrv.ptr != 0u &&
            pyramid.mipCount != 0u &&
            pyramid.sourceWidth != 0u &&
            pyramid.sourceHeight != 0u &&
            pyramid.width != 0u &&
            pyramid.height != 0u;
    }

    uint64_t SceneSurfaceSet::AllocatePublicationGeneration() noexcept {
        uint64_t generation = nextPublicationGeneration_++;
        if (generation == 0u) {
            generation = nextPublicationGeneration_++;
        }
        return generation;
    }

    uint64_t SceneSurfaceSet::AllocateStorageGeneration() noexcept {
        uint64_t generation = nextStorageGeneration_++;
        if (generation == 0u) {
            generation = nextStorageGeneration_++;
        }
        return generation;
    }

} // namespace HIKARI::RENDER3D::SURFACE
