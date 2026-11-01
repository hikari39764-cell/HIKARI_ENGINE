#pragma once

#include <string>
#include <string_view>

namespace HIKARI {

struct AssetGuid {
    std::string value;

    bool IsValid() const { return !value.empty(); }

    bool operator==(const AssetGuid& rhs) const { return value == rhs.value; }

    bool operator!=(const AssetGuid& rhs) const { return !(*this == rhs); }
};

// 文字列で持つ資産 ID。モデルや資産記述子が自分の ID に使う。
struct AssetId {
    std::string value;

    bool operator==(const AssetId& rhs) const { return value == rhs.value; }

    bool operator!=(const AssetId& rhs) const { return !(*this == rhs); }
};

AssetGuid GenerateAssetGuid();
bool IsValidAssetGuid(std::string_view text);

} // namespace HIKARI
