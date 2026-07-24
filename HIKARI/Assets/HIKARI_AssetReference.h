#pragma once

#include <string>

#include "HIKARI_AssetGuid.h"

namespace HIKARI {

    struct AssetReference {
        AssetGuid guid{};
        std::string displayName{};
        std::string cachedPath{};

        bool IsValid() const {
            return guid.IsValid();
        }
    };

} // namespace HIKARI
