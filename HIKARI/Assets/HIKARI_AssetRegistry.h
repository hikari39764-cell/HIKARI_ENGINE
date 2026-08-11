#pragma once

#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "HIKARI_AssetTypes.h"

namespace HIKARI {

    class AssetRegistry {
    public:
        AssetRegistry() noexcept;
        AssetRegistry(const AssetRegistry&) = delete;
        AssetRegistry& operator=(const AssetRegistry&) = delete;
        AssetRegistry(AssetRegistry&&) = delete;
        AssetRegistry& operator=(AssetRegistry&&) = delete;

        bool RegisterDescriptor(std::unique_ptr<AssetDescriptor> descriptor);

        const AssetDescriptor* FindDescriptor(const AssetId& id) const;
        const AssetDescriptor* FindDescriptor(std::string_view id) const;

        template<class TDesc>
        const TDesc* FindAs(const AssetId& id) const {
            const AssetDescriptor* descriptor = FindDescriptor(id);
            return dynamic_cast<const TDesc*>(descriptor);
        }

        std::vector<const AssetDescriptor*> CollectByType(AssetType type) const;
        void Clear();
        // Exchanges descriptor sets while preserving each registry identity.
        // Both revisions advance so long-lived render caches re-resolve data.
        void SwapContents(AssetRegistry& other) noexcept;
        uint64_t GetInstanceId() const noexcept;
        uint64_t GetRevision() const noexcept;

    private:
        void AdvanceRevision() noexcept;

        std::unordered_map<std::string, std::unique_ptr<AssetDescriptor>> descriptors_{};
        uint64_t instanceId_ = 0u;
        uint64_t revision_ = 1u;
    };

} // namespace HIKARI
