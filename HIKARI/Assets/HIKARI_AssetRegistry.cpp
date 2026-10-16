#include "HIKARI_AssetRegistry.h"
#include "Core/Numeric/HIKARI_NonZero.h"

#include <atomic>

namespace HIKARI {
namespace {

uint64_t AllocateAssetRegistryInstanceId() noexcept {
    static std::atomic_uint64_t nextId{1u};
    uint64_t id = nextId.fetch_add(1u, std::memory_order_relaxed);
    if (id == 0u) {
        id = nextId.fetch_add(1u, std::memory_order_relaxed);
    }
    return id;
}

} // namespace

AssetRegistry::AssetRegistry() noexcept : instanceId_(AllocateAssetRegistryInstanceId()) {}

bool AssetRegistry::RegisterDescriptor(std::unique_ptr<AssetDescriptor> descriptor) {
    if (!descriptor || descriptor->id.value.empty()) {
        return false;
    }

    const std::string id = descriptor->id.value;
    if (descriptors_.contains(id)) {
        return false;
    }
    descriptors_.emplace(id, std::move(descriptor));
    NUMERIC::AdvanceNonZero(revision_);
    return true;
}

const AssetDescriptor* AssetRegistry::FindDescriptor(const AssetId& id) const { return FindDescriptor(id.value); }

const AssetDescriptor* AssetRegistry::FindDescriptor(std::string_view id) const {
    const auto it = descriptors_.find(std::string(id));
    if (it == descriptors_.end()) {
        return nullptr;
    }
    return it->second.get();
}

std::vector<const AssetDescriptor*> AssetRegistry::CollectByType(AssetType type) const {
    std::vector<const AssetDescriptor*> result;
    result.reserve(descriptors_.size());
    for (const auto& [_, descriptor] : descriptors_) {
        if (descriptor && descriptor->type == type) {
            result.push_back(descriptor.get());
        }
    }
    return result;
}

void AssetRegistry::Clear() {
    descriptors_.clear();
    NUMERIC::AdvanceNonZero(revision_);
}

void AssetRegistry::SwapContents(AssetRegistry& other) noexcept {
    if (this == &other) {
        return;
    }
    descriptors_.swap(other.descriptors_);
    NUMERIC::AdvanceNonZero(revision_);
    NUMERIC::AdvanceNonZero(other.revision_);
}

uint64_t AssetRegistry::GetInstanceId() const noexcept { return instanceId_; }

uint64_t AssetRegistry::GetRevision() const noexcept { return revision_; }

} // namespace HIKARI
