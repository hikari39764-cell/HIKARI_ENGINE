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

    class IRuntimeFeature {
    public:
        virtual ~IRuntimeFeature() = default;

        virtual std::string_view GetFeatureId() const noexcept = 0;
        virtual std::string_view GetDisplayName() const noexcept = 0;
        virtual std::string_view GetDescription() const noexcept = 0;
        virtual std::span<const std::string_view>
            GetRequiredFeatureIds() const noexcept { return {}; }
        virtual std::span<const std::string_view>
            GetComponentTypeNames() const noexcept { return {}; }
        virtual std::span<const std::string_view>
            GetSystemIds() const noexcept { return {}; }
        virtual std::span<const RuntimeSystemDefault>
            GetDefaultSystems() const noexcept { return {}; }
        virtual bool Register(RuntimeFeatureContext& context) = 0;
    };

} // namespace HIKARI
