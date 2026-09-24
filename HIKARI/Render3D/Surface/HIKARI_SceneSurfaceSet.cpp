#include "Render3D/Surface/HIKARI_SceneSurfaceSet.h"
namespace HIKARI::RENDER3D::SURFACE {
namespace {
constexpr size_t SurfaceIndex(GRAPH::RenderGraphSurfaceKey key) noexcept {
    return static_cast<size_t>(key.semantic) * static_cast<size_t>(GRAPH::RenderGraphSemanticPhase::Count) +
           static_cast<size_t>(key.phase);
}

bool IsKnownKey(GRAPH::RenderGraphSurfaceKey key) noexcept { return IsSceneSurfaceKeyAllowed(key); }

constexpr GRAPH::RenderGraphSurfaceKey Key(GRAPH::RenderGraphSemantic semantic,
                                           GRAPH::RenderGraphSemanticPhase phase) noexcept {
    return {semantic, phase};
}
} // namespace

bool SceneSurfaceLease::IsValid() const noexcept {
    return IsSceneSurfaceKeyAllowed(key) && frame.IsValid() && deviceGeneration != 0u && publicationGeneration != 0u &&
           storageGeneration != 0u;
}

bool SceneSurfaceLease::MatchesFrameDevice(RenderFrameToken expectedFrame,
                                           uint64_t expectedDeviceGeneration) const noexcept {
    return IsValid() && frame.Matches(expectedFrame) && deviceGeneration == expectedDeviceGeneration;
}

bool SceneSurfaceLease::Matches(SceneSurfaceLease expected) const noexcept {
    return IsValid() && expected.IsValid() && key.Matches(expected.key) && frame.Matches(expected.frame) &&
           deviceGeneration == expected.deviceGeneration && publicationGeneration == expected.publicationGeneration &&
           storageGeneration == expected.storageGeneration;
}

bool SceneSurfacePublication::IsAvailable() const noexcept {
    return IsSceneSurfaceKeyAllowed(key) && producer != SceneSurfaceProducer::None &&
           availability != SceneSurfaceAvailability::Unavailable && lease.IsValid();
}

SceneSurfaceFormat ClassifySceneSurfaceFormat(DXGI_FORMAT format) noexcept {
    switch (format) {
    case DXGI_FORMAT_R8_UNORM: return SceneSurfaceFormat::R8Unorm;
    case DXGI_FORMAT_R8G8B8A8_UNORM: return SceneSurfaceFormat::R8G8B8A8Unorm;
    case DXGI_FORMAT_R16G16_FLOAT: return SceneSurfaceFormat::R16G16Float;
    case DXGI_FORMAT_R16G16B16A16_FLOAT: return SceneSurfaceFormat::R16G16B16A16Float;
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_D32_FLOAT: return SceneSurfaceFormat::R32Float;
    case DXGI_FORMAT_R32G32_UINT: return SceneSurfaceFormat::R32G32Uint;
    case DXGI_FORMAT_R32G32_FLOAT: return SceneSurfaceFormat::R32G32Float;
    default: return SceneSurfaceFormat::Unknown;
    }
}

void SceneSurfaceSet::BeginFrame(uint64_t frameIndex, RenderViewId viewId, uint64_t deviceGeneration) noexcept {
    Reset(AllocateRenderFrameToken(frameIndex, viewId), deviceGeneration);
}

void SceneSurfaceSet::Reset(RenderFrameToken nextFrame, uint64_t deviceGeneration) noexcept {
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

bool SceneSurfaceSet::PublishTexture(GRAPH::RenderGraphSurfaceKey key, const RenderTextureView& texture,
                                     SceneSurfaceProducer producer, SceneSurfaceAvailability availability) noexcept {
    if (!ValidateTexturePublication(key, texture, producer, availability) || TryGetPublication(key) != nullptr) {
        return false;
    }
    if (renderWidth_ == 0u || renderHeight_ == 0u) {
        renderWidth_ = texture.width;
        renderHeight_ = texture.height;
    }

    const size_t index = SurfaceIndex(key);
    textures_[index] = texture;
    depthPyramids_[index] = {};
    SceneSurfacePublication publication{};
    publication.key = key;
    publication.producer = producer;
    publication.availability = availability;
    publication.lease.key = key;
    publication.lease.frame = frame_;
    publication.lease.deviceGeneration = deviceGeneration_;
    publication.lease.publicationGeneration = AllocatePublicationGeneration();
    publication.lease.storageGeneration = AllocateStorageGeneration();
    publications_[index] = publication;
    return publication.IsAvailable();
}

bool SceneSurfaceSet::PublishTextureAlias(GRAPH::RenderGraphSurfaceKey target, GRAPH::RenderGraphSurfaceKey source,
                                          SceneSurfaceProducer producer) noexcept {
    if (!IsKnownKey(target) || !IsKnownKey(source)) {
        return false;
    }
    const SceneSurfaceSemanticDesc& targetDesc = GetSceneSurfaceSemanticDesc(target.semantic);
    const SceneSurfaceSemanticDesc& sourceDesc = GetSceneSurfaceSemanticDesc(source.semantic);
    if (producer == SceneSurfaceProducer::None || targetDesc.lifetime == SceneSurfaceLifetime::History ||
        sourceDesc.lifetime == SceneSurfaceLifetime::History ||
        (targetDesc.allowedProducerMask & SceneSurfaceProducerBit(producer)) == 0u ||
        TryGetPublication(target) != nullptr) {
        return false;
    }
    const SceneSurfacePublication* sourcePublication = TryGetPublication(source);
    const RenderTextureView* sourceTexture = TryGetTexture(source);
    if (sourcePublication == nullptr || sourceTexture == nullptr ||
        sourcePublication->availability == SceneSurfaceAvailability::Alias ||
        targetDesc.resourceKind != SceneSurfaceResourceKind::Texture2D ||
        targetDesc.format != ClassifySceneSurfaceFormat(sourceTexture->format)) {
        return false;
    }

    const size_t index = SurfaceIndex(target);
    textures_[index] = *sourceTexture;
    SceneSurfacePublication publication{};
    publication.key = target;
    publication.producer = producer;
    publication.availability = SceneSurfaceAvailability::Alias;
    publication.lease.key = target;
    publication.lease.frame = frame_;
    publication.lease.deviceGeneration = deviceGeneration_;
    publication.lease.publicationGeneration = AllocatePublicationGeneration();
    publication.lease.storageGeneration = sourcePublication->lease.storageGeneration;
    publication.aliasSourceKey = source;
    publication.aliasSourceLease = sourcePublication->lease;
    publications_[index] = publication;
    return publication.IsAvailable();
}

bool SceneSurfaceSet::PublishDepthPyramid(GRAPH::RenderGraphSurfaceKey key, const DEPTH::DepthPyramidView& pyramid,
                                          SceneSurfaceProducer producer,
                                          SceneSurfaceAvailability availability) noexcept {
    if (!IsKnownKey(key) ||
        GetSceneSurfaceSemanticDesc(key.semantic).resourceKind != SceneSurfaceResourceKind::DepthPyramid ||
        producer == SceneSurfaceProducer::None ||
        (GetSceneSurfaceSemanticDesc(key.semantic).allowedProducerMask & SceneSurfaceProducerBit(producer)) == 0u ||
        (availability != SceneSurfaceAvailability::Imported && availability != SceneSurfaceAvailability::Produced) ||
        !pyramid.publication.Matches(frame_) || !IsDepthPyramidSampleable(pyramid) ||
        (key.semantic == GRAPH::RenderGraphSemantic::CurrentDepthPyramid &&
         (pyramid.viewKind != DEPTH::DepthPyramidViewKind::CurrentFrame ||
          pyramid.sourceKind != DEPTH::DepthPyramidSourceKind::SceneDepth)) ||
        (key.semantic == GRAPH::RenderGraphSemantic::VisibilityDepthPyramid &&
         pyramid.sourceKind != DEPTH::DepthPyramidSourceKind::VisibilityPrepass &&
         pyramid.sourceKind != DEPTH::DepthPyramidSourceKind::SceneDepth) ||
        TryGetPublication(key) != nullptr) {
        return false;
    }

    const size_t index = SurfaceIndex(key);
    depthPyramids_[index] = pyramid;
    textures_[index] = {};
    SceneSurfacePublication publication{};
    publication.key = key;
    publication.producer = producer;
    publication.availability = availability;
    publication.lease.key = key;
    publication.lease.frame = frame_;
    publication.lease.deviceGeneration = deviceGeneration_;
    publication.lease.publicationGeneration = AllocatePublicationGeneration();
    publication.lease.storageGeneration = AllocateStorageGeneration();
    publications_[index] = publication;
    return publication.IsAvailable();
}

bool SceneSurfaceSet::Invalidate(GRAPH::RenderGraphSurfaceKey key) noexcept {
    if (!IsKnownKey(key)) {
        return false;
    }
    const size_t index = SurfaceIndex(key);
    const SceneSurfaceLease invalidatedLease = publications_[index].lease;
    textures_[index] = {};
    depthPyramids_[index] = {};
    publications_[index] = {};
    if (invalidatedLease.IsValid()) {
        for (size_t aliasIndex = 0u; aliasIndex < publications_.size(); ++aliasIndex) {
            const SceneSurfacePublication& candidate = publications_[aliasIndex];
            if (candidate.availability == SceneSurfaceAvailability::Alias && candidate.aliasSourceKey == key &&
                candidate.aliasSourceLease.Matches(invalidatedLease)) {
                textures_[aliasIndex] = {};
                depthPyramids_[aliasIndex] = {};
                publications_[aliasIndex] = {};
            }
        }
    }
    return true;
}

const RenderTextureView* SceneSurfaceSet::TryGetTexture(GRAPH::RenderGraphSurfaceKey key) const noexcept {
    if (!IsKnownKey(key) ||
        GetSceneSurfaceSemanticDesc(key.semantic).resourceKind != SceneSurfaceResourceKind::Texture2D) {
        return nullptr;
    }
    const size_t index = SurfaceIndex(key);
    return IsPublicationCurrent(publications_[index]) && textures_[index].IsSampleable() ? &textures_[index] : nullptr;
}

const DEPTH::DepthPyramidView* SceneSurfaceSet::TryGetDepthPyramid(GRAPH::RenderGraphSurfaceKey key) const noexcept {
    if (!IsKnownKey(key) ||
        GetSceneSurfaceSemanticDesc(key.semantic).resourceKind != SceneSurfaceResourceKind::DepthPyramid) {
        return nullptr;
    }
    const size_t index = SurfaceIndex(key);
    return IsPublicationCurrent(publications_[index]) && IsDepthPyramidSampleable(depthPyramids_[index])
               ? &depthPyramids_[index]
               : nullptr;
}

const SceneSurfacePublication* SceneSurfaceSet::TryGetPublication(GRAPH::RenderGraphSurfaceKey key) const noexcept {
    if (!IsKnownKey(key)) {
        return nullptr;
    }
    const SceneSurfacePublication& publication = publications_[SurfaceIndex(key)];
    return IsPublicationCurrent(publication) ? &publication : nullptr;
}

bool SceneSurfaceSet::IsLeaseCurrent(GRAPH::RenderGraphSurfaceKey key, SceneSurfaceLease lease) const noexcept {
    const SceneSurfacePublication* publication = TryGetPublication(key);
    return publication != nullptr && publication->lease.Matches(lease);
}

bool SceneSurfaceSet::TryDescribe(GRAPH::RenderGraphSurfaceKey key,
                                  SceneSurfaceDescriptor& outDescriptor) const noexcept {
    outDescriptor = {};
    const SceneSurfacePublication* publication = TryGetPublication(key);
    if (publication == nullptr) {
        return false;
    }
    outDescriptor.semantic = GetSceneSurfaceSemanticDesc(key.semantic);
    outDescriptor.publication = *publication;
    if (outDescriptor.semantic.resourceKind == SceneSurfaceResourceKind::Texture2D) {
        const RenderTextureView* texture = TryGetTexture(key);
        if (texture == nullptr) {
            return false;
        }
        outDescriptor.width = texture->width;
        outDescriptor.height = texture->height;
        outDescriptor.mipCount = texture->mipCount;
        outDescriptor.sampleable = texture->IsSampleable();
        return outDescriptor.sampleable;
    }
    const DEPTH::DepthPyramidView* pyramid = TryGetDepthPyramid(key);
    if (pyramid == nullptr) {
        return false;
    }
    outDescriptor.width = pyramid->width;
    outDescriptor.height = pyramid->height;
    outDescriptor.mipCount = pyramid->mipCount;
    outDescriptor.sampleable = IsDepthPyramidSampleable(*pyramid);
    return outDescriptor.sampleable;
}

bool SceneSurfaceSet::IsValid(GRAPH::RenderGraphSurfaceKey key) const noexcept {
    return TryGetPublication(key) != nullptr;
}

bool SceneSurfaceSet::HasOpaqueSurface() const noexcept {
    using Semantic = GRAPH::RenderGraphSemantic;
    using Phase = GRAPH::RenderGraphSemanticPhase;
    return TryGetTexture(Key(Semantic::SceneDepth, Phase::Imported)) != nullptr &&
           TryGetTexture(Key(Semantic::SceneColorOpaque, Phase::Opaque)) != nullptr &&
           TryGetTexture(Key(Semantic::PackedSurface, Phase::PostOpaque)) != nullptr;
}

bool SceneSurfaceSet::HasStylizedFeatureMask() const noexcept {
    return TryGetTexture(Key(GRAPH::RenderGraphSemantic::SurfaceFeatureMask,
                             GRAPH::RenderGraphSemanticPhase::PostOpaque)) != nullptr;
}

bool SceneSurfaceSet::HasStylizedOutlineStyle() const noexcept {
    using Semantic = GRAPH::RenderGraphSemantic;
    using Phase = GRAPH::RenderGraphSemanticPhase;
    const RenderTextureView* style = TryGetTexture(Key(Semantic::StylizedOutlineStyle, Phase::PostOpaque));
    const RenderTextureView* mask = TryGetTexture(Key(Semantic::SurfaceFeatureMask, Phase::PostOpaque));
    return style != nullptr && mask != nullptr && style->width == mask->width && style->height == mask->height;
}

bool SceneSurfaceSet::HasCurrentDepthHierarchy() const noexcept {
    using Semantic = GRAPH::RenderGraphSemantic;
    using Phase = GRAPH::RenderGraphSemanticPhase;
    const RenderTextureView* sceneDepth = TryGetTexture(Key(Semantic::SceneDepth, Phase::Imported));
    const RenderTextureView* packedSurface = TryGetTexture(Key(Semantic::PackedSurface, Phase::PostOpaque));
    const DEPTH::DepthPyramidView* pyramid = TryGetDepthPyramid(Key(Semantic::CurrentDepthPyramid, Phase::PostOpaque));
    return sceneDepth != nullptr && packedSurface != nullptr && pyramid != nullptr &&
           pyramid->viewKind == DEPTH::DepthPyramidViewKind::CurrentFrame &&
           pyramid->sourceKind == DEPTH::DepthPyramidSourceKind::SceneDepth &&
           pyramid->sourceWidth == sceneDepth->width && pyramid->sourceHeight == sceneDepth->height &&
           packedSurface->width == sceneDepth->width && packedSurface->height == sceneDepth->height;
}

bool SceneSurfaceSet::ValidateTexturePublication(GRAPH::RenderGraphSurfaceKey key, const RenderTextureView& texture,
                                                 SceneSurfaceProducer producer,
                                                 SceneSurfaceAvailability availability) const noexcept {
    if (!IsFrameValid() || !IsKnownKey(key) || producer == SceneSurfaceProducer::None ||
        (availability != SceneSurfaceAvailability::Imported && availability != SceneSurfaceAvailability::Produced) ||
        !texture.IsSampleable()) {
        return false;
    }
    const SceneSurfaceSemanticDesc& desc = GetSceneSurfaceSemanticDesc(key.semantic);
    return desc.resourceKind == SceneSurfaceResourceKind::Texture2D &&
           (desc.allowedProducerMask & SceneSurfaceProducerBit(producer)) != 0u &&
           (desc.format == SceneSurfaceFormat::Unknown || desc.format == ClassifySceneSurfaceFormat(texture.format)) &&
           ValidateFullResolution(texture.width, texture.height);
}

bool SceneSurfaceSet::ValidateFullResolution(uint32_t width, uint32_t height) const noexcept {
    return width != 0u && height != 0u &&
           ((renderWidth_ == 0u && renderHeight_ == 0u) || (renderWidth_ == width && renderHeight_ == height));
}

bool SceneSurfaceSet::IsPublicationCurrent(const SceneSurfacePublication& publication) const noexcept {
    if (!publication.IsAvailable() || !publication.key.Matches(publication.lease.key) ||
        !publication.lease.MatchesFrameDevice(frame_, deviceGeneration_)) {
        return false;
    }
    if (publication.availability != SceneSurfaceAvailability::Alias) {
        return true;
    }
    if (!IsKnownKey(publication.aliasSourceKey)) {
        return false;
    }
    const SceneSurfacePublication& source = publications_[SurfaceIndex(publication.aliasSourceKey)];
    return source.IsAvailable() && source.lease.Matches(publication.aliasSourceLease);
}

bool SceneSurfaceSet::IsDepthPyramidSampleable(const DEPTH::DepthPyramidView& pyramid) const noexcept {
    return pyramid.publication.Matches(frame_) && pyramid.valid &&
           pyramid.viewKind != DEPTH::DepthPyramidViewKind::Invalid &&
           pyramid.sourceKind != DEPTH::DepthPyramidSourceKind::Unknown &&
           pyramid.depthConvention == DEPTH::DepthPyramidDepthConvention::StandardD3DLessMax &&
           pyramid.pyramidSrv.ptr != 0u && pyramid.mipCount != 0u && pyramid.sourceWidth != 0u &&
           pyramid.sourceHeight != 0u && pyramid.width != 0u && pyramid.height != 0u;
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
