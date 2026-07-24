#include "Scene/Features/HIKARI_SequenceRuntimeFeature.h"

#include <array>
#include <memory>

#include "Scene/Components/HIKARI_SequencePlayerComponent.h"
#include "Scene/Features/HIKARI_RuntimeFeature.h"
#include "Scene/Features/HIKARI_RuntimeFeatureIds.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_ComponentSystemPolicy.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"
#include "Scene/Sequencer/Runtime/HIKARI_SequencePlayerSystem.h"

namespace HIKARI {
    namespace {
        class SequenceRuntimeFeature final : public IRuntimeFeature {
        public:
            std::string_view GetFeatureId() const noexcept override {
                return RuntimeFeatureIds::Cinematics;
            }

            std::string_view GetDisplayName() const noexcept override {
                return "Cinematics";
            }

            std::string_view GetDescription() const noexcept override {
                return "Reusable sequence playback for camera and future presentation tracks.";
            }

            std::span<const std::string_view>
                GetComponentTypeNames() const noexcept override {
                static constexpr std::array<std::string_view, 1> names{
                    "SequencePlayerComponent"
                };
                return names;
            }

            std::span<const std::string_view>
                GetSystemIds() const noexcept override {
                static constexpr std::array<std::string_view, 1> ids{
                    "SequencePlayerSystem"
                };
                return ids;
            }

            bool Register(RuntimeFeatureContext& context) override {
                ComponentTypeInfo player{};
                player.typeName = "SequencePlayerComponent";
                player.factory = []() -> std::unique_ptr<IComponent> {
                    return std::make_unique<SequencePlayerComponent>();
                };
                player.initializeDefaults = [](
                    const SceneObjectData&,
                    nlohmann::json& properties) {
                    properties["enabled"] = true;
                    properties["sequenceAssetGuid"] = "";
                    properties["playOnStart"] = false;
                    properties["loop"] = false;
                    properties["playbackRate"] = 1.0f;
                    properties["startTimeSeconds"] = 0.0f;
                    properties["channel"] = "Cinematics";
                    properties["priority"] = 0;
                    properties["channelPolicy"] =
                        "ReplaceIfHigherOrEqual";
                    properties["stopOnDisable"] = true;
                    properties["restartIfPlaying"] = true;
                    properties["ownerSlotName"] = "Owner";
                    properties["bindings"] = nlohmann::json::array();
                };
                player.presentation = ComponentTypePresentation{
                    "Sequence Player",
                    "Cinematics",
                    "Plays a reusable sequence asset during runtime.",
                    "Cinematics"
                };

                bool success = context.componentRegistry.Register(
                    std::move(player));
                success = context.systemTypeRegistry.Register(SystemTypeInfo{
                    "SequencePlayerSystem",
                    [](const nlohmann::json&) -> std::unique_ptr<ISystem> {
                        return std::make_unique<SequencePlayerSystem>();
                    },
                    "Sequence Player",
                    "Cinematics"
                }) && success;
                success = context.componentSystemPolicy.Register(
                    ComponentSystemRule{
                        "SequencePlayerComponent",
                        "SequencePlayerSystem",
                        170
                    }) && success;
                return success;
            }
        };
    }

    std::unique_ptr<IRuntimeFeature> CreateSequenceRuntimeFeature() {
        return std::make_unique<SequenceRuntimeFeature>();
    }

} // namespace HIKARI
