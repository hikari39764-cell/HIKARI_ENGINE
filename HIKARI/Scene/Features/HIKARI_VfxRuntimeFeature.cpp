#include "Scene/Features/HIKARI_VfxRuntimeFeature.h"

#include <array>
#include <memory>

#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/Components/Rendering/Vfx/HIKARI_GhostPossessionComponent.h"
#include "Scene/Components/Rendering/Vfx/HIKARI_VfxPlayerComponent.h"
#include "Scene/Components/Rendering/Vfx/HIKARI_VfxPlayerComponentAuthoring.h"
#include "Scene/Features/HIKARI_RuntimeFeature.h"
#include "Scene/Features/HIKARI_RuntimeFeatureIds.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_ComponentSystemPolicy.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"
#include "Scene/Vfx/HIKARI_GhostPossessionSystem.h"
#include "Scene/Vfx/HIKARI_VfxPlayerSystem.h"

namespace HIKARI {
    namespace {
        class VfxRuntimeFeature final : public IRuntimeFeature {
        public:
            std::string_view GetFeatureId() const noexcept override {
                return RuntimeFeatureIds::Vfx;
            }

            std::string_view GetDisplayName() const noexcept override {
                return "VFX";
            }

            std::string_view GetDescription() const noexcept override {
                return "Native event and variable driven GPU particle playback.";
            }

            std::span<const std::string_view>
                GetRequiredFeatureIds() const noexcept override {
                static constexpr std::array<std::string_view, 1> ids{
                    RuntimeFeatureIds::Rendering
                };
                return ids;
            }

            std::span<const std::string_view>
                GetComponentTypeNames() const noexcept override {
                static constexpr std::array<std::string_view, 2> names{
                    "VfxPlayerComponent",
                    "GhostPossessionComponent"
                };
                return names;
            }

            std::span<const std::string_view>
                GetSystemIds() const noexcept override {
                static constexpr std::array<std::string_view, 2> ids{
                    "GhostPossessionSystem",
                    "VfxPlayerSystem"
                };
                return ids;
            }

            bool Register(RuntimeFeatureContext& context) override {
                ComponentRegistry& componentRegistry =
                    RuntimeFeatureContextAccess::Components(context);
                SystemTypeRegistry& systemRegistry =
                    RuntimeFeatureContextAccess::Systems(context);
                ComponentSystemPolicy& componentSystemPolicy =
                    RuntimeFeatureContextAccess::Policy(context);

                ComponentTypeInfo player{};
                player.typeName = "VfxPlayerComponent";
                player.factory = []() -> std::unique_ptr<IComponent> {
                    return std::make_unique<VfxPlayerComponent>();
                };
                player.initializeDefaults = [](
                    const SceneObjectData&,
                    nlohmann::json& properties) {
                    properties["enabled"] = true;
                    properties["slots"] = nlohmann::json::array();
                };
                player.presentation = ComponentTypePresentation{
                    "VFX Player",
                    "Rendering/VFX",
                    "Plays one or more reusable VFX assets from runtime events and variables.",
                    "VFX"
                };
                player.authoringCommands =
                    BuildVfxPlayerComponentAuthoringCommands();
                player.validateAuthoring =
                    ValidateVfxPlayerComponentAuthoring;

                ComponentTypeInfo ghost{};
                ghost.typeName = "GhostPossessionComponent";
                ghost.factory = []() -> std::unique_ptr<IComponent> {
                    return std::make_unique<GhostPossessionComponent>();
                };
                ghost.requiredComponents = {
                    "ModelComponent",
                    "MaterialFxComponent"
                };
                ghost.optionalComponents = {
                    "VfxPlayerComponent"
                };
                ghost.initializeDefaults = [](
                    const SceneObjectData&,
                    nlohmann::json& properties) {
                    GhostPossessionComponent defaults{};
                    defaults.Serialize(properties);
                };
                ghost.presentation = ComponentTypePresentation{
                    "Ghost Possession",
                    "Rendering/VFX",
                    "Blends a reusable ghost surface and optional enter, loop, and exit VFX slots.",
                    "VFX"
                };

                bool success = componentRegistry.Register(
                    std::move(player));
                success = componentRegistry.Register(
                    std::move(ghost)) && success;
                success = systemRegistry.Register(SystemTypeInfo{
                    "GhostPossessionSystem",
                    [](const nlohmann::json&) -> std::unique_ptr<ISystem> {
                        return std::make_unique<GhostPossessionSystem>();
                    },
                    "Ghost Possession",
                    "VFX"
                }) && success;
                success = systemRegistry.Register(SystemTypeInfo{
                    "VfxPlayerSystem",
                    [](const nlohmann::json&) -> std::unique_ptr<ISystem> {
                        return std::make_unique<VfxPlayerSystem>();
                    },
                    "VFX Player",
                    "VFX"
                }) && success;
                success = componentSystemPolicy.Register(
                    ComponentSystemRule{
                        "GhostPossessionComponent",
                        "GhostPossessionSystem",
                        170
                    }) && success;
                success = componentSystemPolicy.Register(
                    ComponentSystemRule{
                        "VfxPlayerComponent",
                        "VfxPlayerSystem",
                        180
                    }) && success;
                return success;
            }
        };
    }

    std::unique_ptr<IRuntimeFeature> CreateVfxRuntimeFeature() {
        return std::make_unique<VfxRuntimeFeature>();
    }

} // namespace HIKARI
