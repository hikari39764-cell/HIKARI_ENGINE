#include "Render3D/GpuDriven/HIKARI_MassGpuScene.h"

#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <unordered_map>
#include <utility>

#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Assets/Models/HIKARI_ModelBounds.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneSurfaceRecord.h"
#include "Render3D/Runtime/HIKARI_RenderModelCache.h"
#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {
namespace {

uint64_t HashCombine(uint64_t seed, uint64_t value) noexcept {
    seed ^= value + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
    return seed != 0u ? seed : 1u;
}

uint64_t HashString(uint64_t seed, const std::string& value) noexcept {
    for (const unsigned char byte : value) {
        seed ^= static_cast<uint64_t>(byte);
        seed *= 1099511628211ull;
    }
    return seed != 0u ? seed : 1u;
}

uint64_t BuildTemplateKey(const MassRenderProxyTemplateRequest& request) noexcept {
    uint64_t key = HashCombine(0xcbf29ce484222325ull, request.typeId);
    if (request.model != nullptr) {
        key = HashString(key, request.model->id.value);
        key = HashString(key, request.model->sourcePath);
    }
    key = HashCombine(key, request.model != nullptr ? request.model->sourceRevision : 0u);
    key = HashCombine(key, request.meshIndex);
    key = HashCombine(key, request.primitiveIndex);
    key = HashCombine(key, request.materialIndex);
    key = HashCombine(key, request.receiveShadow ? 1u : 0u);
    key = HashString(key, request.clusteredGeometryPath);
    return key;
}

void AppendDirtyRange(std::vector<RUNTIME::MassRenderDirtyRange>& ranges, uint32_t first, uint32_t count) {
    if (count == 0u) {
        return;
    }
    if (!ranges.empty()) {
        RUNTIME::MassRenderDirtyRange& tail = ranges.back();
        const uint64_t tailEnd = static_cast<uint64_t>(tail.firstInstance) + tail.instanceCount;
        const uint64_t nextEnd = static_cast<uint64_t>(first) + count;
        // 履歴を跨ぐ入力は未整列。前方の区間を失わず、最後に正規化する。
        if (first >= tail.firstInstance && first <= tailEnd) {
            tail.instanceCount = static_cast<uint32_t>(
                (std::min)((std::max)(tailEnd, nextEnd) - tail.firstInstance, static_cast<uint64_t>(UINT32_MAX)));
            return;
        }
    }
    ranges.push_back({first, count});
}

void NormalizeDirtyRanges(std::vector<RUNTIME::MassRenderDirtyRange>& ranges) {
    std::sort(ranges.begin(), ranges.end(),
              [](const auto& lhs, const auto& rhs) { return lhs.firstInstance < rhs.firstInstance; });
    size_t writeIndex = 0u;
    for (const RUNTIME::MassRenderDirtyRange range : ranges) {
        if (range.instanceCount == 0u) {
            continue;
        }
        if (writeIndex == 0u) {
            ranges[writeIndex++] = range;
            continue;
        }
        RUNTIME::MassRenderDirtyRange& tail = ranges[writeIndex - 1u];
        const uint64_t tailEnd = static_cast<uint64_t>(tail.firstInstance) + tail.instanceCount;
        const uint64_t rangeEnd = static_cast<uint64_t>(range.firstInstance) + range.instanceCount;
        if (range.firstInstance <= tailEnd) {
            tail.instanceCount = static_cast<uint32_t>(
                (std::min)((std::max)(tailEnd, rangeEnd) - tail.firstInstance, static_cast<uint64_t>(UINT32_MAX)));
        } else {
            ranges[writeIndex++] = range;
        }
    }
    ranges.resize(writeIndex);
}

uint64_t BuildResolvedTemplateKey(uint64_t requestKey, const RUNTIME::SurfaceGpuSceneInstance& instance,
                                  const RUNTIME::SurfaceGpuSceneMaterialSource& material) noexcept {
    uint64_t key = requestKey;
    key = HashCombine(key, instance.flags);
    key = HashCombine(key, instance.resourceFlags);
    key = HashCombine(key, instance.geometryBackend);
    key = HashCombine(key, instance.clusterGeometrySrvDescriptorIndex);
    key = HashCombine(key, instance.clusterGeometryMetadataSrvDescriptorIndex);
    key = HashCombine(key, instance.clusterSurfaceIndex);
    key = HashCombine(key, instance.clusterRangeIndex);
    key = HashCombine(key, instance.clusterRangeCount);
    key = HashCombine(key, instance.clusterLodRangeIndex);
    key = HashCombine(key, instance.clusterLodRangeCount);
    key = HashCombine(key, material.materialKey);
    key = HashCombine(key, material.materialResource.index);
    key = HashCombine(key, material.materialResource.generation);
    key = HashCombine(key, material.materialRevision);
    key = HashCombine(key, std::bit_cast<uint32_t>(instance.boundsCenterRadius.x));
    key = HashCombine(key, std::bit_cast<uint32_t>(instance.boundsCenterRadius.y));
    key = HashCombine(key, std::bit_cast<uint32_t>(instance.boundsCenterRadius.z));
    key = HashCombine(key, std::bit_cast<uint32_t>(instance.boundsCenterRadius.w));
    return key;
}

uint32_t SaturateToUint32(size_t value) noexcept {
    return static_cast<uint32_t>((std::min)(value, static_cast<size_t>(UINT32_MAX)));
}

bool IsSupportedTemplateInstance(const RUNTIME::SurfaceGpuSceneInstance& instance) noexcept {
    using Flag = RUNTIME::SurfaceGpuSceneInstanceFlags;
    using ResourceFlag = RUNTIME::SurfaceGpuSceneResourceFlags;
    const uint32_t unsupported = static_cast<uint32_t>(Flag::Skinned) | static_cast<uint32_t>(Flag::WaterSurface) |
                                 static_cast<uint32_t>(Flag::TerrainSurface) |
                                 static_cast<uint32_t>(Flag::CameraCenteredOcean) |
                                 static_cast<uint32_t>(Flag::FoliageWind) | static_cast<uint32_t>(Flag::MaterialFx) |
                                 static_cast<uint32_t>(Flag::Transparent) | static_cast<uint32_t>(Flag::DepthAware) |
                                 static_cast<uint32_t>(Flag::AlphaMasked);
    const uint32_t requiredResources = static_cast<uint32_t>(ResourceFlag::ClusterGeometryShaderVisible) |
                                       static_cast<uint32_t>(ResourceFlag::ClusterGeometrySurfaceRange) |
                                       static_cast<uint32_t>(ResourceFlag::ClusterGeometryLodRanges);
    // Pass 所属は Mass projection 側の方針として BuildMassRecord で加える。
    // 再利用テンプレートは物理 backend / resource 互換性だけを判定する。
    return (instance.flags & unsupported) == 0u &&
           (instance.flags & static_cast<uint32_t>(Flag::ClusterMainline)) != 0u &&
           instance.geometryBackend == static_cast<uint32_t>(RUNTIME::SurfaceGeometryBackend::ClusterGeometry) &&
           (instance.resourceFlags & requiredResources) == requiredResources &&
           instance.clusterGeometrySrvDescriptorIndex != RUNTIME::kInvalidRenderSurfaceIndex &&
           instance.clusterGeometryMetadataSrvDescriptorIndex != RUNTIME::kInvalidRenderSurfaceIndex &&
           instance.clusterSurfaceIndex != RUNTIME::kInvalidRenderSurfaceIndex &&
           instance.clusterRangeIndex != RUNTIME::kInvalidRenderSurfaceIndex && instance.clusterRangeCount != 0u &&
           instance.clusterIndexCount != 0u;
}

RUNTIME::MassRenderProxyGpuRecord BuildMassRecord(const RUNTIME::MassRenderTransform& transform,
                                                  const RUNTIME::SurfaceGpuSceneInstance& proxy,
                                                  uint32_t sourceRecordIndex) noexcept {
    using Flag = RUNTIME::SurfaceGpuSceneInstanceFlags;
    const uint32_t keptFlags = static_cast<uint32_t>(Flag::ReceiveShadow) | static_cast<uint32_t>(Flag::DoubleSided) |
                               static_cast<uint32_t>(Flag::ClusterMainline) |
                               static_cast<uint32_t>(Flag::PassForwardOpaque) |
                               static_cast<uint32_t>(Flag::PassDepthPrepass);
    RUNTIME::MassRenderProxyGpuRecord record{};
    record.position = transform.position;
    record.objectIdLow = transform.objectIdLow;
    record.rotation = transform.rotation;
    record.scale = transform.scale;
    record.objectIdHigh = transform.objectIdHigh;
    record.boundsCenterRadius = {proxy.boundsCenterRadius.x, proxy.boundsCenterRadius.y, proxy.boundsCenterRadius.z,
                                 proxy.boundsCenterRadius.w};
    record.sourceRecordIndex = sourceRecordIndex;
    record.flags = (proxy.flags & keptFlags) | static_cast<uint32_t>(Flag::PassForwardOpaque) |
                   static_cast<uint32_t>(Flag::PassDepthPrepass);
    record.materialDataIndex = RUNTIME::kInvalidRenderSurfaceIndex;
    record.clusterGeometrySrvDescriptorIndex = proxy.clusterGeometrySrvDescriptorIndex;
    record.clusterGeometryMetadataSrvDescriptorIndex = proxy.clusterGeometryMetadataSrvDescriptorIndex;
    record.clusterSurfaceIndex = proxy.clusterSurfaceIndex;
    record.clusterRangeIndex = proxy.clusterRangeIndex;
    record.clusterRangeCount = proxy.clusterRangeCount;
    record.clusterIndexCount = proxy.clusterIndexCount;
    record.resourceFlags = proxy.resourceFlags;
    record.geometryBackend = proxy.geometryBackend;
    record.clusterLodRangeIndex = proxy.clusterLodRangeIndex;
    record.clusterLodRangeCount = proxy.clusterLodRangeCount;
    record.clusterSelectedLodIndex = proxy.clusterSelectedLodIndex;
    record.clusterLodFlags = proxy.clusterLodFlags;
    return record;
}

} // namespace

