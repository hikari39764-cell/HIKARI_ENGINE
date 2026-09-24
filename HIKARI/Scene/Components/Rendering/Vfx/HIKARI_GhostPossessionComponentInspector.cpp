#include "Scene/Components/Rendering/Vfx/HIKARI_GhostPossessionComponent.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Scene/HIKARI_GameObject.h"

namespace HIKARI {
namespace {
bool EditSlotChoice(IInspectorBuilder& builder, const char* label, const VfxPlayerComponent* player,
                    VfxSlotId& slotId) {
    if (player == nullptr) {
        return false;
    }

    std::vector<std::string> labels{"None"};
    std::vector<VfxSlotId> ids{VfxSlotId{}};
    int selectedIndex = slotId.IsValid() ? -1 : 0;
    for (const VfxPlayerSlot& slot : player->GetSlots()) {
        labels.push_back(slot.name + " [" + std::to_string(slot.id.value) + "]");
        ids.push_back(slot.id);
        if (slot.id == slotId) {
            selectedIndex = static_cast<int>(ids.size() - 1u);
        }
    }
    if (selectedIndex < 0) {
        labels.push_back("Missing [" + std::to_string(slotId.value) + "]");
        ids.push_back(slotId);
        selectedIndex = static_cast<int>(ids.size() - 1u);
    }

    std::vector<const char*> choices{};
    choices.reserve(labels.size());
    for (const std::string& choice : labels) {
        choices.push_back(choice.c_str());
    }
    if (!builder.Choice(label, selectedIndex, choices)) {
        return false;
    }
    selectedIndex = std::clamp(selectedIndex, 0, static_cast<int>(ids.size() - 1u));
    slotId = ids[static_cast<size_t>(selectedIndex)];
    return true;
}
} // namespace

void GhostPossessionComponent::BuildInspector(IInspectorBuilder& builder) {
    GhostPossessionSettings next = settings_;
    bool changed = false;

    if (builder.Section("State", true)) {
        changed = builder.Bool("Enabled", next.enabled) || changed;
        changed = builder.Bool("Possessed", next.possessed) || changed;
        changed = builder.Bool("Preview In Editor", next.previewInEditor) || changed;
        changed = builder.FloatRange("Intensity", next.intensity, 0.0f, 2.0f, 0.01f) || changed;
        changed = builder.FloatRange("Blend In Seconds", next.blendInSeconds, 0.0f, 5.0f, 0.01f) || changed;
        changed = builder.FloatRange("Blend Out Seconds", next.blendOutSeconds, 0.0f, 5.0f, 0.01f) || changed;
    }

    if (builder.Section("Ghost Surface", true)) {
        changed = builder.String("Material FX Profile", next.materialFxProfileId) || changed;
        changed = builder.FloatRange("Rim Strength", next.rimStrength, 0.0f, 8.0f, 0.01f) || changed;
        changed = builder.FloatRange("Rim Power", next.rimPower, 0.1f, 16.0f, 0.05f) || changed;
        changed = builder.FloatRange("Dissolve Amount", next.dissolveAmount, 0.0f, 0.9f, 0.005f) || changed;
        changed = builder.FloatRange("Edge Width", next.edgeWidth, 0.001f, 0.3f, 0.001f) || changed;
        changed = builder.FloatRange("Pulse Speed", next.pulseSpeed, 0.0f, 10.0f, 0.01f) || changed;
        changed = builder.FloatRange("Edge Boost", next.edgeBoost, 0.0f, 8.0f, 0.01f) || changed;
        changed = builder.FloatRange("Noise Scale", next.noiseScale, 0.01f, 10.0f, 0.01f) || changed;
        changed = builder.FloatRange("Flow Speed", next.flowSpeed, 0.0f, 12.0f, 0.01f) || changed;
        changed = builder.FloatRange("Flow Strength", next.flowStrength, 0.0f, 2.0f, 0.01f) || changed;
        changed = builder.FloatRange("Distortion Strength", next.distortionStrength, 0.0f, 0.05f, 0.0005f) || changed;
        changed = builder.FloatRange("Scene Color Mix", next.sceneColorMix, 0.0f, 1.0f, 0.01f) || changed;
        changed =
            builder.FloatRange("Scene Color Noise Scale", next.sceneColorNoiseScale, 0.01f, 10.0f, 0.01f) || changed;
    }

    if (builder.Section("Optional VFX Slots", false)) {
        const GameObject* owner = GetOwner();
        const VfxPlayerComponent* player = owner != nullptr ? owner->GetComponent<VfxPlayerComponent>() : nullptr;
        if (player == nullptr) {
            builder.Text("Add a VFX Player component to assign particle slots.");
        } else {
            changed = EditSlotChoice(builder, "Enter Slot", player, next.enterSlotId) || changed;
            changed = EditSlotChoice(builder, "Loop Slot", player, next.loopSlotId) || changed;
            changed = EditSlotChoice(builder, "Exit Slot", player, next.exitSlotId) || changed;
        }
    }

    if (changed) {
        SetSettings(std::move(next));
    }
}

} // namespace HIKARI
