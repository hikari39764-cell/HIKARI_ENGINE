#pragma once

#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "HIKARI_AssetTypes.h"

namespace HIKARI {

    class AssetRegistry {
    public:
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

    private:
        std::unordered_map<std::string, std::unique_ptr<AssetDescriptor>> descriptors_{};
    };

} // namespace HIKARI