void MassGpuSceneSource::Reset() {
    instances.clear();
    materialSources.clear();
    dirtyHistory.clear();
    worldIdentity = 0u;
    layoutVersion = 0u;
    sourceVersion = 0u;
    materialVersion = 0u;
    publicationFrameIndex = 0u;
    publicationGeneration = 0u;
}

bool MassGpuSceneSource::CollectDirtyRangesSince(uint64_t baseSourceVersion,
                                                 std::vector<RUNTIME::MassRenderDirtyRange>& outRanges) const {
    outRanges.clear();
    if (baseSourceVersion == sourceVersion) {
        return true;
    }
    uint64_t cursor = baseSourceVersion;
    bool found = false;
    for (const MassGpuSceneVersionDelta& delta : dirtyHistory) {
        if (delta.baseSourceVersion != cursor) {
            continue;
        }
        found = true;
        for (const RUNTIME::MassRenderDirtyRange& range : delta.dirtyRanges) {
            AppendDirtyRange(outRanges, range.firstInstance, range.instanceCount);
        }
        cursor = delta.sourceVersion;
        if (cursor == sourceVersion) {
            break;
        }
    }
    if (!found || cursor != sourceVersion) {
        outRanges.clear();
        return false;
    }
    NormalizeDirtyRanges(outRanges);
    return true;
}

