#pragma once
#include "Scene/HIKARI_SceneDocument.h"
#include <span>
#include <string_view>
namespace HIKARI {
class World;
namespace EDITOR {
struct UiArrangement {
    std::string_view id, label;
    int axis;
    float alignment;
};
std::span<const UiArrangement> UiArrangements();
bool BuildUiArrangement(const SceneDocument&, World&, std::span<const SceneObjectId>, std::string_view,
                        nlohmann::json& commands, std::string& error);
} // namespace EDITOR
} // namespace HIKARI
