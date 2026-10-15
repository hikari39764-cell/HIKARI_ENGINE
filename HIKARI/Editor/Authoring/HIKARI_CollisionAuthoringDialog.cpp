#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Authoring/HIKARI_CollisionAuthoringDialog.h"

#include <algorithm>

#include "imgui.h"

namespace HIKARI::EDITOR {

void CollisionAuthoringDialog::Open(SceneObjectId selectedObject) {
    selectedObject_ = selectedObject;
    scope_ = selectedObject.value != 0u ? 0 : 1;
    shapeSource_ = 0;
    bodyMode_ = 0;
    existingColliderPolicy_ = 0;
    makeTrigger_ = false;
    openRequested_ = true;
}

bool CollisionAuthoringDialog::Draw(CollisionAuthoringRequest& outRequest) {
    if (openRequested_) {
        ImGui::OpenPopup(TEXT::LocalizeWindow("editor.ui.collision.setup.f1b571", "Collision Setup").c_str());
        openRequested_ = false;
    }

    bool applied = false;
    ImGui::SetNextWindowSize(ImVec2(500.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal(TEXT::LocalizeWindow("editor.ui.collision.setup.f1b571", "Collision Setup").c_str(),
                               nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        static const char* scopes[]{"Selected Object", "Scene Geometry"};
        static const char* bodyModes[]{"Static", "Kinematic", "Dynamic"};
        static const char* shapeSources[]{"Fit Primitive to Geometry", "Model Default Collision"};
        static const char* existingPolicies[]{"Keep Existing Colliders", "Configure First Collider"};

        if (selectedObject_.value == 0u) {
            scope_ = 1;
            ImGui::BeginDisabled();
        }
        ImGui::Combo(TEXT::LocalizeWindow("editor.ui.target.61ad50", "Target").c_str(), &scope_, scopes,
                     IM_ARRAYSIZE(scopes));
        if (selectedObject_.value == 0u) {
            ImGui::EndDisabled();
        }
        ImGui::Combo(TEXT::LocalizeWindow("editor.ui.shape.ea5c1a", "Shape").c_str(), &shapeSource_, shapeSources,
                     IM_ARRAYSIZE(shapeSources));
        ImGui::Combo(TEXT::LocalizeWindow("editor.ui.body.718a7e", "Body").c_str(), &bodyMode_, bodyModes,
                     IM_ARRAYSIZE(bodyModes));
        ImGui::Combo(TEXT::LocalizeWindow("editor.ui.existing.0a6f32", "Existing").c_str(), &existingColliderPolicy_,
                     existingPolicies, IM_ARRAYSIZE(existingPolicies));
        ImGui::Checkbox(
            TEXT::LocalizeWindow("editor.ui.new.fitted.collider.is.trigger.5118ad", "New / Fitted Collider Is Trigger")
                .c_str(),
            &makeTrigger_);

        ImGui::Spacing();
        if (ImGui::Button(TEXT::LocalizeWindow("ui.common.apply", "Apply").c_str(), ImVec2(120.0f, 0.0f))) {
            outRequest = {};
            outRequest.scope =
                scope_ == 0 ? CollisionAuthoringScope::SelectedObject : CollisionAuthoringScope::SceneGeometry;
            outRequest.selectedObject = selectedObject_;
            outRequest.bodyMode = static_cast<CollisionBodyMode>(std::clamp(bodyMode_, 0, 2));
            outRequest.shapeSource = shapeSource_ == 1 ? CollisionAuthoringShapeSource::ModelDefaultCollision
                                                       : CollisionAuthoringShapeSource::GeometryFit;
            outRequest.existingColliderPolicy =
                existingColliderPolicy_ == 0 ? ExistingColliderPolicy::Keep : ExistingColliderPolicy::ConfigureFirst;
            outRequest.makeTrigger = makeTrigger_;
            applied = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(TEXT::LocalizeWindow("ui.common.cancel", "Cancel").c_str(), ImVec2(120.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    return applied;
}

} // namespace HIKARI::EDITOR