struct MassGpuSceneRegistry::Impl {
    static uint64_t AllocateVersion(uint64_t& counter) noexcept {
        uint64_t version = counter++;
        if (version == 0u) {
            version = counter++;
        }
        if (counter == 0u) {
            counter = 1u;
        }
        return version;
    }

    struct TemplateState {
        uint64_t requestKey = 0u;
        RUNTIME::SceneSurfaceInstance surface{};
        std::vector<GpuSceneSurfaceRecord> records{};
        std::vector<uint32_t> selectedRecords{};
        std::vector<RUNTIME::SurfaceGpuSceneInstance> instances{};
        std::vector<RUNTIME::SurfaceGpuSceneMaterialSource> materials{};
        bool valid = false;
        bool unsupported = false;
    };

    struct PendingStore {
        MassGpuSceneStoreInput input{};
        TemplateState* renderTemplate = nullptr;
        uint64_t templateKey = 0u;
    };

    struct StoreLayout {
        uint64_t typeId = 0u;
        uint64_t templateKey = 0u;
        uint64_t worldLayoutVersion = 0u;
        uint64_t worldSourceVersion = 0u;
        uint32_t firstInstance = 0u;
        uint32_t instanceCount = 0u;
    };

    TemplateState* ResolveTemplate(const MassRenderProxyTemplateRequest& request,
                                   RUNTIME::RenderModelCache& renderModelCache) {
        if (request.typeId == 0u || request.model == nullptr ||
            request.model->GetState() != ModelAsset::State::Loaded) {
            return nullptr;
        }
        TemplateState& state = templates[request.typeId];
        const uint64_t requestKey = BuildTemplateKey(request);
        if (state.requestKey != requestKey) {
            state = {};
            state.requestKey = requestKey;
        }
        const RUNTIME::RenderModelAsset* renderModel = renderModelCache.GetOrCreate(*request.model);
        if (renderModel == nullptr || !renderModel->valid || request.clusteredGeometryPath.empty()) {
            return &state;
        }
        const RUNTIME::RenderSurfaceRecord* selectedSurface = nullptr;
        for (const RUNTIME::RenderSurfaceRecord& surface : renderModel->surfaces) {
            if (surface.meshIndex == request.meshIndex && surface.primitiveIndex == request.primitiveIndex &&
                surface.materialIndex == request.materialIndex) {
                if (selectedSurface != nullptr) {
                    selectedSurface = nullptr;
                    break;
                }
                selectedSurface = &surface;
            }
        }
        if (selectedSurface == nullptr) {
            return &state;
        }

        state.surface = {};
        state.surface.objectId = {request.typeId};
        state.surface.objectLifetimeGeneration = request.model->sourceRevision;
        state.surface.objectVersion = request.model->sourceRevision;
        state.surface.surfaceIndex = selectedSurface->surfaceIndex;
        state.surface.model = request.model;
        state.surface.renderModel = renderModel;
        state.surface.surface = selectedSurface;
        state.surface.nodeIndex = selectedSurface->nodeIndex;
        state.surface.meshIndex = selectedSurface->meshIndex;
        state.surface.primitiveIndex = selectedSurface->primitiveIndex;
        state.surface.materialIndex = selectedSurface->materialIndex;
        state.surface.drawWorldMatrix = MATH::Mat4::Identity();
        state.surface.clusterWorldMatrix = MATH::Mat4::Identity();
        state.surface.hasDrawWorldMatrix = true;
        state.surface.localBounds = selectedSurface->localBounds;
        state.surface.worldBounds = selectedSurface->localBounds;
        state.surface.valid = BOUNDS::IsUsable(selectedSurface->localBounds);
        state.surface.visible = state.surface.valid;
        state.surface.isStatic = false;
        state.surface.castShadow = false;
        state.surface.receiveShadow = request.receiveShadow && selectedSurface->receiveShadowDefault;
        state.surface.hasRuntimeAnimation = false;
        state.surface.allowStaticCachedForward = false;
        state.surface.skinned = selectedSurface->IsSkinned();
        state.surface.clusteredGeometryPath = request.clusteredGeometryPath;

        state.records.clear();
        state.records.push_back(BuildGpuSceneSurfaceRecord(state.surface, 0u));
        state.selectedRecords.clear();
        state.selectedRecords.push_back(0u);
        state.instances.clear();
        state.materials.clear();
        state.valid = false;
        state.unsupported = false;
        if (state.selectedRecords.size() != 1u) {
            return &state;
        }
        (void)BuildGpuSceneInstanceList(state.records, state.selectedRecords, state.instances, state.materials);
        if (state.instances.size() != 1u || state.materials.size() != 1u) {
            return &state;
        }
        state.unsupported = !IsSupportedTemplateInstance(state.instances.front());
        state.valid = !state.unsupported;
        return &state;
    }

