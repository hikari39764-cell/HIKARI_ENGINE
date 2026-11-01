#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/AnimationStateMachine/HIKARI_AnimationStateMachineWorkspaceController.h"

#include <algorithm>
#include <string>
#include <utility>

#include "Animation/Runtime/HIKARI_AnimationClipSampler.h"
#include "Animation/StateMachine/HIKARI_AnimationStateMotionEvaluator.h"
#include "RenderContracts/Model/HIKARI_ModelAsset.h"
#include "Assets/Models/HIKARI_ModelManager.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#include "imgui.h"

namespace HIKARI::EDITOR {
namespace {
ModelAsset* ResolvePreviewModel(DocumentSceneBase& scene,
                                const ANIMATION::AnimationStateMachineDefinition& definition) {
    if (definition.previewModelAssetId.value.empty())
        return nullptr;
    ModelAsset* model = scene.GetModelManager().FindAsset(definition.previewModelAssetId.value);
    if (model == nullptr || model->GetState() != ModelAsset::State::Loaded) {
        (void)scene.GetModelManager().LoadAssetNow(definition.previewModelAssetId.value);
        model = scene.GetModelManager().FindAsset(definition.previewModelAssetId.value);
    }
    return model != nullptr && model->GetState() == ModelAsset::State::Loaded ? model : nullptr;
}

bool DrawClipPicker(const char* label, const ModelAsset& model, ANIMATION::AnimationClipReference& reference) {
    const char* preview = reference.fallbackName.empty() ? "<select animation>" : reference.fallbackName.c_str();
    bool changed = false;
    if (ImGui::BeginCombo(label, preview)) {
        for (size_t index = 0u; index < model.animations.size(); ++index) {
            const AnimationClip& clip = model.animations[index];
            const bool selected = model.GetAnimationClipId(index) == reference.clipId;
            const std::string itemLabel =
                clip.name + "  (" + std::to_string(clip.durationSec) + " s)##" + label + std::to_string(index);
            if (ImGui::Selectable(itemLabel.c_str(), selected)) {
                reference = ANIMATION::MakeAnimationClipReference(model, index);
                changed = true;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

const char* MotionTypeName(ANIMATION::AnimationStateMotionType type) noexcept {
    return type == ANIMATION::AnimationStateMotionType::BlendTree1D ? "1D Blend Tree" : "Animation Clip";
}
} // namespace

void AnimationStateMachineWorkspaceController::DrawSelectedStateMotionEditor(DocumentSceneBase& scene,
                                                                             ANIMATION::AnimationState& state) {
    auto& definition = document_.Definition();
    ImGui::SeparatorText(TEXT::Localize("editor.ui.motion.e040db").c_str());

    ANIMATION::AnimationStateMotionType motionType = ANIMATION::GetAnimationStateMotionType(state.motion);
    if (ImGui::BeginCombo(TEXT::LocalizeWindow("editor.ui.motion.type.f64f2e", "Motion Type").c_str(),
                          MotionTypeName(motionType))) {
        constexpr ANIMATION::AnimationStateMotionType types[]{ANIMATION::AnimationStateMotionType::Clip,
                                                              ANIMATION::AnimationStateMotionType::BlendTree1D};
        for (const auto candidate : types) {
            const bool selected = candidate == motionType;
            if (ImGui::Selectable(MotionTypeName(candidate), selected) && !selected) {
                auto before = definition;
                if (candidate == ANIMATION::AnimationStateMotionType::BlendTree1D) {
                    ANIMATION::AnimationBlendTree1DMotion blendTree{};
                    const auto parameter = std::find_if(definition.parameters.begin(), definition.parameters.end(),
                                                        [](const ANIMATION::AnimationParameterDefinition& p) {
                                                            return p.type == ANIMATION::AnimationParameterType::Float;
                                                        });
                    if (parameter != definition.parameters.end()) {
                        blendTree.parameterId = parameter->id;
                    }
                    state.motion = std::move(blendTree);
                } else {
                    state.motion = ANIMATION::AnimationClipMotion{};
                }
                RecordMutation(std::move(before));
                return;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ModelAsset* model = ResolvePreviewModel(scene, definition);
    if (model == nullptr || model->animations.empty()) {
        ImGui::TextDisabled(
            TEXT::Localize("editor.ui.choose.an.animated.model.above.to.select.motion.clips.d1f154").c_str());
    }

    if (auto* clipMotion = std::get_if<ANIMATION::AnimationClipMotion>(&state.motion)) {
        if (model != nullptr && !model->animations.empty()) {
            auto before = definition;
            if (DrawClipPicker("Animation Clip", *model, clipMotion->clip)) {
                RecordMutation(std::move(before));
            }
        }
        return;
    }

    auto* blendTree = std::get_if<ANIMATION::AnimationBlendTree1DMotion>(&state.motion);
    if (blendTree == nullptr)
        return;

    const ANIMATION::AnimationParameterDefinition* selectedParameter =
        ANIMATION::FindAnimationParameter(definition, blendTree->parameterId);
    const char* parameterPreview =
        selectedParameter != nullptr ? selectedParameter->name.c_str() : "<select Float parameter>";
    if (ImGui::BeginCombo(TEXT::LocalizeWindow("editor.ui.blend.parameter.d4e14e", "Blend Parameter").c_str(),
                          parameterPreview)) {
        for (const auto& parameter : definition.parameters) {
            if (parameter.type != ANIMATION::AnimationParameterType::Float) {
                continue;
            }
            const bool selected = parameter.id == blendTree->parameterId;
            if (ImGui::Selectable(parameter.name.c_str(), selected)) {
                auto before = definition;
                blendTree->parameterId = parameter.id;
                RecordMutation(std::move(before));
                return;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (selectedParameter == nullptr) {
        if (ImGui::Button(
                TEXT::LocalizeWindow("editor.ui.create.float.parameter.385587", "Create Float Parameter").c_str())) {
            auto before = definition;
            ANIMATION::AnimationParameterDefinition parameter{};
            parameter.id = ANIMATION::AllocateAnimationParameterId(definition);
            parameter.name = "Blend Value";
            parameter.type = ANIMATION::AnimationParameterType::Float;
            parameter.source = ANIMATION::AnimationParameterSource::Manual;
            parameter.defaultValue = 0.0f;
            blendTree->parameterId = parameter.id;
            definition.parameters.push_back(std::move(parameter));
            RecordMutation(std::move(before));
            return;
        }
    }

    ImGui::Spacing();
    if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.blend.sample.83c942", "+ Blend Sample").c_str())) {
        auto before = definition;
        ANIMATION::AnimationBlendTree1DSample sample{};
        sample.threshold = blendTree->samples.empty() ? 0.0f : blendTree->samples.back().threshold + 1.0f;
        if (model != nullptr && !model->animations.empty()) {
            sample.clip = ANIMATION::MakeAnimationClipReference(*model, 0u);
        }
        blendTree->samples.push_back(std::move(sample));
        RecordMutation(std::move(before));
        return;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(blendTree->samples.size() < 2u);
    if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.evenly.space.c180a5", "Evenly Space").c_str())) {
        auto before = definition;
        const float first = blendTree->samples.front().threshold;
        float last = blendTree->samples.back().threshold;
        if (last <= first) {
            last = first + static_cast<float>(blendTree->samples.size() - 1u);
        }
        const float step = (last - first) / static_cast<float>(blendTree->samples.size() - 1u);
        for (size_t index = 0u; index < blendTree->samples.size(); ++index) {
            blendTree->samples[index].threshold = first + step * static_cast<float>(index);
        }
        RecordMutation(std::move(before));
        return;
    }
    ImGui::EndDisabled();

    if (blendTree->samples.empty()) {
        ImGui::TextDisabled(
            TEXT::Localize("editor.ui.add.at.least.one.sample.two.or.more.clips.create.a.blend.34bddb").c_str());
        return;
    }

    constexpr ImGuiTableFlags tableFlags = ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_BordersOuter |
                                           ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp;
    if (!ImGui::BeginTable("BlendSamples", 4, tableFlags))
        return;
    ImGui::TableSetupColumn("Threshold", ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn("Animation");
    ImGui::TableSetupColumn("Speed", ImGuiTableColumnFlags_WidthFixed, 76.0f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 28.0f);
    ImGui::TableHeadersRow();

    for (size_t index = 0u; index < blendTree->samples.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        auto& sample = blendTree->samples[index];
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::SetNextItemWidth(-1.0f);
        auto before = definition;
        if (ImGui::DragFloat("##Threshold", &sample.threshold, 0.02f)) {
            RecordMutation(std::move(before), 12000u + state.id.value * 128u + index);
            ImGui::PopID();
            ImGui::EndTable();
            return;
        }
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
        if (model != nullptr && !model->animations.empty()) {
            before = definition;
            if (DrawClipPicker("##Clip", *model, sample.clip)) {
                RecordMutation(std::move(before));
                ImGui::PopID();
                ImGui::EndTable();
                return;
            }
        } else {
            ImGui::TextDisabled(TEXT::Localize("editor.ui.no.model.clips.a3b702").c_str());
        }
        ImGui::TableSetColumnIndex(2);
        ImGui::SetNextItemWidth(-1.0f);
        before = definition;
        if (ImGui::DragFloat("##Speed", &sample.speedScale, 0.02f, -8.0f, 8.0f)) {
            RecordMutation(std::move(before), 14000u + state.id.value * 128u + index);
            ImGui::PopID();
            ImGui::EndTable();
            return;
        }
        ImGui::TableSetColumnIndex(3);
        if (ImGui::SmallButton("X")) {
            before = definition;
            blendTree->samples.erase(blendTree->samples.begin() + index);
            RecordMutation(std::move(before));
            ImGui::PopID();
            ImGui::EndTable();
            return;
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

} // namespace HIKARI::EDITOR
