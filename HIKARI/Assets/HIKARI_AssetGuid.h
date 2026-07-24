#pragma once

#include <string>
#include <string_view>

namespace HIKARI {

    struct AssetGuid {
        std::string value;

        bool IsValid() const {
            return !value.empty();
        }

        bool operator==(const AssetGuid& rhs) const {
            return value == rhs.value;
        }

        bool operator!=(const AssetGuid& rhs) const {
            return !(*this == rhs);
        }
    };

    AssetGuid GenerateAssetGuid();
    bool IsValidAssetGuid(std::string_view text);

} // namespace HIKARI