    void PushDelta(uint64_t baseVersion, uint64_t nextVersion, std::vector<RUNTIME::MassRenderDirtyRange> ranges) {
        source.dirtyHistory.push_back(MassGpuSceneVersionDelta{baseVersion, nextVersion, std::move(ranges)});
        if (source.dirtyHistory.size() > kMassGpuSceneDirtyHistoryCapacity) {
            source.dirtyHistory.erase(source.dirtyHistory.begin());
        }
    }

    MassGpuSceneSource source{};
    MassGpuSceneRegistryStats stats{};
    std::unordered_map<uint64_t, TemplateState> templates{};
    std::vector<PendingStore> pending{};
    std::vector<StoreLayout> layouts{};
    uint64_t pendingWorldIdentity = 0u;
    uint64_t pendingPublicationFrameIndex = 0u;
    uint64_t pendingPublicationGeneration = 0u;
    uint64_t nextLayoutVersion = 1u;
    uint64_t nextSourceVersion = 1u;
    bool pendingValid = false;
};

MassGpuSceneRegistry::MassGpuSceneRegistry() : impl_(std::make_unique<Impl>()) {}

MassGpuSceneRegistry::~MassGpuSceneRegistry() = default;

void MassGpuSceneRegistry::Clear() {
    impl_->source.Reset();
    impl_->stats = {};
    impl_->templates.clear();
    impl_->pending.clear();
    impl_->layouts.clear();
    impl_->pendingWorldIdentity = 0u;
    impl_->pendingPublicationFrameIndex = 0u;
    impl_->pendingPublicationGeneration = 0u;
    impl_->pendingValid = false;
}

