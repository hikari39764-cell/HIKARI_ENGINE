#include "Scene/Features/HIKARI_BuiltInRuntimeFeatures.h"

#include <array>
#include <memory>
#include <string>
#include <utility>

#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "HIKARI/Project/RuntimeFeature.h"
#include "Scene/Features/HIKARI_RuntimeFeatureIds.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_ComponentSystemPolicy.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"
#include "Scene/Water/HIKARI_WaterSurfaceComponent.h"
#include "Scene/Water/HIKARI_WaterSurfaceSystem.h"

namespace HIKARI {

namespace {
class WaterRuntimeFeature final : public IRuntimeFeature {
public:
    std::string_view GetFeatureId() const noexcept override { return RuntimeFeatureIds::Water; }

    std::string_view GetDisplayName() const noexcept override { return "Water"; }

    std::string_view GetDescription() const noexcept override {
        return "Water surface rendering and world-space queries.";
    }

    std::span<const std::string_view> GetRequiredFeatureIds() const noexcept override {
        static constexpr std::array<std::string_view, 1> ids{RuntimeFeatureIds::Rendering};
        return ids;
    }

    bool Register(RuntimeFeatureContext& context) override {
        ComponentRegistry& components = RuntimeFeatureContextAccess::Components(context);
        SystemTypeRegistry& systems = RuntimeFeatureContextAccess::Systems(context);
        ComponentSystemPolicy& policy = RuntimeFeatureContextAccess::Policy(context);

        ComponentTypeInfo water{};
        water.typeName = "WaterSurfaceComponent";
        water.factory = []() -> std::unique_ptr<IComponent> { return IComponent::Create<WaterSurfaceComponent>(); };
        water.requiredComponents = {"ModelComponent", "ProceduralMeshComponent"};
        water.runtimeApplyPolicy = ComponentRuntimeApplyPolicy::OnEditCommit;
        water.initializeDefaults = [](const SceneObjectData&, nlohmann::json& properties) {
            WaterSurfaceComponent defaults{};
            defaults.Serialize(properties);
        };
        water.presentation =
            ComponentTypePresentation{"Water Surface", "Water", "Gerstner-driven water surface with runtime queries.",
                                      std::string(RuntimeFeatureIds::Water)};

        bool success = components.Register(std::move(water));
        success = systems.Register(SystemTypeInfo{"WaterSurfaceSystem",
                                                  [](const nlohmann::json&) -> std::unique_ptr<ISystem> {
                                                      return std::make_unique<WaterSurfaceSystem>();
                                                  },
                                                  "Water Surface", std::string(RuntimeFeatureIds::Water)}) &&
                  success;
        success = policy.Register(ComponentSystemRule{"WaterSurfaceComponent", "WaterSurfaceSystem", 95}) && success;
        return success;
    }

    std::span<const RuntimeSystemDefault> GetDefaultSystems() const noexcept override {
        static constexpr std::array<RuntimeSystemDefault, 1> defaults{{"WaterSurfaceSystem", true, 95}};
        return defaults;
    }
};
} // namespace

std::unique_ptr<IRuntimeFeature> CreateWaterRuntimeFeature() { return std::make_unique<WaterRuntimeFeature>(); }

} // namespace HIKARI
