#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <cstdio>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Material/Authoring/HIKARI_MaterialInstanceResolver.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#include "imgui.h"

namespace HIKARI::EDITOR {
namespace {

AssetGuid EffectiveResourceGuid(const MaterialAssetData& instance, const MaterialGraphResourceData& resource) {
    const auto found =
        std::find_if(instance.instance.resourceOverrides.begin(), instance.instance.resourceOverrides.end(),
                     [&resource](const auto& entry) { return entry.resourceId == resource.id; });
    return found != instance.instance.resourceOverrides.end() ? found->assetGuid : resource.assetGuid;
}

} // namespace

void ModelMaterialWorkspaceController::DrawInstanceWindow(DocumentSceneBase& scene) {
    if (!ImGui::Begin(TEXT::LocalizeWindow("editor.ui.instance.5f97f8", "ModelMaterial/Instance").c_str())) {
        ImGui::End();
        return;
    }
    if (!authoringDocument_.HasMaterial()) {
        ImGui::TextWrapped(TEXT::Localize("editor.ui.open.a.material.before.creating.an.instance.bb3495").c_str());
        ImGui::End();
        return;
    }

    MaterialAssetData& instance = authoringDocument_.EditMaterial();
    if (instance.kind != MaterialAssetKind::Instance) {
        ImGui::TextWrapped(
            TEXT::Localize("editor.ui.create.a.lightweight.instance.that.inherits.this.material.and.on.3b9f89")
                .c_str());
        if (ImGui::Button(
                TEXT::LocalizeWindow("editor.ui.create.material.instance.85908f", "Create Material Instance").c_str(),
                ImVec2(-1.0f, 0.0f))) {
            (void)CreateMaterial(scene, instance.templateId, true, statusMessage_);
        }
        ImGui::End();
        return;
    }

    ImGui::Text(TEXT::Localize("editor.ui.parent.s.c6d7f1").c_str(),
                instance.instance.parentMaterialGuid.value.c_str());
    const auto* parentSource = LoadInstanceParent(scene.GetAssetDatabase(), statusMessage_);
    if (!parentSource) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.3f, 1.0f),
                           TEXT::Localize("editor.ui.parent.material.is.unavailable.53592c").c_str());
        ImGui::End();
        return;
    }
    const auto& parent = *parentSource;
    ImGui::SeparatorText(TEXT::Localize("editor.ui.parameter.overrides.f96015").c_str());
    DrawMaterialParameterControls(instance, parent.graph, &parent);
    ImGui::SeparatorText(TEXT::Localize("editor.ui.resource.overrides.71f02a").c_str());
    for (const MaterialGraphResourceData& resource : parent.graph.resources) {
        ImGui::PushID(resource.id.c_str());
        ImGui::TextUnformatted(resource.displayName.empty() ? resource.id.c_str() : resource.displayName.c_str());
        const AssetGuid effective = EffectiveResourceGuid(instance, resource);
        char buffer[128]{};
        std::snprintf(buffer, sizeof(buffer), "%s", effective.value.c_str());
        ImGui::SetNextItemWidth(-64.0f);
        if (ImGui::InputTextWithHint("##InstanceResource", "Asset GUID", buffer, sizeof(buffer),
                                     ImGuiInputTextFlags_EnterReturnsTrue)) {
            MaterialAssetData before = instance;
            std::string error{};
            if (ASSETS::MATERIAL::SetMaterialInstanceResourceOverride(instance, parent, resource.id, AssetGuid{buffer},
                                                                      error)) {
                authoringDocument_.CommitMaterial(std::move(before), "Bind instance resource");
            } else {
                statusMessage_ = std::move(error);
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton(TEXT::LocalizeWindow("ui.common.reset", "Reset").c_str())) {
            MaterialAssetData before = instance;
            if (ASSETS::MATERIAL::RestoreMaterialInstanceResourceDefault(instance, resource.id)) {
                authoringDocument_.CommitMaterial(std::move(before), "Reset instance resource");
            }
        }
        ImGui::PopID();
    }

    ImGui::Separator();
    ImGui::BeginDisabled(instance.instance.parameterOverrides.empty() && instance.instance.resourceOverrides.empty());
    if (ImGui::Button(
            TEXT::LocalizeWindow("editor.ui.reset.all.instance.overrides.633aad", "Reset All Instance Overrides")
                .c_str(),
            ImVec2(-1.0f, 0.0f))) {
        MaterialAssetData before = instance;
        instance.instance.parameterOverrides.clear();
        instance.instance.resourceOverrides.clear();
        authoringDocument_.CommitMaterial(std::move(before), "Reset instance overrides");
    }
    ImGui::EndDisabled();
    ImGui::End();
}

} // namespace HIKARI::EDITOR