void MassGpuSceneRegistry::BeginSync(uint64_t worldIdentity, uint64_t publicationFrameIndex,
                                     uint64_t publicationGeneration) {
    impl_->stats = {};
    impl_->pending.clear();
    impl_->pendingWorldIdentity = worldIdentity;
    impl_->pendingPublicationFrameIndex = publicationFrameIndex;
    impl_->pendingPublicationGeneration = publicationGeneration;
    impl_->pendingValid = worldIdentity != 0u && publicationGeneration != 0u;
}

bool MassGpuSceneRegistry::AddStore(const MassGpuSceneStoreInput& input) {
    size_t pendingInstanceCount = 0u;
    for (const Impl::PendingStore& store : impl_->pending) {
        if (store.input.proxy.typeId == input.proxy.typeId) {
            impl_->pendingValid = false;
            return false;
        }
        pendingInstanceCount += store.input.transforms.size();
    }
    if (!impl_->pendingValid || input.proxy.typeId == 0u || input.proxy.model == nullptr || input.layoutVersion == 0u ||
        input.sourceVersion == 0u || input.transforms.size() > kMassGpuSceneMaximumSourceInstanceCount ||
        pendingInstanceCount > kMassGpuSceneMaximumSourceInstanceCount - input.transforms.size()) {
        impl_->pendingValid = false;
        return false;
    }
    try {
        impl_->pending.push_back(Impl::PendingStore{input, nullptr, BuildTemplateKey(input.proxy)});
        ++impl_->stats.storeCount;
        return true;
    } catch (...) {
        impl_->pendingValid = false;
        return false;
    }
}

