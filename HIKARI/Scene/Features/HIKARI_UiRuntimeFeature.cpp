#include "Scene/Features/HIKARI_UiRuntimeFeature.h"

#include <array>
#include <memory>
#include <string>
#include <utility>

#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/Features/HIKARI_RuntimeFeature.h"
#include "Scene/Features/HIKARI_RuntimeFeatureIds.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_ComponentSystemPolicy.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"
#include "Scene/Ui/HIKARI_UiSceneComponents.h"
#include "Scene/Ui/HIKARI_UiSceneSystem.h"

namespace HIKARI {
    namespace {

        ComponentTypePresentation UiPresentation(
            const char* displayName,
            const char* description) {
            return { displayName, "UI", description, "UI" };
        }

        template<class Component>
        ComponentTypeInfo UiComponent(
            std::string typeName,
            const char* displayName,
            const char* description) {
            ComponentTypeInfo info{};
            info.typeName = std::move(typeName);
            info.factory = []() -> std::unique_ptr<IComponent> {
                return std::make_unique<Component>();
            };
            info.presentation = UiPresentation(displayName, description);
            return info;
        }

        class UiRuntimeFeature final : public IRuntimeFeature {
        public:
            std::string_view GetFeatureId() const noexcept override {
                return RuntimeFeatureIds::Ui;
            }
            std::string_view GetDisplayName() const noexcept override {
                return "UI";
            }
            std::string_view GetDescription() const noexcept override {
                return "Scene-authored Canvas, image, text and button UI.";
            }
            std::span<const std::string_view>
                GetRequiredFeatureIds() const noexcept override {
                static constexpr std::array<std::string_view, 1u> ids{
                    RuntimeFeatureIds::Rendering
                };
                return ids;
            }
            std::span<const std::string_view>
                GetComponentTypeNames() const noexcept override {
                static constexpr std::array<std::string_view, 5u> names{
                    kUiCanvasComponentType,
                    kUiRectComponentType,
                    kUiImageComponentType,
                    kUiTextComponentType,
                    kUiButtonComponentType
                };
                return names;
            }
            std::span<const std::string_view>
                GetSystemIds() const noexcept override {
                static constexpr std::array<std::string_view, 1u> ids{
                    kUiSceneSystemId
                };
                return ids;
            }
            std::span<const RuntimeSystemDefault>
                GetDefaultSystems() const noexcept override {
                static constexpr std::array<RuntimeSystemDefault, 1u> systems{
                    RuntimeSystemDefault{ kUiSceneSystemId, true, 70 }
                };
                return systems;
            }

            bool Register(RuntimeFeatureContext& context) override {
                ComponentRegistry& components =
                    RuntimeFeatureContextAccess::Components(context);
                SystemTypeRegistry& systems =
                    RuntimeFeatureContextAccess::Systems(context);

                ComponentTypeInfo canvas = UiComponent<UiCanvasComponent>(
                    std::string(kUiCanvasComponentType),
                    "UI Canvas",
                    "Reference canvas and interaction root.");
                ComponentTypeInfo rect = UiComponent<UiRectComponent>(
                    std::string(kUiRectComponentType),
                    "UI Rect",
                    "Anchored screen-space layout rectangle.");
                ComponentTypeInfo image = UiComponent<UiImageComponent>(
                    std::string(kUiImageComponentType),
                    "UI Image",
                    "Texture or generated solid-color image.");
                image.requiredComponents = {
                    std::string(kUiRectComponentType) };
                ComponentTypeInfo text = UiComponent<UiTextComponent>(
                    std::string(kUiTextComponentType),
                    "UI Text",
                    "UTF-8 text in the owning UI Rect.");
                text.requiredComponents = {
                    std::string(kUiRectComponentType) };
                ComponentTypeInfo button = UiComponent<UiButtonComponent>(
                    std::string(kUiButtonComponentType),
                    "UI Button",
                    "Pointer and action-driven UI activation.");
                button.requiredComponents = {
                    std::string(kUiRectComponentType) };

                bool success = components.Register(std::move(canvas));
                success = components.Register(std::move(rect)) && success;
                success = components.Register(std::move(image)) && success;
                success = components.Register(std::move(text)) && success;
                success = components.Register(std::move(button)) && success;
                success = systems.Register(SystemTypeInfo{
                    std::string(kUiSceneSystemId),
                    [](const nlohmann::json&) -> std::unique_ptr<ISystem> {
                        return CreateUiSceneSystem();
                    },
                    "UI Scene",
                    "UI"
                }) && success;
                auto& policy = RuntimeFeatureContextAccess::Policy(context);
                success = policy.Register({std::string(kUiCanvasComponentType),
                    std::string(kUiSceneSystemId), 70}) && success;
                // UI の frame source 公開も共通の描画送信系が所有する。
                success = policy.Register({std::string(kUiCanvasComponentType),
                    "ModelRenderSystem", 100}) && success;
                return success;
            }
        };

    } // namespace

    std::unique_ptr<IRuntimeFeature> CreateUiRuntimeFeature() {
        return std::make_unique<UiRuntimeFeature>();
    }

} // namespace HIKARI
