#include "HIKARI_AssetRegistry.h"

namespace HIKARI {

    bool AssetRegistry::RegisterDescriptor(std::unique_ptr<AssetDescriptor> descriptor) {
        if (!descriptor || descriptor->id.value.empty()) {
            return false;
        }

        const std::string id = descriptor->id.value;
        descriptors_[id] = std::move(descriptor);
        return true;
    }

    const AssetDescriptor* AssetRegistry::FindDescriptor(const AssetId& id) const {
        return FindDescriptor(id.value);
    }

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
    }

} // namespace HIKARI
