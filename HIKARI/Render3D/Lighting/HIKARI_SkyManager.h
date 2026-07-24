#pragma once

#include <string>
#include <unordered_map>

#include "Render3D/Lighting/HIKARI_SkyAsset.h"

namespace HIKARI {

    class SkyManager {
    public:
        void RegisterAsset(const SkyAsset& asset);
        void RegisterOrUpdateAsset(const SkyAsset& asset);
        void Clear();
        bool ContainsAsset(const std::string& name) const;
        const SkyAsset* FindAsset(const std::string& name) const;

    private:
        std::unordered_map<std::string, SkyAsset> assets_{};
    };

} // namespace HIKARI
