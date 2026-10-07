#include "Scene/Features/HIKARI_BuiltInRuntimeFeatures.h"

#include <array>
#include <memory>
#include <string>
#include <utility>

#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/Features/HIKARI_BuoyancyComponent.h"
#include "Scene/Features/HIKARI_BuoyancySystem.h"
#include "HIKARI/Project/RuntimeFeature.h"
#include "Scene/Features/HIKARI_RuntimeFeatureIds.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_ComponentSystemPolicy.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"

namespace HIKARI {

namespace {
class BuoyancyRuntimeFeature final : public IRuntimeFeature {
public:
    std::string_view GetFeatureId() const noexcept override { return RuntimeFeatureIds::Buoyancy; }

    std::string_view GetDisplayName() const noexcept override { return "Buoyancy"; }

    std::string_view GetDescription() const noexcept override {
        return "Optional physics response driven by water queries.";
    }

    std::span<const std::string_view> GetRequiredFeatureIds() const noexcept override {
        static constexpr std::array<std::string_view, 2> ids{RuntimeFeatureIds::Water, RuntimeFeatureIds::Physics};
        return ids;
    }

    bool Register(RuntimeFeatureContext& context) override {
        ComponentRegistry& components = RuntimeFeatureContextAccess::Components(context);
        SystemTypeRegistry& systems = RuntimeFeatureContextAccess::Systems(context);
        ComponentSystemPolicy& policy = RuntimeFeatureContextAccess::Policy(context);

        ComponentTypeInfo buoyancy{};
        buoyancy.typeName = "BuoyancyComponent";
        buoyancy.factory = []() -> std::unique_ptr<IComponent> { return IComponent::Create<BuoyancyComponent>(); };
        buoyancy.runtimeApplyPolicy = ComponentRuntimeApplyPolicy::Immediate;
        buoyancy.presentation =
            ComponentTypePresentation{"Buoyancy", "Water", "Applies buoyant force through the water query contract.",
                                      std::string(RuntimeFeatureIds::Buoyancy)};

        bool success = components.Register(std::move(buoyancy));
        success = systems.Register(SystemTypeInfo{"BuoyancySystem",
                                                  [](const nlohmann::json&) -> std::unique_ptr<ISystem> {
                                                      return std::make_unique<BuoyancySystem>();
                                                  },
                                                  "Buoyancy", std::string(RuntimeFeatureIds::Buoyancy)}) &&
                  success;
        success = policy.Register(ComponentSystemRule{"BuoyancyComponent", "BuoyancySystem", 90}) && success;
        return success;
    }
};
} // namespace

std::unique_ptr<IRuntimeFeature> CreateBuoyancyRuntimeFeature() { return std::make_unique<BuoyancyRuntimeFeature>(); }

} // namespace HIKARI
