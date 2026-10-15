#include "Scene/Features/HIKARI_BuiltInRuntimeFeatures.h"

#include <array>
#include <memory>
#include <string>

#include "Scene/Features/HIKARI_RuntimeFeatureContextAccess.h"
#include "Scene/Components/Rendering/Vfx/HIKARI_VfxPlayerComponent.h"
#include "Scene/Components/Rendering/Vfx/HIKARI_VfxPlayerComponentAuthoring.h"
#include "HIKARI/Project/RuntimeFeature.h"
#include "Scene/Features/HIKARI_RuntimeFeatureIds.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_ComponentSystemPolicy.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"
#include "Scene/Vfx/HIKARI_VfxPlayerSystem.h"

namespace HIKARI {
namespace {
class VfxRuntimeFeature final : public IRuntimeFeature {
public:
    std::string_view GetFeatureId() const noexcept override { return RuntimeFeatureIds::Vfx; }

    std::string_view GetDisplayName() const noexcept override { return "VFX"; }

    std::string_view GetDescription() const noexcept override {
        return "Native event and variable driven GPU particle playback.";
    }

    std::span<const std::string_view> GetRequiredFeatureIds() const noexcept override {
        static constexpr std::array<std::string_view, 1> ids{RuntimeFeatureIds::Rendering};
        return ids;
    }

    bool Register(RuntimeFeatureContext& context) override {
        ComponentRegistry& componentRegistry = RuntimeFeatureContextAccess::Components(context);
        SystemTypeRegistry& systemRegistry = RuntimeFeatureContextAccess::Systems(context);
        ComponentSystemPolicy& componentSystemPolicy = RuntimeFeatureContextAccess::Policy(context);

        ComponentTypeInfo player{};
        player.typeName = "VfxPlayerComponent";
        player.factory = []() -> std::unique_ptr<IComponent> { return IComponent::Create<VfxPlayerComponent>(); };
        player.initializeDefaults = [](const SceneObjectData&, nlohmann::json& properties) {
            properties["enabled"] = true;
            properties["slots"] = nlohmann::json::array();
        };
        player.presentation = ComponentTypePresentation{
            "VFX Player", "Rendering/VFX", "Plays one or more reusable VFX assets from runtime events and variables.",
            std::string(RuntimeFeatureIds::Vfx)};
        player.authoringCommands = BuildVfxPlayerComponentAuthoringCommands();
        player.validateAuthoring = ValidateVfxPlayerComponentAuthoring;

        bool success = componentRegistry.Register(std::move(player));
        success = systemRegistry.Register(SystemTypeInfo{"VfxPlayerSystem",
                                                         [](const nlohmann::json&) -> std::unique_ptr<ISystem> {
                                                             return std::make_unique<VfxPlayerSystem>();
                                                         },
                                                         "VFX Player", std::string(RuntimeFeatureIds::Vfx)}) &&
                  success;
        success = componentSystemPolicy.Register(ComponentSystemRule{"VfxPlayerComponent", "VfxPlayerSystem", 180}) &&
                  success;
        return success;
    }
};
} // namespace

std::unique_ptr<IRuntimeFeature> CreateVfxRuntimeFeature() { return std::make_unique<VfxRuntimeFeature>(); }

} // namespace HIKARI
