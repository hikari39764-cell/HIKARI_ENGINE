#pragma once
#include "Scene/HIKARI_SceneDocument.h"
#include <span>
#include <string_view>
#include <vector>

namespace HIKARI {
class ComponentRegistry;
class AssetDatabase;
} // namespace HIKARI
namespace HIKARI::EDITOR {
struct UiTemplateDefinition {
    std::string_view id, labelKey;
    bool text = false;
};
std::span<const UiTemplateDefinition> UiTemplates();
std::string FindUiFont(const AssetDatabase& database);
bool ReparentUiElement(SceneDocument& document, SceneObjectId element, SceneObjectId parent, std::string& error);
bool AttachUiPrefabToCanvas(SceneDocument& document, const ComponentRegistry& registry, SceneObjectId instance,
                            SceneObjectId selected, std::string& error);
struct UiCreateResult {
    bool ok = false;
    SceneObjectId id{};
    std::string error;
};
UiCreateResult CreateUiSettingsTemplate(SceneDocument&, const ComponentRegistry&, SceneObjectId parent,
                                        std::string_view font, std::string_view name);
UiCreateResult CreateUiTemplate(SceneDocument& document, const ComponentRegistry& registry, std::string_view templateId,
                                SceneObjectId parent, std::string_view font, std::string_view name = {});
} // namespace HIKARI::EDITOR
