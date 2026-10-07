#pragma once

#include <span>
#include <string_view>

#include "HIKARI/Project/RuntimeRegistration.h"

namespace HIKARI {

struct RuntimeSystemDefault {
    std::string_view systemId{};
    bool enabled = true;
    int executionOrder = 0;
};

// Register 内で登録した組件・系统は、Feature の追加時に引擎が一度試行して一覧化する。
// そのため Register は登録以外の副作用を持たないこと。
class IRuntimeFeature {
public:
    virtual ~IRuntimeFeature() = default;

    virtual std::string_view GetFeatureId() const noexcept = 0;
    virtual std::string_view GetDisplayName() const noexcept = 0;
    virtual std::string_view GetDescription() const noexcept = 0;
    virtual std::span<const std::string_view> GetRequiredFeatureIds() const noexcept { return {}; }
    virtual std::span<const RuntimeSystemDefault> GetDefaultSystems() const noexcept { return {}; }
    virtual bool Register(RuntimeFeatureContext& context) = 0;
};

} // namespace HIKARI
