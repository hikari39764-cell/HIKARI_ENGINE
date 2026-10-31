#include "Scene/Features/HIKARI_BuiltInRuntimeFeatures.h"

#include "Scene/Components/HIKARI_CameraFlowComponent.h"
#include "Scene/Camera/HIKARI_CameraFlowSystem.h"
#include <array>
#include <memory>

#include "Scene/Components/HIKARI_SequencePlayerComponent.h"
#include "HIKARI/Project/RuntimeFeature.h"
#include "Scene/Features/HIKARI_RuntimeFeatureIds.h"
#include "Scene/Features/HIKARI_RuntimeFeatureContextAccess.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_ComponentSystemPolicy.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"
#include "Scene/Sequencer/Runtime/HIKARI_SequencePlayerSystem.h"

namespace HIKARI {
namespace {
class SequenceRuntimeFeature final : public IRuntimeFeature {
public:
    std::string_view GetFeatureId() const noexcept override { return RuntimeFeatureIds::Cinematics; }

    std::string_view GetDisplayName() const noexcept override { return "Cinematics"; }

    std::string_view GetDescription() const noexcept override {
        return "Reusable sequence playback for camera and future presentation tracks.";
    }

    bool Register(RuntimeFeatureContext& context) override {
        ComponentRegistry& componentRegistry = RuntimeFeatureContextAccess::Components(context);
        SystemTypeRegistry& systemTypeRegistry = RuntimeFeatureContextAccess::Systems(context);
        ComponentSystemPolicy& componentSystemPolicy = RuntimeFeatureContextAccess::Policy(context);
        ComponentTypeInfo player{};
        player.typeName = "SequencePlayerComponent";
        player.factory = []() -> std::unique_ptr<IComponent> { return IComponent::Create<SequencePlayerComponent>(); };
        player.initializeDefaults = [](const SceneObjectData&, nlohmann::json& properties) {
            SequencePlayerComponent{}.Serialize(properties);
        };
        player.fields = JSONFIELDS::Describe<SequencePlayerSettings>(kSequencePlayerFields);
        player.presentation = ComponentTypePresentation{
            "Sequence Player", "Cinematics", "Plays a reusable sequence asset during runtime.", "Cinematics"};

        bool success = componentRegistry.Register(std::move(player));
        success = systemTypeRegistry.Register(SystemTypeInfo{"SequencePlayerSystem",
                                                             [](const nlohmann::json&) -> std::unique_ptr<ISystem> {
                                                                 return std::make_unique<SequencePlayerSystem>();
                                                             },
                                                             "Sequence Player", "Cinematics"}) &&
                  success;
        success = componentSystemPolicy.Register(
                      ComponentSystemRule{"SequencePlayerComponent", "SequencePlayerSystem", 170}) &&
                  success;
        ComponentTypeInfo flow{};
        flow.typeName = "CameraFlowComponent";
        flow.factory = [] { return IComponent::Create<CameraFlowComponent>(); };
        flow.initializeDefaults = [](const SceneObjectData&, nlohmann::json& properties) {
            CameraFlowComponent{}.Serialize(properties);
        };
        flow.fields = JSONFIELDS::Describe<CameraFlowSettings>(kCameraFlowFields);
        flow.presentation = {"Camera Flow", "Cinematics",
                             "Enter a region, play a sequence, hand control to a gameplay camera mode.", "Cinematics"};
        success = componentRegistry.Register(std::move(flow)) && success;
        success = systemTypeRegistry.Register(SystemTypeInfo{"CameraFlowSystem",
                                                             [](const nlohmann::json&) -> std::unique_ptr<ISystem> {
                                                                 return std::make_unique<CameraFlowSystem>();
                                                             },
                                                             "Camera Flow", "Cinematics"}) &&
                  success;
        success = componentSystemPolicy.Register(ComponentSystemRule{"CameraFlowComponent", "CameraFlowSystem", 180}) &&
                  success;
        return success;
    }
};
} // namespace

std::unique_ptr<IRuntimeFeature> CreateSequenceRuntimeFeature() { return std::make_unique<SequenceRuntimeFeature>(); }

} // namespace HIKARI
