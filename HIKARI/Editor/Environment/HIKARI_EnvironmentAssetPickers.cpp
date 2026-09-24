#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetImportState.h"
#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Editor/Environment/HIKARI_EnvironmentEditComparison.h"
#include "Editor/Environment/HIKARI_EnvironmentPanel.h"
#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Widgets/HIKARI_AssetFieldWidget.h"
#include "Render3D/Diagnostics/HIKARI_EnvironmentDiagnostics.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_SceneTransitionBus.h"
#include "Vfx/Post/HIKARI_PostProfile.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <utility>
#include <vector>
#endif
#include "Editor/Environment/HIKARI_EnvironmentPanelSections.h"

namespace HIKARI::EDITOR::ENVIRONMENT_PANEL {

#if defined(HIKARI_WITH_EDITOR)
struct SkyPickerEntry {
    std::string id{};
    std::string name{};
    std::string path{};
    std::string state{};
    std::string label{};
};

SkyPickerEntry BuildSkyPickerEntry(const AssetDescriptor& descriptor, const AssetDatabase* assetDatabase) {
    SkyPickerEntry entry{};
    entry.id = descriptor.id.value;
    entry.path = descriptor.sourcePath;

    if (assetDatabase) {
        if (const AssetRecord* record = assetDatabase->FindByGuid(AssetGuid{descriptor.id.value})) {
            entry.name = record->displayName.empty() ? record->sourcePath.stem().string() : record->displayName;
            entry.path = record->sourcePath.generic_string();
            entry.state = ToString(GetImportState(*record));
        }
    }

    if (entry.name.empty()) {
        entry.name = std::filesystem::path(descriptor.sourcePath).stem().string();
    }
    if (entry.name.empty()) {
        entry.name = entry.id.empty() ? TEXT::Localize("ui.environment.unnamedSky") : entry.id;
    }
    if (entry.state.empty()) {
        entry.state = TEXT::Localize("ui.environment.registered");
    }

    entry.label = entry.name + "  [" + entry.state + "]##" + entry.id;
    return entry;
}

bool DrawSkyAssetPicker(const AssetRegistry* assetRegistry, const AssetDatabase* assetDatabase, std::string& value) {
    if (assetDatabase) {
        return EDITOR::DrawAssetField(
            assetDatabase,
            EDITOR::AssetFieldOptions{TEXT::Localize("ui.environment.skyAsset"), AssetType::Sky, true, false, true},
            value);
    }

    if (!assetRegistry) {
        char skyAssetBuffer[256]{};
        std::strncpy(skyAssetBuffer, value.c_str(), sizeof(skyAssetBuffer) - 1);
        if (ImGui::InputText(TEXT::Localize("ui.environment.skyAsset").c_str(), skyAssetBuffer,
                             sizeof(skyAssetBuffer))) {
            value = skyAssetBuffer;
            return true;
        }
        return false;
    }

    std::vector<const AssetDescriptor*> skies = assetRegistry->CollectByType(AssetType::Sky);
    std::vector<SkyPickerEntry> entries;
    entries.reserve(skies.size());
    for (const AssetDescriptor* descriptor : skies) {
        if (descriptor && !descriptor->id.value.empty()) {
            entries.push_back(BuildSkyPickerEntry(*descriptor, assetDatabase));
        }
    }
    std::sort(entries.begin(), entries.end(),
              [](const SkyPickerEntry& lhs, const SkyPickerEntry& rhs) { return lhs.name < rhs.name; });

    const SkyPickerEntry* current = nullptr;
    for (const SkyPickerEntry& entry : entries) {
        if (entry.id == value) {
            current = &entry;
            break;
        }
    }

    bool changed = false;
    const std::string preview =
        value.empty() ? TEXT::Localize("ui.common.none")
                      : (current ? current->name : TEXT::Localize("ui.environment.missingPrefix") + value);
    if (ImGui::BeginCombo(TEXT::Localize("ui.environment.skyAsset").c_str(), preview.c_str())) {
        if (ImGui::Selectable(TEXT::Localize("ui.common.none").c_str(), value.empty())) {
            value.clear();
            changed = true;
        }
        for (const SkyPickerEntry& entry : entries) {
            const bool selected = value == entry.id;
            if (ImGui::Selectable(entry.label.c_str(), selected)) {
                value = entry.id;
                changed = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s\n%s\nGUID: %s", entry.name.c_str(),
                                  entry.path.empty() ? TEXT::Localize("ui.environment.noSourcePath").c_str()
                                                     : entry.path.c_str(),
                                  entry.id.c_str());
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button(TEXT::Localize("ui.environment.copySkyGuid").c_str()) && !value.empty()) {
        ImGui::SetClipboardText(value.c_str());
    }
    if (!value.empty()) {
        if (current) {
            ImGui::TextDisabled("%s | %s", current->path.c_str(), current->state.c_str());
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                               TEXT::Localize("ui.environment.unresolvedSkyFormat").c_str(), value.c_str());
        }
    }
    return changed;
}

bool DrawPanoramaTextureAssetPicker(const AssetDatabase* assetDatabase, const char* labelKey, std::string& value) {
    const std::string label = TEXT::Localize(labelKey);
    if (assetDatabase) {
        return EDITOR::DrawAssetField(assetDatabase,
                                      EDITOR::AssetFieldOptions{label, AssetType::Texture, true, false, true}, value);
    }

    char assetBuffer[256]{};
    std::strncpy(assetBuffer, value.c_str(), sizeof(assetBuffer) - 1);
    if (ImGui::InputText(label.c_str(), assetBuffer, sizeof(assetBuffer))) {
        value = assetBuffer;
        return true;
    }
    return false;
}

#endif

} // namespace HIKARI::EDITOR::ENVIRONMENT_PANEL
