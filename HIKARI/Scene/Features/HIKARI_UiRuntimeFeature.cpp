#include "Scene/Ui/HIKARI_UiDataBindingComponent.h"
#include "Scene/Ui/HIKARI_UiBindingSourceComponent.h"
#include "Scene/Ui/HIKARI_UiScrollViewComponent.h"
#include "Scene/Ui/HIKARI_UiTextFieldComponent.h"
#include "Scene/Features/HIKARI_BuiltInRuntimeFeatures.h"

#include "Scene/Ui/HIKARI_UiAnimationComponents.h"
#include <array>
#include <memory>
#include <string>
#include <utility>

#include "Scene/Features/HIKARI_RuntimeFeatureContextAccess.h"
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
        ComponentTypeInfo textField = UiComponent<UiTextFieldComponent>(
            "UiTextFieldComponent", "UI Text Field",
            "Accept keyboard input in the game window. Text is stored in this object's UiTextComponent.");
        textField.requiredComponents = {std::string(kUiRectComponentType), std::string(kUiTextComponentType)};
        ComponentTypeInfo scrollView =
            UiComponent<UiScrollViewComponent>("UiScrollViewComponent", "UI Scroll View",
                                               "Clip child content to this viewport and allow scrolling. "
                                               "Add a layout group to this object to arrange its content.");
        scrollView.requiredComponents = {std::string(kUiRectComponentType)};

        bool success = components.Register(std::move(canvas));
        success = components.Register(std::move(rect)) && success;
        success = components.Register(std::move(image)) && success;
        success = components.Register(std::move(text)) && success;
        success = components.Register(std::move(button)) && success;
        success = components.Register(std::move(textField)) && success;
        success = components.Register(std::move(scrollView)) && success;
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
        auto settings = UiComponent<UiDisplaySettingsPanelComponent>(
            "UiDisplaySettingsPanelComponent", "Display Settings Panel",
            "Child controls edit a shared draft. Apply changes in a game window; window changes require confirmation.");
        settings.requiredComponents = {"UiRectComponent"};
        success = components.Register(std::move(settings)) && success;
        auto setting =
            UiComponent<UiDisplaySettingComponent>("UiDisplaySettingComponent", "Display Setting Control",
                                                   "Bind a text or button to a display option or panel action.");
        setting.requiredComponents = {"UiTextComponent"};
        success = components.Register(std::move(setting)) && success;
        auto binding =
            UiComponent<UiDataBindingComponent>("UiDataBindingComponent", "UI Data Binding",
                                                "Display runtime variables without writing game rules in the UI.");
        binding.requiredComponents = {"UiRectComponent"};
        success = components.Register(std::move(binding)) && success;
        success = components.Register(UiComponent<UiBindingSourceComponent>(
                      "UiBindingSourceComponent", "UI Variable Source",
                      "Descendant UI elements read project runtime variables from this object.")) &&
                  success;
        auto animator = UiComponent<UiAnimatorComponent>("UiAnimatorComponent", "UI Animator",
                                                         "Play reusable clips on this UI hierarchy.");
        success = components.Register(std::move(animator)) && success;
        auto target = UiComponent<UiAnimationTargetComponent>("UiAnimationTargetComponent", "UI Animation Target",
                                                              "A unique named slot inside the parent animator.");
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