bool MassGpuSceneRegistry::EndSync(RUNTIME::RenderModelCache& renderModelCache) {
    if (!impl_->pendingValid) {
        return false;
    }
    try {
        impl_->templates.reserve(impl_->templates.size() + impl_->pending.size());
        for (Impl::PendingStore& store : impl_->pending) {
            store.renderTemplate = impl_->ResolveTemplate(store.input.proxy, renderModelCache);
            ++impl_->stats.templateCount;
            if (store.renderTemplate == nullptr || !store.renderTemplate->valid) {
                if (store.renderTemplate != nullptr && store.renderTemplate->unsupported) {
                    ++impl_->stats.unsupportedTemplateCount;
                } else {
                    ++impl_->stats.missingTemplateCount;
                }
                impl_->source.Reset();
                impl_->layouts.clear();
                return false;
            }
            store.templateKey = BuildResolvedTemplateKey(store.templateKey, store.renderTemplate->instances.front(),
                                                         store.renderTemplate->materials.front());
        }

        bool layoutChanged = impl_->source.worldIdentity != impl_->pendingWorldIdentity ||
                             impl_->layouts.size() != impl_->pending.size();
        size_t totalCount = 0u;
        for (size_t index = 0u; index < impl_->pending.size(); ++index) {
            const Impl::PendingStore& store = impl_->pending[index];
            if (store.input.transforms.size() > static_cast<size_t>(UINT32_MAX) - totalCount) {
                impl_->source.Reset();
                impl_->layouts.clear();
                return false;
            }
            if (!layoutChanged) {
                const Impl::StoreLayout& layout = impl_->layouts[index];
                layoutChanged = layout.typeId != store.input.proxy.typeId || layout.templateKey != store.templateKey ||
                                layout.worldLayoutVersion != store.input.layoutVersion ||
                                layout.firstInstance != totalCount ||
                                layout.instanceCount != store.input.transforms.size();
                if (!layoutChanged && layout.worldSourceVersion != store.input.sourceVersion &&
                    (store.input.dirtyBaseSourceVersion != layout.worldSourceVersion ||
                     store.input.dirtyRanges.empty())) {
                    layoutChanged = true;
                }
            }
            totalCount += store.input.transforms.size();
        }

        if (layoutChanged) {
            impl_->source.instances.resize(totalCount);
            impl_->source.materialSources.resize(impl_->pending.size());
            impl_->layouts.clear();
            impl_->layouts.reserve(impl_->pending.size());
            uint32_t cursor = 0u;
            for (uint32_t storeIndex = 0u; storeIndex < impl_->pending.size(); ++storeIndex) {
                const Impl::PendingStore& store = impl_->pending[storeIndex];
                const RUNTIME::SurfaceGpuSceneInstance& renderTemplate = store.renderTemplate->instances.front();
                for (size_t localIndex = 0u; localIndex < store.input.transforms.size(); ++localIndex) {
                    impl_->source.instances[cursor + localIndex] =
                        BuildMassRecord(store.input.transforms[localIndex], renderTemplate, storeIndex);
                }
                RUNTIME::SurfaceGpuSceneMaterialSource material = store.renderTemplate->materials.front();
                material.sourceRecordIndex = storeIndex;
                material.localGpuSceneInstanceIndex = cursor;
                impl_->source.materialSources[storeIndex] = material;
                impl_->layouts.push_back(Impl::StoreLayout{store.input.proxy.typeId, store.templateKey,
                                                           store.input.layoutVersion, store.input.sourceVersion, cursor,
                                                           SaturateToUint32(store.input.transforms.size())});
                cursor += SaturateToUint32(store.input.transforms.size());
            }
            const uint64_t baseVersion = impl_->source.sourceVersion;
            impl_->source.layoutVersion = Impl::AllocateVersion(impl_->nextLayoutVersion);
            impl_->source.sourceVersion = Impl::AllocateVersion(impl_->nextSourceVersion);
            impl_->source.materialVersion = impl_->source.layoutVersion;
            impl_->source.worldIdentity = impl_->pendingWorldIdentity;
            impl_->source.dirtyHistory.clear();
            std::vector<RUNTIME::MassRenderDirtyRange> fullRange{};
            if (totalCount != 0u) {
                fullRange.push_back({0u, SaturateToUint32(totalCount)});
            }
            impl_->PushDelta(baseVersion, impl_->source.sourceVersion, std::move(fullRange));
            ++impl_->stats.fullRebuildCount;
        } else {
            std::vector<RUNTIME::MassRenderDirtyRange> changed{};
            for (size_t storeIndex = 0u; storeIndex < impl_->pending.size(); ++storeIndex) {
                const Impl::PendingStore& store = impl_->pending[storeIndex];
                Impl::StoreLayout& layout = impl_->layouts[storeIndex];
                if (layout.worldSourceVersion == store.input.sourceVersion) {
                    continue;
                }
                for (const RUNTIME::MassRenderDirtyRange& range : store.input.dirtyRanges) {
                    if (range.instanceCount == 0u || range.firstInstance >= layout.instanceCount ||
                        range.instanceCount > layout.instanceCount - range.firstInstance) {
                        impl_->source.Reset();
                        impl_->layouts.clear();
                        return false;
                    }
                    const uint32_t globalFirst = layout.firstInstance + range.firstInstance;
                    const RUNTIME::SurfaceGpuSceneInstance& renderTemplate = store.renderTemplate->instances.front();
                    for (uint32_t localOffset = 0u; localOffset < range.instanceCount; ++localOffset) {
                        const uint32_t localIndex = range.firstInstance + localOffset;
                        impl_->source.instances[globalFirst + localOffset] = BuildMassRecord(
                            store.input.transforms[localIndex], renderTemplate, static_cast<uint32_t>(storeIndex));
                    }
                    AppendDirtyRange(changed, globalFirst, range.instanceCount);
                }
                layout.worldSourceVersion = store.input.sourceVersion;
            }
            if (!changed.empty()) {
                const uint64_t baseVersion = impl_->source.sourceVersion;
                impl_->source.sourceVersion = Impl::AllocateVersion(impl_->nextSourceVersion);
                impl_->PushDelta(baseVersion, impl_->source.sourceVersion, std::move(changed));
                ++impl_->stats.incrementalPatchCount;
            } else {
                ++impl_->stats.stableReuseCount;
            }
        }

        impl_->stats.sourceInstanceCount = SaturateToUint32(impl_->source.instances.size());
        impl_->source.publicationFrameIndex = impl_->pendingPublicationFrameIndex;
        impl_->source.publicationGeneration = impl_->pendingPublicationGeneration;
        if (!impl_->source.dirtyHistory.empty()) {
            impl_->stats.dirtyRangeCount = SaturateToUint32(impl_->source.dirtyHistory.back().dirtyRanges.size());
        }
        return true;
    } catch (...) {
        impl_->source.Reset();
        impl_->layouts.clear();
        return false;
    }
}

const MassGpuSceneSource& MassGpuSceneRegistry::GetSource() const noexcept { return impl_->source; }

const MassGpuSceneRegistryStats& MassGpuSceneRegistry::GetStats() const noexcept { return impl_->stats; }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
