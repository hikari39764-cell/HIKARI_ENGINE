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
#include "Scene/Terrain/HIKARI_TerrainSurfaceFollowerComponent.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceSystem.h"
#include "Scene/Terrain/HIKARI_TerrainInteractionSourceSystem.h"

namespace HIKARI {

namespace {
class SurfaceInteractionRuntimeFeature final : public IRuntimeFeature {
public:
    std::string_view GetFeatureId() const noexcept override { return RuntimeFeatureIds::SurfaceInteraction; }

    std::string_view GetDisplayName() const noexcept override { return "Surface Interaction"; }

    std::string_view GetDescription() const noexcept override {
        return "Optional terrain contact, deformation, and sampling.";
    }

    std::span<const std::string_view> GetRequiredFeatureIds() const noexcept override {
        static constexpr std::array<std::string_view, 2> ids{RuntimeFeatureIds::Terrain, RuntimeFeatureIds::Physics};
        return ids;
    }

    bool Register(RuntimeFeatureContext& context) override {
        ComponentRegistry& components = RuntimeFeatureContextAccess::Components(context);
        SystemTypeRegistry& systems = RuntimeFeatureContextAccess::Systems(context);
        ComponentSystemPolicy& policy = RuntimeFeatureContextAccess::Policy(context);

        ComponentTypeInfo follower{};
        follower.typeName = "TerrainSurfaceFollowerComponent";
        follower.factory = []() -> std::unique_ptr<IComponent> {
            return IComponent::Create<TerrainSurfaceFollowerComponent>();
        };
        follower.runtimeApplyPolicy = ComponentRuntimeApplyPolicy::Immediate;
        follower.initializeDefaults = [](const SceneObjectData&, nlohmann::json& properties) {
            TerrainSurfaceFollowerComponent defaults{};
            defaults.Serialize(properties);
        };
        follower.presentation = ComponentTypePresentation{"Surface Follower", "Surface Interaction",
                                                          "Keeps a presentation object aligned to a sampled surface.",
                                                          std::string(RuntimeFeatureIds::SurfaceInteraction)};

        bool success = components.Register(std::move(follower));
        success = systems.Register(SystemTypeInfo{"TerrainInteractionSourceSystem",
                                                  [](const nlohmann::json&) -> std::unique_ptr<ISystem> {
                                                      return std::make_unique<TerrainInteractionSourceSystem>();
                                                  },
                                                  "Terrain Interaction Sources",
                                                  std::string(RuntimeFeatureIds::SurfaceInteraction)}) &&
                  success;
        success = systems.Register(SystemTypeInfo{"TerrainSurfaceSystem",
                                                  [](const nlohmann::json&) -> std::unique_ptr<ISystem> {
                                                      return std::make_unique<TerrainSurfaceSystem>();
                                                  },
                                                  "Surface Deformation",
                                                  std::string(RuntimeFeatureIds::SurfaceInteraction)}) &&
                  success;
        // 明示的な systems 配列で上書きされた Scene でも、対応する
        // component が存在する場合は interaction system を導入する。
        for (const ComponentSystemRule& rule :
             {ComponentSystemRule{"TerrainSurfaceComponent", "TerrainInteractionSourceSystem", 125},
              ComponentSystemRule{"TerrainSurfaceFollowerComponent", "TerrainInteractionSourceSystem", 125},
              ComponentSystemRule{"TerrainSurfaceComponent", "TerrainSurfaceSystem", 140},
              ComponentSystemRule{"TerrainSurfaceFollowerComponent", "TerrainSurfaceSystem", 140}}) {
            success = policy.Register(rule) && success;
        }
        return success;
    }

    std::span<const RuntimeSystemDefault> GetDefaultSystems() const noexcept override {
        static constexpr std::array<RuntimeSystemDefault, 2> defaults{
            RuntimeSystemDefault{"TerrainInteractionSourceSystem", true, 125},
            RuntimeSystemDefault{"TerrainSurfaceSystem", true, 140}};
        return defaults;
    }
};
} // namespace

std::unique_ptr<IRuntimeFeature> CreateSurfaceInteractionRuntimeFeature() {
    return std::make_unique<SurfaceInteractionRuntimeFeature>();
}

} // namespace HIKARI
