#include "Scene/HIKARI_WorldMassStore.h"
#include "Core/Numeric/HIKARI_NonZero.h"

#include <algorithm>
#include <cstring>
#include <iterator>
#include <limits>

namespace HIKARI {
namespace {

constexpr uint32_t kSupportedMassRenderFlags = static_cast<uint32_t>(GameProjectWorldMassRenderFlags::ReceiveShadow |
                                                                     GameProjectWorldMassRenderFlags::SkipRayTracing);

size_t FixedTextLength(const char* text, size_t capacity) noexcept {
    if (text == nullptr) {
        return capacity;
    }
    size_t length = 0u;
    while (length < capacity && text[length] != '\0') {
        ++length;
    }
    return length;
}

bool FixedTextTailIsZero(const char* text, size_t length, size_t capacity) noexcept {
    if (text == nullptr || length >= capacity) {
        return false;
    }
    for (size_t index = length; index < capacity; ++index) {
        if (text[index] != '\0') {
            return false;
        }
    }
    return true;
}

} // namespace

bool IsValidWorldMassSchema(const WorldMassSchema& schema) noexcept {
    return schema.typeId.IsValid() && schema.layoutHash != 0u && schema.schemaVersion != 0u && schema.podSize != 0u &&
           schema.podSize <= kWorldMassMaximumPodBytes && schema.podAlignment != 0u &&
           schema.podAlignment <= alignof(std::max_align_t) && (schema.podAlignment & (schema.podAlignment - 1u)) == 0u;
}

bool IsSameWorldMassSchema(const WorldMassSchema& lhs, const WorldMassSchema& rhs) noexcept {
    return lhs.typeId == rhs.typeId && lhs.layoutHash == rhs.layoutHash && lhs.schemaVersion == rhs.schemaVersion &&
           lhs.podSize == rhs.podSize && lhs.podAlignment == rhs.podAlignment;
}

bool IsValidWorldMassRenderProxy(const GameProjectWorldMassRenderProxyDesc& descriptor) noexcept {
    if (descriptor.structSize != kGameProjectWorldMassRenderProxyDescV1Size ||
        descriptor.version != kGameProjectWorldMassVersion || !descriptor.typeId.IsValid() ||
        descriptor.layoutHash == 0u || descriptor.schemaVersion == 0u ||
        (descriptor.flags & ~kSupportedMassRenderFlags) != 0u || descriptor.reserved0 != 0u ||
        NUMERIC::AnyNonZero(descriptor.reserved)) {
        return false;
    }
    const size_t guidLength = FixedTextLength(descriptor.modelAssetGuid, std::size(descriptor.modelAssetGuid));
    if (!FixedTextTailIsZero(descriptor.modelAssetGuid, guidLength, std::size(descriptor.modelAssetGuid))) {
        return false;
    }
    switch (descriptor.geometry) {
    case GameProjectWorldMassRenderGeometry::ModelAsset: return guidLength != 0u;
    case GameProjectWorldMassRenderGeometry::BuiltInBox:
    case GameProjectWorldMassRenderGeometry::BuiltInSphere: return guidLength == 0u;
    default: return false;
    }
}

bool IsSameWorldMassRenderProxy(const GameProjectWorldMassRenderProxyDesc& lhs,
                                const GameProjectWorldMassRenderProxyDesc& rhs) noexcept {
    return std::memcmp(&lhs, &rhs, sizeof(lhs)) == 0;
}

bool WorldMassSchemaRegistry::Register(WorldMassSchema schema) noexcept {
    if (!IsValidWorldMassSchema(schema)) {
        return false;
    }
    try {
        return schemas_.emplace(schema.typeId.value, schema).second;
    } catch (...) {
        return false;
    }
}

bool WorldMassSchemaRegistry::RegisterRenderProxy(const GameProjectWorldMassRenderProxyDesc& descriptor) noexcept {
    const WorldMassSchema* schema = Find(descriptor.typeId);
    if (schema == nullptr || !IsValidWorldMassRenderProxy(descriptor) || schema->layoutHash != descriptor.layoutHash ||
        schema->schemaVersion != descriptor.schemaVersion) {
        return false;
    }
    try {
        return renderProxies_.emplace(descriptor.typeId.value, descriptor).second;
    } catch (...) {
        return false;
    }
}

void WorldMassSchemaRegistry::Clear() noexcept {
    renderProxies_.clear();
    schemas_.clear();
}

const WorldMassSchema* WorldMassSchemaRegistry::Find(ComponentTypeId typeId) const noexcept {
    if (!typeId.IsValid()) {
        return nullptr;
    }
    const auto found = schemas_.find(typeId.value);
    return found != schemas_.end() ? &found->second : nullptr;
}

const GameProjectWorldMassRenderProxyDesc* WorldMassSchemaRegistry::FindRenderProxy(
    ComponentTypeId typeId) const noexcept {
    if (!typeId.IsValid()) {
        return nullptr;
    }
    const auto found = renderProxies_.find(typeId.value);
    return found != renderProxies_.end() ? &found->second : nullptr;
}

WorldMassRenderInstance WorldMassStore::BuildRenderInstance(RuntimeObjectHandle object,
                                                            const GameProjectFloat3& position,
                                                            const GameProjectQuaternion& rotation,
                                                            const GameProjectFloat3& scale) noexcept {
    const uint64_t identity = object.ToValue();
    return WorldMassRenderInstance{{position.x, position.y, position.z},
                                   static_cast<uint32_t>(identity & 0xffffffffu),
                                   {rotation.x, rotation.y, rotation.z, rotation.w},
                                   {scale.x, scale.y, scale.z},
                                   static_cast<uint32_t>(identity >> 32u)};
}

void WorldMassStore::MarkRenderDirty(Store& store, size_t rowIndex) noexcept {
    if (!store.hasRenderProxy || rowIndex >= store.renderInstances.size() ||
        rowIndex > (std::numeric_limits<uint32_t>::max)()) {
        return;
    }
    const bool hadDirty = store.renderDirtyAll || !store.renderDirtyIndices.empty();
    if (!hadDirty) {
        store.renderDirtyBaseSourceVersion = store.renderSourceVersion;
    }
    if (store.renderDirtyAll) {
        return;
    }
    if (rowIndex >= store.renderDirtyListPositions.size()) {
        store.renderDirtyAll = true;
        store.renderDirtyIndices.clear();
        store.renderDirtyRanges.clear();
        std::fill(store.renderDirtyListPositions.begin(), store.renderDirtyListPositions.end(), UINT32_MAX);
        return;
    }
    if (store.renderDirtyListPositions[rowIndex] != UINT32_MAX) {
        return;
    }
    try {
        const uint32_t compactIndex = static_cast<uint32_t>(rowIndex);
        if (!store.renderDirtyIndices.empty() && compactIndex < store.renderDirtyIndices.back()) {
            store.renderDirtyIndicesSorted = false;
        }
        store.renderDirtyIndices.push_back(compactIndex);
        store.renderDirtyListPositions[rowIndex] = static_cast<uint32_t>(store.renderDirtyIndices.size() - 1u);
    } catch (...) {
        store.renderDirtyAll = true;
        store.renderDirtyIndices.clear();
        store.renderDirtyRanges.clear();
        std::fill(store.renderDirtyListPositions.begin(), store.renderDirtyListPositions.end(), UINT32_MAX);
    }
}

void WorldMassStore::RemoveRenderDirty(Store& store, size_t rowIndex) noexcept {
    if (store.renderDirtyAll || rowIndex >= store.renderDirtyListPositions.size()) {
        return;
    }
    const uint32_t position = store.renderDirtyListPositions[rowIndex];
    if (position == UINT32_MAX || position >= store.renderDirtyIndices.size()) {
        return;
    }
    const uint32_t movedDirtyIndex = store.renderDirtyIndices.back();
    store.renderDirtyIndices[position] = movedDirtyIndex;
    store.renderDirtyIndices.pop_back();
    store.renderDirtyListPositions[rowIndex] = UINT32_MAX;
    if (position < store.renderDirtyIndices.size() && movedDirtyIndex < store.renderDirtyListPositions.size()) {
        store.renderDirtyListPositions[movedDirtyIndex] = position;
        store.renderDirtyIndicesSorted = false;
    }
}

void WorldMassStore::AdvanceRenderSourceVersion(Store& store) noexcept {
    if (!store.hasRenderProxy) {
        return;
    }
    ++store.renderSourceVersion;
    if (store.renderSourceVersion == 0u) {
        store.renderSourceVersion = 1u;
        NUMERIC::AdvanceNonZero(store.renderLayoutVersion);
        store.renderDirtyBaseSourceVersion = 0u;
    }
}

void WorldMassStore::AdvanceRenderLayoutVersion(Store& store) noexcept {
    if (!store.hasRenderProxy) {
        return;
    }
    NUMERIC::AdvanceNonZero(store.renderLayoutVersion);
    AdvanceRenderSourceVersion(store);
}

void WorldMassStore::RebuildRenderDirtyRanges(Store& store) {
    store.renderDirtyRanges.clear();
    if (store.renderDirtyAll) {
        if (!store.renderInstances.empty()) {
            store.renderDirtyRanges.push_back(
                {0u, static_cast<uint32_t>((std::min)(store.renderInstances.size(), static_cast<size_t>(UINT32_MAX)))});
        }
        return;
    }
    if (store.renderDirtyIndices.empty()) {
        return;
    }
    if (!store.renderDirtyIndicesSorted) {
        std::sort(store.renderDirtyIndices.begin(), store.renderDirtyIndices.end());
        store.renderDirtyIndicesSorted = true;
    }
    store.renderDirtyRanges.reserve(store.renderDirtyIndices.size());
    uint32_t rangeFirst = store.renderDirtyIndices.front();
    uint32_t rangeEnd = rangeFirst + 1u;
    for (size_t index = 1u; index < store.renderDirtyIndices.size(); ++index) {
        const uint32_t dirtyIndex = store.renderDirtyIndices[index];
        if (dirtyIndex <= rangeEnd) {
            rangeEnd = (std::max)(rangeEnd, dirtyIndex + 1u);
            continue;
        }
        store.renderDirtyRanges.push_back({rangeFirst, rangeEnd - rangeFirst});
        rangeFirst = dirtyIndex;
        rangeEnd = dirtyIndex + 1u;
    }
    store.renderDirtyRanges.push_back({rangeFirst, rangeEnd - rangeFirst});
}

void WorldMassStore::ClearAcknowledgedRenderDirty(Store& store) noexcept {
    store.renderDirtyIndices.clear();
    store.renderDirtyRanges.clear();
    std::fill(store.renderDirtyListPositions.begin(), store.renderDirtyListPositions.end(), UINT32_MAX);
    store.renderDirtyIndicesSorted = true;
    store.renderDirtyAll = false;
    store.renderDirtyBaseSourceVersion = store.renderSourceVersion;
}

bool WorldMassStore::HasChangedTransform(const GameProjectFloat3& position, const GameProjectQuaternion& rotation,
                                         const GameProjectFloat3& scale,
                                         const WorldMassRenderInstance& before) noexcept {
    return std::memcmp(&position, &before.position, sizeof(position)) != 0 ||
           std::memcmp(&rotation, &before.rotation, sizeof(rotation)) != 0 ||
           std::memcmp(&scale, &before.scale, sizeof(scale)) != 0;
}

bool WorldMassStore::PublishRenderStores(uint64_t worldIdentity, uint64_t frameIndex,
                                         std::span<const WorldMassRenderStoreView> extraViews, void* context,
                                         RenderPublicationVisitorFn visitor,
                                         WorldMassRenderPublicationReceipt& outReceipt) noexcept {
    outReceipt = {};
    if (worldIdentity == 0u || visitor == nullptr || renderPublicationInProgress_) {
        return false;
    }
    renderPublicationInProgress_ = true;
    struct PublicationScope final {
        bool& active;
        ~PublicationScope() { active = false; }
    } publicationScope{renderPublicationInProgress_};
    uint64_t generation = nextRenderPublicationGeneration_++;
    if (generation == 0u) {
        generation = nextRenderPublicationGeneration_++;
    }
    if (nextRenderPublicationGeneration_ == 0u) {
        nextRenderPublicationGeneration_ = 1u;
    }
    const WorldMassRenderPublicationReceipt candidateReceipt{worldIdentity, generation, frameIndex};

    try {
        renderPublicationViews_.clear();
        renderPublicationViews_.reserve(renderStoreTypeOrder_.size() + extraViews.size());
        for (const uint64_t typeValue : renderStoreTypeOrder_) {
            const auto found = stores_.find(typeValue);
            if (found == stores_.end() || !found->second.hasRenderProxy) {
                return false;
            }
            RebuildRenderDirtyRanges(found->second);
            Store& store = found->second;
            renderPublicationViews_.push_back(WorldMassRenderStoreView{
                store.schema.typeId, &store.renderProxy, store.renderInstances, store.renderDirtyRanges,
                store.renderLayoutVersion, store.renderSourceVersion, store.renderDirtyBaseSourceVersion});
        }
        renderPublicationViews_.insert(renderPublicationViews_.end(), extraViews.begin(), extraViews.end());
        if (!visitor(context, candidateReceipt, renderPublicationViews_)) {
            return false;
        }
        for (size_t viewIndex = 0u; viewIndex < renderStoreTypeOrder_.size(); ++viewIndex) {
            const uint64_t typeValue = renderStoreTypeOrder_[viewIndex];
            Store& store = stores_.find(typeValue)->second;
            const WorldMassRenderStoreView& publishedView = renderPublicationViews_[viewIndex];
            if (publishedView.typeId != store.schema.typeId ||
                publishedView.layoutVersion != store.renderLayoutVersion ||
                publishedView.sourceVersion != store.renderSourceVersion ||
                publishedView.instances.data() != store.renderInstances.data() ||
                publishedView.instances.size() != store.renderInstances.size()) {
                return false;
            }
            store.publishedSourceVersion = store.renderSourceVersion;
            store.publishedGeneration = generation;
        }
    } catch (...) {
        return false;
    }

    activeRenderPublication_ = candidateReceipt;
    outReceipt = activeRenderPublication_;
    return true;
}

bool WorldMassStore::AcknowledgeRenderStores(const WorldMassRenderPublicationReceipt& receipt) noexcept {
    if (!receipt.IsValid() || receipt.worldIdentity != activeRenderPublication_.worldIdentity ||
        receipt.generation != activeRenderPublication_.generation ||
        receipt.frameIndex != activeRenderPublication_.frameIndex) {
        return false;
    }
    for (const uint64_t typeValue : renderStoreTypeOrder_) {
        const auto found = stores_.find(typeValue);
        if (found == stores_.end() || found->second.publishedGeneration != receipt.generation ||
            found->second.publishedSourceVersion != found->second.renderSourceVersion) {
            return false;
        }
    }
    for (const uint64_t typeValue : renderStoreTypeOrder_) {
        ClearAcknowledgedRenderDirty(stores_.find(typeValue)->second);
    }
    activeRenderPublication_ = {};
    return true;
}

} // namespace HIKARI
