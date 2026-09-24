#include "Editor/Authoring/HIKARI_UiAuthoring.h"
#include "Editor/HIKARI_DocumentComponentAuthoringService.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"
#include "Scene/Ui/HIKARI_UiDisplaySettings.h"
#include "Core/Text/HIKARI_Localization.h"

namespace HIKARI::EDITOR {
UiCreateResult CreateUiSettingsTemplate(SceneDocument& document, const ComponentRegistry& registry,
                                        SceneObjectId parent, std::string_view font, std::string_view name) {
    auto working = document;
    const auto root = CreateUiTemplate(working, registry, "panel", parent, font,
                                       name.empty() ? TEXT::Localize("display.panel") : std::string(name));
    if (!root.ok)
        return root;
    DocumentComponentAuthoringService authoring;
    const auto panel =
        authoring.AddComponent(registry, *FindSceneObjectData(working, root.id), "UiDisplaySettingsPanelComponent");
    if (!panel.success)
        return {false, {}, panel.message};
    const auto patch = [&](SceneObjectId id, std::string_view type, const nlohmann::json& data) {
        for (auto& c : FindSceneObjectData(working, id)->components)
            if (c.type == type) {
                c.properties.merge_patch(data);
                break;
            }
    };
    const auto rect = [&](SceneObjectId id, float x, float y, float width, float height) {
        patch(id, "UiRectComponent", {{"offsetMinimum", {x, y}}, {"offsetMaximum", {x + width, y + height}}});
    };
    rect(root.id, -500, -430, 1000, 860);
    patch(root.id, "UiImageComponent", {{"tint", {.035, .048, .07, .98}}});
    const auto child = [&](std::string_view kind, const char* title, float x, float y, float width, float height) {
        const auto value = CreateUiTemplate(working, registry, kind, root.id, font, TEXT::Localize(title));
        if (value.ok) {
            rect(value.id, x, y, width, height);
            patch(value.id, "UiTextComponent",
                  {{"pixelHeight", 28},
                   {"outlineWidth", 0},
                   {"shadowColor", {0, 0, 0, 0}},
                   {"color", {.85, .89, .95, 1}}});
        }
        return value;
    };
    const auto heading = child("label", "display.panel", -440, -382, 750, 60);
    if (!heading.ok)
        return heading;
    patch(heading.id, "UiTextComponent", {{"pixelHeight", 46}, {"horizontalAlign", "Left"}});
    const auto accent = child("image", "display.panel", -440, -300, 880, 3);
    if (!accent.ok)
        return accent;
    patch(accent.id, "UiImageComponent", {{"tint", {.12, .72, .69, 1}}});
    const auto control = [&](SceneObjectId id, const char* action, const char* setting = "AntiAliasing") {
        auto* object = FindSceneObjectData(working, id);
        const auto result = authoring.AddComponent(registry, *object, "UiDisplaySettingComponent");
        if (result.success)
            patch(id, "UiDisplaySettingComponent", {{"action", action}, {"setting", setting}});
        return result;
    };
    float y = -270;
    for (const auto& field : UiDisplaySettings()) {
        const auto label = child("label", field.label, -440, y, 410, 54);
        if (!label.ok)
            return label;
        patch(label.id, "UiTextComponent", {{"horizontalAlign", "Left"}});
        const auto value = child("button", field.label, 0, y, 440, 54);
        if (!value.ok)
            return value;
        patch(value.id, "UiImageComponent", {{"tint", {.09, .14, .20, 1}}});
        const auto added = control(value.id, "Cycle", field.id);
        if (!added.success)
            return {false, {}, added.message};
        y += 67;
    }
    const auto status = child("label", "display.ready", -440, 220, 880, 96);
    if (!status.ok)
        return status;
    patch(status.id, "UiTextComponent", {{"pixelHeight", 23}, {"horizontalAlign", "Left"}});
    const auto addedStatus = control(status.id, "Status");
    if (!addedStatus.success)
        return {false, {}, addedStatus.message};
    float x = -440;
    for (const auto& action : {std::pair{"Revert", "display.revert"}, std::pair{"Keep", "display.keep"},
                               std::pair{"Apply", "display.apply"}}) {
        const auto button = child("button", action.second, x, 342, 272, 58);
        if (!button.ok)
            return button;
        const auto added = control(button.id, action.first);
        if (!added.success)
            return {false, {}, added.message};
        if (std::string_view(action.first) == "Apply")
            patch(button.id, "UiImageComponent", {{"tint", {.06, .49, .48, 1}}});
        x += 304;
    }
    document.objects = std::move(working.objects);
    return root;
}
} // namespace HIKARI::EDITOR
