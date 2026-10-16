#include "Editor/Authoring/HIKARI_UiAuthoring.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Editor/HIKARI_DocumentComponentAuthoringService.h"
#include "Scene/HIKARI_SceneComponentId.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"
#include "Scene/Serialization/Objects/HIKARI_SceneObjectJson.h"
#include <algorithm>
#include <array>

namespace HIKARI::EDITOR {
namespace {
constexpr std::array<UiTemplateDefinition, 18> templates{{{"settings", "display.panel"},
                                                          {"menu", "uix.menu"},
                                                          {"hud", "uix.hud"},
                                                          {"canvas", "uix.canvas"},
                                                          {"panel", "uix.panel"},
                                                          {"image", "uix.image"},
                                                          {"button", "uix.button"},
                                                          {"scene_button", "uix.sceneButton"},
                                                          {"progress", "uix.progress"},
                                                          {"scroll", "uix.scrollList"},
                                                          {"text_field", "uix.textField", true},
                                                          {"row", "uix.row"},
                                                          {"column", "uix.column"},
                                                          {"grid", "uix.grid"},
                                                          {"label", "uix.label", true},
                                                          {"title", "uix.title", true},
                                                          {"caption", "uix.caption", true},
                                                          {"world_text", "uix.worldText", true}}};
void Patch(SceneObjectData& object, std::string_view type, const nlohmann::json& patch) {
    if (SceneComponentData* component = object.FindComponent(type))
        component->properties.merge_patch(patch);
}
} // namespace
std::span<const UiTemplateDefinition> UiTemplates() { return templates; }
bool ReparentUiElement(SceneDocument& document, SceneObjectId element, SceneObjectId parentId, std::string& error) {
    auto* object = FindSceneObjectData(document, element);
    const auto* parent = FindSceneObjectData(document, parentId);
    if (!object || !parent || object->editorLocked || parent->editorLocked ||
        object->FindComponent("UiRectComponent") == nullptr || object->FindComponent("UiCanvasComponent") != nullptr ||
        (parent->FindComponent("UiRectComponent") == nullptr &&
         parent->FindComponent("UiCanvasComponent") == nullptr)) {
        error = "Choose an unlocked UI element and a Canvas or UI container.";
        return false;
    }
    const auto* ancestor = parent;
    for (int depth = 0; ancestor && depth < 64; ++depth) {
        if (ancestor->id == element) {
            error = "An element cannot contain itself.";
            return false;
        }
        if (ancestor->FindComponent("UiCanvasComponent") != nullptr) {
            object->parent = parentId;
            error.clear();
            return true;
        }
        ancestor = ancestor->parent ? FindSceneObjectData(document, *ancestor->parent) : nullptr;
    }
    error = "The parent must belong to a Canvas.";
    return false;
}
std::string FindUiFont(const AssetDatabase& database) {
    auto fonts = database.CollectByType(AssetType::Font);
    std::erase_if(fonts, [](const auto* font) { return !font->lastImportSucceeded || !font->sourceExists; });
    if (fonts.empty())
        return {};
    std::sort(fonts.begin(), fonts.end(), [](const auto* a, const auto* b) { return a->displayName < b->displayName; });
    return fonts.front()->guid.value;
}
bool AttachUiPrefabToCanvas(SceneDocument& document, const ComponentRegistry& registry, SceneObjectId instance,
                            SceneObjectId selected, std::string& error) {
    const auto* root = FindSceneObjectData(document, instance);
    if (!root) {
        error = "Prefab root is missing.";
        return false;
    }
    if (root->FindComponent("UiRectComponent") == nullptr || root->FindComponent("UiCanvasComponent") != nullptr)
        return true;
    auto working = document;
    SceneObjectId parent{};
    const auto* candidate = FindSceneObjectData(working, selected);
    if (candidate && (candidate->FindComponent("UiCanvasComponent") != nullptr ||
                      candidate->FindComponent("UiGroupComponent") != nullptr ||
                      candidate->FindComponent("UiLayoutGroupComponent") != nullptr)) {
        const auto* ancestor = candidate;
        for (int depth = 0; ancestor && depth < 64; ++depth) {
            if (ancestor->FindComponent("UiCanvasComponent") != nullptr) {
                parent = candidate->id;
                break;
            }
            ancestor = ancestor->parent ? FindSceneObjectData(working, *ancestor->parent) : nullptr;
        }
    }
    if (!parent.value)
        for (const auto& o : working.objects)
            if (o.FindComponent("UiCanvasComponent") != nullptr && !o.editorLocked) {
                parent = o.id;
                break;
            }
    if (!parent.value) {
        const auto created = CreateUiTemplate(working, registry, "canvas", {}, {});
        if (!created.ok) {
            error = created.error;
            return false;
        }
        parent = created.id;
    }
    if (!ReparentUiElement(working, instance, parent, error))
        return false;
    document.objects = std::move(working.objects);
    return true;
}
UiCreateResult CreateUiTemplate(SceneDocument& document, const ComponentRegistry& registry, std::string_view templateId,
                                SceneObjectId parentId, std::string_view font, std::string_view name) {
    const auto entry =
        std::find_if(templates.begin(), templates.end(), [&](const auto& t) { return t.id == templateId; });
    if (entry == templates.end())
        return {false, {}, "Unknown UI template."};
    if (templateId == "settings")
        return CreateUiSettingsTemplate(document, registry, parentId, font, name);
    SceneDocument working = document;
    if (templateId == "menu" || templateId == "hud") {
        const auto root = CreateUiTemplate(working, registry, "panel", parentId, font,
                                           name.empty() ? TEXT::Localize(entry->labelKey) : std::string(name));
        if (!root.ok)
            return root;
        for (auto* ancestor = FindSceneObjectData(working, root.id); ancestor;) {
            if (ancestor->FindComponent("UiCanvasComponent") != nullptr) {
                Patch(*ancestor, "UiCanvasComponent", {{"inputMode", templateId == "menu" ? "Menu" : "Hud"}});
                break;
            }
            ancestor = ancestor->parent ? FindSceneObjectData(working, *ancestor->parent) : nullptr;
        }
        const auto child = [&](std::string_view kind, const char* label) {
            return CreateUiTemplate(working, registry, kind, root.id, font, TEXT::Localize(label));
        };
        if (templateId == "hud") {
            Patch(*FindSceneObjectData(working, root.id), "UiRectComponent",
                  {{"anchorMinimum", {0, 0}},
                   {"anchorMaximum", {0, 0}},
                   {"offsetMinimum", {32, 32}},
                   {"offsetMaximum", {432, 160}}});
            const auto label = child("label", "uix.health");
            if (!label.ok)
                return label;
            Patch(*FindSceneObjectData(working, label.id), "UiRectComponent",
                  {{"offsetMinimum", {-180, -50}}, {"offsetMaximum", {180, -4}}});
            const auto bar = child("progress", "uix.health");
            if (!bar.ok)
                return bar;
            Patch(*FindSceneObjectData(working, bar.id), "UiRectComponent",
                  {{"offsetMinimum", {-180, 12}}, {"offsetMaximum", {180, 36}}});
        } else {
            const auto title = child("title", "uix.menu");
            if (!title.ok)
                return title;
            Patch(*FindSceneObjectData(working, title.id), "UiRectComponent",
                  {{"offsetMinimum", {-320, -180}}, {"offsetMaximum", {320, -80}}});
            Patch(*FindSceneObjectData(working, title.id), "UiTextComponent", {{"pixelHeight", 56}});
            const auto start = child("button", "uix.continue");
            if (!start.ok)
                return start;
            Patch(*FindSceneObjectData(working, start.id), "UiRectComponent",
                  {{"offsetMinimum", {-160, -30}}, {"offsetMaximum", {160, 40}}});
            Patch(
                *FindSceneObjectData(working, start.id), "UiButtonComponent",
                {{"groupAction", 2}, {"groupTarget", SCENE::SERIALIZATION::WriteSceneObjectReference(root.id.value)}});
            const auto close = child("button", "uix.help");
            if (!close.ok)
                return close;
            Patch(*FindSceneObjectData(working, close.id), "UiRectComponent",
                  {{"offsetMinimum", {-160, 65}}, {"offsetMaximum", {160, 135}}});
            const auto help = child("panel", "uix.help");
            if (!help.ok)
                return help;
            Patch(*FindSceneObjectData(working, help.id), "UiGroupComponent", {{"visible", false}, {"modal", true}});
            Patch(*FindSceneObjectData(working, help.id), "UiRectComponent", {{"order", 40}});
            const auto helpText =
                CreateUiTemplate(working, registry, "label", help.id, font, TEXT::Localize("uix.helpText"));
            if (!helpText.ok)
                return helpText;
            Patch(*FindSceneObjectData(working, helpText.id), "UiRectComponent",
                  {{"offsetMinimum", {-280, -150}}, {"offsetMaximum", {280, 50}}, {"order", 41}});
            const auto dismiss =
                CreateUiTemplate(working, registry, "button", help.id, font, TEXT::Localize("uix.close"));
            if (!dismiss.ok)
                return dismiss;
            Patch(*FindSceneObjectData(working, dismiss.id), "UiRectComponent",
                  {{"offsetMinimum", {-160, 90}}, {"offsetMaximum", {160, 160}}, {"order", 42}});
            Patch(
                *FindSceneObjectData(working, dismiss.id), "UiButtonComponent",
                {{"groupAction", 2}, {"groupTarget", SCENE::SERIALIZATION::WriteSceneObjectReference(help.id.value)}});
            Patch(
                *FindSceneObjectData(working, close.id), "UiButtonComponent",
                {{"groupAction", 1}, {"groupTarget", SCENE::SERIALIZATION::WriteSceneObjectReference(help.id.value)}});
        }
        document.objects = std::move(working.objects);
        return root;
    }
    DocumentComponentAuthoringService authoring;
    std::string error;
    auto add = [&](SceneObjectData& o, const char* type) {
        const auto result = authoring.AddComponent(registry, o, type);
        if (!result.success)
            error = result.message;
        return result.success;
    };
    auto make = [&](std::string_view label) {
        SceneObjectData o;
        o.id = AllocateSceneObjectId(working);
        o.name = std::string(label);
        return o;
    };
    if (templateId != "canvas" && templateId != "world_text") {
        auto* parent = FindSceneObjectData(working, parentId);
        auto* ancestor = parent;
        for (int i = 0; ancestor && i < 64 && ancestor->FindComponent("UiCanvasComponent") == nullptr; ++i)
            ancestor = FindSceneObjectData(working, ancestor->parent.value_or(SceneObjectId{}));
        if (!ancestor || ancestor->FindComponent("UiCanvasComponent") == nullptr) {
            if (parentId.value)
                return {false, {}, "Parent must belong to a UI Canvas."};
            auto first = std::find_if(working.objects.begin(), working.objects.end(),
                                      [](const auto& o) { return o.FindComponent("UiCanvasComponent") != nullptr; });
            if (first != working.objects.end())
                parentId = first->id;
            else {
                auto canvas = make(TEXT::Localize("uix.canvas"));
                if (!add(canvas, "UiCanvasComponent"))
                    return {false, {}, error};
                parentId = canvas.id;
                working.objects.push_back(std::move(canvas));
            }
        }
    }
    auto object = make(name.empty() ? TEXT::Localize(entry->labelKey) : std::string(name));
    if (templateId != "canvas" && templateId != "world_text")
        object.parent = parentId;
    if (templateId == "canvas") {
        if (!add(object, "UiCanvasComponent"))
            return {false, {}, error};
    } else if (templateId == "world_text") {
        if (!add(object, "WorldTextComponent"))
            return {false, {}, error};
        Patch(object, "WorldTextComponent",
              {{"text", object.name}, {IsValidAssetGuid(font) ? "fontAssetGuid" : "fontAssetPath", font}});
    } else {
        if (!add(object, "UiRectComponent"))
            return {false, {}, error};
        const bool text = entry->text || templateId == "button" || templateId == "scene_button";
        if (!entry->text && templateId != "scroll" && templateId != "text_field") {
            if (!add(object, "UiImageComponent"))
                return {false, {}, error};
            Patch(object, "UiImageComponent", {{"tint", {.055, .08, .115, .96}}});
        }
        if (text && templateId != "text_field") {
            if (!add(object, "UiTextComponent"))
                return {false, {}, error};
            Patch(object, "UiTextComponent",
                  {{"text", object.name},
                   {IsValidAssetGuid(font) ? "fontAssetGuid" : "fontAssetPath", font},
                   {"outlineWidth", 0},
                   {"shadowColor", {0, 0, 0, 0}}});
        }
        if (templateId == "button" || templateId == "scene_button") {
            if (!add(object, "UiButtonComponent"))
                return {false, {}, error};
            Patch(object, "UiImageComponent", {{"tint", {.08, .38, .44, 1}}});
        }
        if (templateId == "scene_button") {
            if (!add(object, "HIKARI.SceneFlow.Trigger"))
                return {false, {}, error};
            Patch(object, "HIKARI.SceneFlow.Trigger", {{"actionEnabled", false}, {"uiButtonEnabled", true}});
        }
        if (templateId == "progress") {
            if (!add(object, "UiProgressComponent"))
                return {false, {}, error};
            Patch(object, "UiRectComponent", {{"offsetMinimum", {-160, -10}}, {"offsetMaximum", {160, 10}}});
            Patch(object, "UiImageComponent", {{"tint", {.15, .78, .65, 1}}});
        }
        if (templateId == "scroll") {
            if (!add(object, "UiImageComponent") || !add(object, "UiGroupComponent") ||
                !add(object, "UiScrollViewComponent") || !add(object, "UiLayoutGroupComponent"))
                return {false, {}, error};
            Patch(object, "UiRectComponent", {{"offsetMinimum", {-360, -220}}, {"offsetMaximum", {360, 220}}});
            Patch(object, "UiImageComponent", {{"tint", {.035, .048, .07, .98}}});
            Patch(object, "UiScrollViewComponent",
                  {{"axis", "Vertical"}, {"contentExtent", 0.0}, {"scrollSpeed", 48.0}});
            Patch(object, "UiLayoutGroupComponent",
                  {{"direction", 1}, {"cellSize", {680, 56}}, {"spacing", 12}, {"padding", 16}});
        }
        if (templateId == "text_field") {
            if (!add(object, "UiTextComponent") || !add(object, "UiImageComponent") ||
                !add(object, "UiTextFieldComponent"))
                return {false, {}, error};
            Patch(object, "UiRectComponent", {{"offsetMinimum", {-340, -36}}, {"offsetMaximum", {340, 36}}});
            Patch(object, "UiImageComponent", {{"tint", {.09, .14, .20, 1}}});
            Patch(object, "UiTextComponent",
                  {{"text", TEXT::Localize("uix.textFieldSample")},
                   {IsValidAssetGuid(font) ? "fontAssetGuid" : "fontAssetPath", font},
                   {"pixelHeight", 24},
                   {"horizontalAlign", "Left"},
                   {"outlineWidth", 0},
                   {"shadowColor", {0, 0, 0, 0}}});
            Patch(object, "UiTextFieldComponent", {{"enabled", true}, {"maximumCharacters", 256}});
        }
        if (templateId == "panel" || templateId == "row" || templateId == "column" || templateId == "grid") {
            if (!add(object, "UiGroupComponent"))
                return {false, {}, error};
            Patch(object, "UiRectComponent", {{"offsetMinimum", {-360, -240}}, {"offsetMaximum", {360, 240}}});
            if (templateId != "panel") {
                if (!add(object, "UiLayoutGroupComponent"))
                    return {false, {}, error};
                Patch(object, "UiLayoutGroupComponent",
                      {{"direction", templateId == "row"      ? 0
                                     : templateId == "column" ? 1
                                                              : 2}});
            }
        }
        if (templateId == "title") {
            Patch(object, "UiRectComponent", {{"offsetMinimum", {-500, -100}}, {"offsetMaximum", {500, 100}}});
            Patch(object, "UiTextComponent",
                  {{"pixelHeight", 84},
                   {"letterSpacing", 3},
                   {"outlineWidth", 2},
                   {"outlineColor", {.02, .05, .08, 1}},
                   {"glowColor", {.05, .65, .9, 1}},
                   {"glowWidth", 3},
                   {"glowIntensity", .4}});
        }
        if (templateId == "caption") {
            Patch(object, "UiRectComponent",
                  {{"anchorMinimum", {.5, 1}},
                   {"anchorMaximum", {.5, 1}},
                   {"offsetMinimum", {-640, -170}},
                   {"offsetMaximum", {640, -70}}});
            Patch(object, "UiTextComponent",
                  {{"pixelHeight", 36}, {"outlineWidth", 2}, {"outlineColor", {0, 0, 0, 1}}});
        }
    }
    NormalizeSceneComponentIds(object);
    const auto id = object.id;
    working.objects.push_back(std::move(object));
    if (templateId == "scroll") {
        for (const char* label : {"uix.scrollItemOne", "uix.scrollItemTwo", "uix.scrollItemThree"}) {
            const auto item = CreateUiTemplate(working, registry, "button", id, font, TEXT::Localize(label));
            if (!item.ok)
                return item;
            Patch(*FindSceneObjectData(working, item.id), "UiImageComponent", {{"tint", {.08, .38, .44, 1}}});
        }
    }
    document.objects = std::move(working.objects);
    return {true, id, {}};
}
} // namespace HIKARI::EDITOR
