#pragma once

#include <span>
#include <string_view>
#include <vector>

#include "Scene/HIKARI_SceneDocument.h"

namespace HIKARI {

    class ComponentRegistry;
    class ComponentSystemPolicy;
    class SystemTypeRegistry;
    class World;

    struct RuntimeFeatureContext {
        ComponentRegistry& componentRegistry;
        SystemTypeRegistry& systemTypeRegistry;
        ComponentSystemPolicy& componentSystemPolicy;
        World& world;
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
        virtual bool Register(RuntimeFeatureContext& context) = 0;
        virtual void AppendDefaultSceneSystems(
            std::vector<SceneSystemData>&) const {}
    };

} // namespace HIKARI
