#include "Editor/Authoring/HIKARI_UiArrangement.h"
#include "Scene/HIKARI_World.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Ui/HIKARI_UiLayoutResolver.h"
#include "Scene/Ui/HIKARI_UiUtilityComponents.h"
#include "Core/Text/HIKARI_Localization.h"
#include <algorithm>
#include <array>
#include <limits>
#include <unordered_set>

namespace HIKARI::EDITOR {
std::span<const UiArrangement> UiArrangements() {
    static constexpr UiArrangement modes[]{
        {"left", "uix.alignLeft", 0, 0},          {"center", "uix.alignCenter", 0, .5f},
        {"right", "uix.alignRight", 0, 1},        {"top", "uix.alignTop", 1, 0},
        {"middle", "uix.alignMiddle", 1, .5f},    {"bottom", "uix.alignBottom", 1, 1},
        {"horizontal", "uix.distributeX", 0, -1}, {"vertical", "uix.distributeY", 1, -1}};
    return modes;
}
bool BuildUiArrangement(const SceneDocument& document, World& world, std::span<const SceneObjectId> ids,
                        std::string_view mode, nlohmann::json& commands, std::string& error) {
    commands = nlohmann::json::array();
    const auto options = UiArrangements();
    const auto choice = std::find_if(options.begin(), options.end(), [&](const auto& m) { return m.id == mode; });
    if (choice == options.end()) {
        error = "Unknown UI arrangement.";
        return false;
    }
    const bool distribute = choice->alignment < 0;
    if (ids.size() < (distribute ? 3u : 2u)) {
        error = TEXT::Localize("uix.arrangeCount");
        return false;
    }
    struct Item {
        SceneObjectId id;
        const SceneComponentData* rect;
        float start, size;
    };
    std::vector<Item> items;
    std::unordered_set<uint64_t> seen;
    std::optional<SceneObjectId> parent;
    UiLayoutResolver resolver;
    resolver.SetAuthoringPreview(true);
    float low = std::numeric_limits<float>::max(), high = -low, extent = 0;
    for (const auto id : ids) {
        const auto source =
            std::find_if(document.objects.begin(), document.objects.end(), [&](const auto& o) { return o.id == id; });
        auto* object = world.FindObject(id);
        if (!object || source == document.objects.end() || source->editorLocked || !seen.insert(id.value).second) {
            error = TEXT::Localize("uix.arrangeUnavailable");
            return false;
        }
        if (!items.empty() && parent != source->parent) {
            error = TEXT::Localize("uix.arrangeSiblings");
            return false;
        }
        parent = source->parent;
        const auto* group = object->GetParent() ? object->GetParent()->GetComponent<UiLayoutGroupComponent>() : nullptr;
        if (group && group->IsEnabled()) {
            error = TEXT::Localize("uix.layoutControlled");
            return false;
        }
        const auto rect = std::find_if(source->components.begin(), source->components.end(),
                                       [](const auto& c) { return c.type == "UiRectComponent"; });
        const auto layout = resolver.Resolve(world, *object);
        if (rect == source->components.end() || !layout.valid) {
            error = TEXT::Localize("uix.arrangeUnavailable");
            return false;
        }
        const float start = choice->axis ? layout.y : layout.x, size = choice->axis ? layout.height : layout.width;
        items.push_back({id, &*rect, start, size});
        low = std::min(low, start);
        high = std::max(high, start + size);
        extent += size;
    }
    std::stable_sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    const float gap = (high - low - extent) / static_cast<float>(items.size() - 1);
    float cursor = low;
    for (const auto& item : items) {
        const float target = distribute ? cursor : low + (high - low - item.size) * choice->alignment;
        cursor += item.size + gap;
        const float delta = target - item.start;
        if (delta == 0)
            continue;
        for (const auto* field : {"offsetMinimum", "offsetMaximum"}) {
            auto offset = item.rect->properties.at(field).get<std::array<float, 2>>();
            offset[static_cast<size_t>(choice->axis)] += delta;
            commands.push_back({{"objectId", item.id.value},
                                {"componentId", item.rect->id.value},
                                {"propertyId", field},
                                {"value", offset}});
        }
    }
    return true;
}
} // namespace HIKARI::EDITOR
