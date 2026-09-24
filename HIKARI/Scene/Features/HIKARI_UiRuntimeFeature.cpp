#include "Scene/Ui/HIKARI_UiDataBindingComponent.h"
#include "Scene/Features/HIKARI_UiRuntimeFeature.h"

#include "Scene/Ui/HIKARI_UiAnimationComponents.h"
#include <array>
#include <memory>
#include <string>
#include <utility>

#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "HIKARI/Project/RuntimeFeature.h"
#include "Scene/Features/HIKARI_RuntimeFeatureIds.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_ComponentSystemPolicy.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"
#include "Scene/Ui/HIKARI_UiSceneComponents.h"
#include "Scene/Ui/HIKARI_UiSceneSystem.h"
#include "Scene/Ui/HIKARI_UiUtilityComponents.h"
#include "Scene/Ui/HIKARI_UiDisplaySettings.h"

namespace HIKARI {
namespace {

ComponentTypePresentation UiPresentation(const char* displayName, const char* description) {
    return {displayName, "UI", description, "UI"};
}

template <class Component>
ComponentTypeInfo UiComponent(std::string typeName, const char* displayName, const char* description) {
    ComponentTypeInfo info{};
    info.typeName = std::move(typeName);
    info.factory = []() -> std::unique_ptr<IComponent> { return IComponent::Create<Component>(); };
    info.presentation = UiPresentation(displayName, description);
    return info;
}

class UiRuntimeFeature final : public IRuntimeFeature {
public:
    std::string_view GetFeatureId() const noexcept override { return RuntimeFeatureIds::Ui; }
    std::string_view GetDisplayName() const noexcept override { return "UI"; }
    std::string_view GetDescription() const noexcept override {
        return "Scene-authored Canvas, image, text and button UI.";
    }
    std::span<const std::string_view> GetRequiredFeatureIds() const noexcept override {
        static constexpr std::array<std::string_view, 1u> ids{RuntimeFeatureIds::Rendering};
        return ids;
    }
    std::span<const std::string_view> GetComponentTypeNames() const noexcept override {
        static constexpr std::array<std::string_view, 13u> names{
            kUiCanvasComponentType,      kUiRectComponentType,   kUiImageComponentType,
            kUiTextComponentType,        kUiButtonComponentType, "UiGroupComponent",
            "UiLayoutGroupComponent",    "UiProgressComponent",  "UiDisplaySettingsPanelComponent",
            "UiDisplaySettingComponent", "UiAnimatorComponent",  "UiAnimationTargetComponent",
            "UiDataBindingComponent"};
        return names;
    }
    std::span<const std::string_view> GetSystemIds() const noexcept override {
        static constexpr std::array<std::string_view, 1u> ids{kUiSceneSystemId};
        return ids;
    }
    std::span<const RuntimeSystemDefault> GetDefaultSystems() const noexcept override {
        static constexpr std::array<RuntimeSystemDefault, 1u> systems{RuntimeSystemDefault{kUiSceneSystemId, true, 70}};
        return systems;
    }

    bool Register(RuntimeFeatureContext& context) override {
        ComponentRegistry& components = RuntimeFeatureContextAccess::Components(context);
        SystemTypeRegistry& systems = RuntimeFeatureContextAccess::Systems(context);

        ComponentTypeInfo canvas = UiComponent<UiCanvasComponent>(std::string(kUiCanvasComponentType), "UI Canvas",
                                                                  "Reference canvas and interaction root.");
        ComponentTypeInfo rect = UiComponent<UiRectComponent>(std::string(kUiRectComponentType), "UI Rect",
                                                              "Anchored screen-space layout rectangle.");
        ComponentTypeInfo image = UiComponent<UiImageComponent>(std::string(kUiImageComponentType), "UI Image",
                                                                "Texture or generated solid-color image.");
        image.requiredComponents = {std::string(kUiRectComponentType)};
        ComponentTypeInfo text = UiComponent<UiTextComponent>(std::string(kUiTextComponentType), "UI Text",
                                                              "UTF-8 text in the owning UI Rect.");
        text.requiredComponents = {std::string(kUiRectComponentType)};
        ComponentTypeInfo button = UiComponent<UiButtonComponent>(std::string(kUiButtonComponentType), "UI Button",
                                                                  "Pointer and action-driven UI activation.");
        button.requiredComponents = {std::string(kUiRectComponentType)};

        bool success = components.Register(std::move(canvas));
        success = components.Register(std::move(rect)) && success;
        success = components.Register(std::move(image)) && success;
        success = components.Register(std::move(text)) && success;
        success = components.Register(std::move(button)) && success;
        success =
            components.Register(UiComponent<UiGroupComponent>(
                "UiGroupComponent", "UI Group", "Visibility, opacity and interaction inherited by descendants.")) &&
            success;
        auto layout = UiComponent<UiLayoutGroupComponent>("UiLayoutGroupComponent", "UI Layout Group",
                                                          "Arrange direct children in a row, column or grid.");
        layout.requiredComponents = {std::string(kUiRectComponentType)};
        success = components.Register(std::move(layout)) && success;
        auto progress = UiComponent<UiProgressComponent>(
            "UiProgressComponent", "UI Progress",
            "Fill an image from a value or a runtime variable, for health and progress bars.");
        progress.requiredComponents = {std::string(kUiImageComponentType)};
        success = components.Register(std::move(progress)) && success;
        auto settings = UiComponent<UiDisplaySettingsPanelComponent>("UiDisplaySettingsPanelComponent", "display.panel",
                                                                     "display.panelHelp");
        settings.requiredComponents = {"UiRectComponent"};
        success = components.Register(std::move(settings)) && success;
        auto setting = UiComponent<UiDisplaySettingComponent>("UiDisplaySettingComponent", "display.control",
                                                              "display.controlHelp");
        setting.requiredComponents = {"UiTextComponent"};
        success = components.Register(std::move(setting)) && success;
        auto binding = UiComponent<UiDataBindingComponent>("UiDataBindingComponent", "uibind.component", "uibind.help");
        binding.requiredComponents = {"UiRectComponent"};
        success = components.Register(std::move(binding)) && success;
        auto animator =
            UiComponent<UiAnimatorComponent>("UiAnimatorComponent", "uianim.animator", "uianim.animatorHelp");
        success = components.Register(std::move(animator)) && success;
        auto target =
            UiComponent<UiAnimationTargetComponent>("UiAnimationTargetComponent", "uianim.target", "uianim.targetHelp");
        target.requiredComponents = {"UiRectComponent"};
        success = components.Register(std::move(target)) && success;
        success = systems.Register(SystemTypeInfo{
                      std::string(kUiSceneSystemId),
                      [](const nlohmann::json&) -> std::unique_ptr<ISystem> { return CreateUiSceneSystem(); },
                      "UI Scene", "UI"}) &&
                  success;
        auto& policy = RuntimeFeatureContextAccess::Policy(context);
        success = policy.Register({std::string(kUiCanvasComponentType), std::string(kUiSceneSystemId), 70}) && success;
        // UI の frame source 公開も共通の描画送信系が所有する。
        success = policy.Register({std::string(kUiCanvasComponentType), "ModelRenderSystem", 100}) && success;
        return success;
    }
};

} // namespace

std::unique_ptr<IRuntimeFeature> CreateUiRuntimeFeature() { return std::make_unique<UiRuntimeFeature>(); }

} // namespace HIKARI
