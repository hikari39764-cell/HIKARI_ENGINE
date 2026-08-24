#include "Editor/Workspaces/CombatAction/HIKARI_CombatActionWorkspaceController.h"

#include <algorithm>
#include <cstring>
#include <string_view>
#include <utility>
#include <vector>

#include "Editor/Widgets/HIKARI_AssetFieldWidget.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
    namespace {
#if defined(HIKARI_WITH_EDITOR)
        constexpr ImVec4 kAccent{ 1.0f, 0.46f, 0.24f, 1.0f };

        bool InputTextString(
            const char* label,
            std::string& value,
            size_t capacity = 256u) {
            std::vector<char> buffer(
                (std::max)(capacity, value.size() + 2u), '\0');
            if (!value.empty()) {
                std::memcpy(buffer.data(), value.data(), value.size());
            }
            if (!ImGui::InputText(label, buffer.data(), buffer.size())) {
                return false;
            }
            value = buffer.data();
            return true;
        }

        uint64_t MergeGroup(std::string_view name) noexcept {
            uint64_t hash = 1469598103934665603ull;
            for (const char value : name) {
                hash ^= static_cast<unsigned char>(value);
                hash *= 1099511628211ull;
            }
            return hash == 0u ? 1u : hash;
        }

        const char* TrackName(CombatActionTrackKind track) noexcept {
            switch (track) {
            case CombatActionTrackKind::Animation: return "Animation";
            case CombatActionTrackKind::Phase: return "Action Phases";
            case CombatActionTrackKind::Hit: return "Hit Detection";
            case CombatActionTrackKind::Cancel: return "Cancel / Buffer";
            case CombatActionTrackKind::Motion: return "Motion / Warp";
            case CombatActionTrackKind::Defense: return "Defense";
            case CombatActionTrackKind::Vfx: return "VFX Cues";
            case CombatActionTrackKind::Presentation: return "Presentation";
            case CombatActionTrackKind::None:
            default: return "Action";
            }
        }

        template<class T>
        T* FindById(std::vector<T>& values, uint64_t id) {
            const auto found = std::find_if(
                values.begin(), values.end(),
                [id](const T& value) { return value.id == id; });
            return found == values.end() ? nullptr : &*found;
        }
#endif
    }

    void CombatActionWorkspaceController::DrawInspector(
        DocumentSceneBase& scene) {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::Begin("Action Inspector###CombatAction/Inspector")) {
            ImGui::End();
            return;
        }
        ACTION::CombatActionDefinition& definition = document_.Definition();
        ImGui::TextColored(kAccent, "%s", TrackName(selection_.track));
        ImGui::TextDisabled(
            selection_.id == 0u
                ? "Edit the selected track or action settings"
                : "Selected timeline item");
        ImGui::Separator();

        const auto commit = [this](ACTION::CombatActionDefinition before,
                                   std::string_view field) {
            RecordMutation(std::move(before), MergeGroup(field));
        };
        const auto seal = [this]() {
            if (ImGui::IsItemDeactivatedAfterEdit()) document_.SealMerge();
        };

        if (selection_.track == CombatActionTrackKind::None ||
            selection_.track == CombatActionTrackKind::Presentation ||
            selection_.id == 0u) {
            ImGui::SeparatorText("ACTION IDENTITY");
            ACTION::CombatActionDefinition before = definition;
            if (InputTextString("Action Id", definition.actionId)) {
                commit(std::move(before), "action.id");
            }
            seal();
            before = definition;
            if (InputTextString("Display Name", definition.displayName)) {
                commit(std::move(before), "action.name");
            }
            seal();
            before = definition;
            if (ImGui::DragFloat(
                    "Duration", &definition.durationSeconds,
                    0.01f, ACTION::kMinimumCombatActionDurationSeconds,
                    ACTION::kMaximumCombatActionDurationSeconds, "%.3f s")) {
                ACTION::NormalizeCombatAction(definition);
                currentTimeSeconds_ = (std::min)(
                    currentTimeSeconds_, definition.durationSeconds);
                commit(std::move(before), "action.duration");
            }
            seal();
            int fps = static_cast<int>(definition.displayFramesPerSecond);
            before = definition;
            if (ImGui::SliderInt("Display FPS", &fps, 12, 120)) {
                definition.displayFramesPerSecond = static_cast<uint32_t>(fps);
                commit(std::move(before), "action.fps");
            }
            seal();
            ImGui::SeparatorText("GAMEPLAY COST");
            before = definition;
            if (ImGui::DragFloat(
                    "Stamina Cost", &definition.staminaCost,
                    0.1f, 0.0f, 999.0f)) {
                commit(std::move(before), "action.stamina");
            }
            seal();
            before = definition;
            if (ImGui::DragFloat(
                    "Cooldown", &definition.cooldownSeconds,
                    0.01f, 0.0f, 120.0f, "%.2f s")) {
                commit(std::move(before), "action.cooldown");
            }
            seal();
            ImGui::SeparatorText("PREVIEW CAST");
            std::string attacker = definition.previewAttackerModelGuid.value;
            before = definition;
            if (DrawAssetField(
                    &scene.GetAssetDatabase(),
                    { "Attacker Model", AssetType::Model }, attacker)) {
                definition.previewAttackerModelGuid.value = std::move(attacker);
                commit(std::move(before), "action.previewAttacker");
            }
            std::string victim = definition.previewVictimModelGuid.value;
            before = definition;
            if (DrawAssetField(
                    &scene.GetAssetDatabase(),
                    { "Victim Model", AssetType::Model }, victim)) {
                definition.previewVictimModelGuid.value = std::move(victim);
                commit(std::move(before), "action.previewVictim");
            }
            ImGui::SeparatorText("PRESENTATION");
            std::string sequence = definition.presentation.sequenceAssetGuid.value;
            before = definition;
            if (DrawAssetField(
                    &scene.GetAssetDatabase(),
                    { "Camera Sequence", AssetType::Sequence }, sequence)) {
                definition.presentation.sequenceAssetGuid.value =
                    std::move(sequence);
                commit(std::move(before), "action.sequence");
            }
            before = definition;
            if (InputTextString(
                    "Camera Reaction",
                    definition.presentation.cameraReactionProfile)) {
                commit(std::move(before), "action.cameraReaction");
            }
            seal();
        } else if (selection_.track == CombatActionTrackKind::Animation) {
            auto* value = FindById(definition.animationSegments, selection_.id);
            if (value != nullptr) {
                ACTION::CombatActionDefinition before = definition;
                if (InputTextString("Clip", value->clipName)) {
                    commit(std::move(before), "animation.clip");
                }
                seal();
                before = definition;
                if (ImGui::DragFloat(
                        "Start", &value->startTimeSeconds, 0.005f,
                        0.0f, definition.durationSeconds, "%.3f s")) {
                    ACTION::NormalizeCombatAction(definition);
                    commit(std::move(before), "animation.start");
                }
                seal();
                before = definition;
                if (ImGui::DragFloat(
                        "Duration", &value->durationSeconds, 0.005f,
                        0.001f, definition.durationSeconds, "%.3f s")) {
                    ACTION::NormalizeCombatAction(definition);
                    commit(std::move(before), "animation.duration");
                }
                seal();
                before = definition;
                if (ImGui::DragFloat(
                        "Source Start", &value->sourceStartSeconds,
                        0.005f, 0.0f, 120.0f, "%.3f s")) {
                    commit(std::move(before), "animation.sourceStart");
                }
                seal();
                before = definition;
                if (ImGui::SliderFloat(
                        "Playback Rate", &value->playbackRate,
                        0.1f, 3.0f, "%.2fx")) {
                    commit(std::move(before), "animation.rate");
                }
                seal();
                before = definition;
                if (ImGui::DragFloat(
                        "Blend In", &value->blendInSeconds,
                        0.005f, 0.0f, value->durationSeconds, "%.3f s")) {
                    commit(std::move(before), "animation.blendIn");
                }
                seal();
                before = definition;
                if (ImGui::DragFloat(
                        "Blend Out", &value->blendOutSeconds,
                        0.005f, 0.0f, value->durationSeconds, "%.3f s")) {
                    commit(std::move(before), "animation.blendOut");
                }
                seal();
            }
        } else if (selection_.track == CombatActionTrackKind::Phase) {
            auto* value = FindById(definition.phases, selection_.id);
            if (value != nullptr) {
                const char* names[]{ "Startup", "Active", "Recovery" };
                int kind = static_cast<int>(value->kind);
                ACTION::CombatActionDefinition before = definition;
                if (ImGui::Combo("Phase", &kind, names, 3)) {
                    value->kind =
                        static_cast<ACTION::CombatActionPhaseKind>(kind);
                    commit(std::move(before), "phase.kind");
                }
                before = definition;
                if (ImGui::DragFloatRange2(
                        "Window", &value->startTimeSeconds,
                        &value->endTimeSeconds, 0.005f, 0.0f,
                        definition.durationSeconds, "%.3f", "%.3f")) {
                    ACTION::NormalizeCombatAction(definition);
                    commit(std::move(before), "phase.window");
                }
                seal();
            }
        } else if (selection_.track == CombatActionTrackKind::Hit) {
            auto* value = FindById(definition.hitWindows, selection_.id);
            if (value != nullptr) {
                ACTION::CombatActionDefinition before = definition;
                if (InputTextString("Label", value->label)) {
                    commit(std::move(before), "hit.label");
                }
                seal();
                before = definition;
                if (InputTextString("Socket", value->socketName)) {
                    commit(std::move(before), "hit.socket");
                }
                seal();
                const char* names[]{ "Sphere", "Capsule", "Box" };
                int shape = static_cast<int>(value->shape);
                before = definition;
                if (ImGui::Combo("Shape", &shape, names, 3)) {
                    value->shape =
                        static_cast<ACTION::CombatHitShapeKind>(shape);
                    commit(std::move(before), "hit.shape");
                }
                before = definition;
                if (ImGui::DragFloatRange2(
                        "Window", &value->startTimeSeconds,
                        &value->endTimeSeconds, 0.005f, 0.0f,
                        definition.durationSeconds, "%.3f", "%.3f")) {
                    ACTION::NormalizeCombatAction(definition);
                    commit(std::move(before), "hit.window");
                }
                seal();
                before = definition;
                if (ImGui::DragFloat3(
                        "Local Offset", &value->localOffset.x, 0.01f)) {
                    commit(std::move(before), "hit.offset");
                }
                seal();
                if (value->shape == ACTION::CombatHitShapeKind::Box) {
                    before = definition;
                    if (ImGui::DragFloat3(
                            "Half Extents", &value->halfExtents.x,
                            0.01f, 0.001f, 20.0f)) {
                        commit(std::move(before), "hit.extents");
                    }
                    seal();
                } else {
                    before = definition;
                    if (ImGui::DragFloat(
                            "Radius", &value->radius,
                            0.01f, 0.001f, 20.0f)) {
                        commit(std::move(before), "hit.radius");
                    }
                    seal();
                    if (value->shape == ACTION::CombatHitShapeKind::Capsule) {
                        before = definition;
                        if (ImGui::DragFloat(
                                "Half Height", &value->halfHeight,
                                0.01f, value->radius, 20.0f)) {
                            commit(std::move(before), "hit.height");
                        }
                        seal();
                    }
                }
                before = definition;
                if (ImGui::DragFloat(
                        "Damage Scale", &value->damageScale,
                        0.01f, 0.0f, 20.0f)) {
                    commit(std::move(before), "hit.damage");
                }
                seal();
                before = definition;
                if (ImGui::DragFloat(
                        "Posture Scale", &value->postureDamageScale,
                        0.01f, 0.0f, 20.0f)) {
                    commit(std::move(before), "hit.posture");
                }
                seal();
            }
        } else if (selection_.track == CombatActionTrackKind::Cancel) {
            auto* value = FindById(definition.cancelWindows, selection_.id);
            if (value != nullptr) {
                ACTION::CombatActionDefinition before = definition;
                if (InputTextString("Label", value->label)) {
                    commit(std::move(before), "cancel.label");
                }
                seal();
                before = definition;
                if (ImGui::DragFloatRange2(
                        "Window", &value->startTimeSeconds,
                        &value->endTimeSeconds, 0.005f, 0.0f,
                        definition.durationSeconds, "%.3f", "%.3f")) {
                    ACTION::NormalizeCombatAction(definition);
                    commit(std::move(before), "cancel.window");
                }
                seal();
                ImGui::TextDisabled(
                    "Allowed action tags are serialized as a stable list.\n"
                    "Tag picker integration is the next catalog-backed layer.");
            }
        } else if (selection_.track == CombatActionTrackKind::Motion) {
            auto* value = FindById(definition.motionWindows, selection_.id);
            if (value != nullptr) {
                const char* names[]{
                    "None", "Authored Velocity", "Root Motion", "Motion Warp" };
                int mode = static_cast<int>(value->mode);
                ACTION::CombatActionDefinition before = definition;
                if (ImGui::Combo("Mode", &mode, names, 4)) {
                    value->mode = static_cast<ACTION::CombatMotionMode>(mode);
                    commit(std::move(before), "motion.mode");
                }
                before = definition;
                if (ImGui::DragFloatRange2(
                        "Window", &value->startTimeSeconds,
                        &value->endTimeSeconds, 0.005f, 0.0f,
                        definition.durationSeconds, "%.3f", "%.3f")) {
                    ACTION::NormalizeCombatAction(definition);
                    commit(std::move(before), "motion.window");
                }
                seal();
                before = definition;
                if (ImGui::DragFloat3(
                        "Velocity", &value->velocity.x, 0.05f)) {
                    commit(std::move(before), "motion.velocity");
                }
                seal();
                if (value->mode == ACTION::CombatMotionMode::MotionWarp) {
                    before = definition;
                    if (InputTextString(
                            "Warp Target Slot", value->warpTargetSlot)) {
                        commit(std::move(before), "motion.target");
                    }
                    seal();
                    before = definition;
                    if (ImGui::SliderFloat(
                            "Rotation Weight", &value->rotationWeight,
                            0.0f, 1.0f)) {
                        commit(std::move(before), "motion.rotation");
                    }
                    seal();
                }
            }
        } else if (selection_.track == CombatActionTrackKind::Defense) {
            auto* value = FindById(definition.defenseWindows, selection_.id);
            if (value != nullptr) {
                ACTION::CombatActionDefinition before = definition;
                if (ImGui::DragFloatRange2(
                        "Window", &value->startTimeSeconds,
                        &value->endTimeSeconds, 0.005f, 0.0f,
                        definition.durationSeconds, "%.3f", "%.3f")) {
                    ACTION::NormalizeCombatAction(definition);
                    commit(std::move(before), "defense.window");
                }
                seal();
                before = definition;
                if (ImGui::Checkbox("Invulnerable", &value->invulnerable)) {
                    commit(std::move(before), "defense.invulnerable");
                }
                before = definition;
                if (ImGui::Checkbox("Super Armor", &value->superArmor)) {
                    commit(std::move(before), "defense.superArmor");
                }
                before = definition;
                if (ImGui::SliderFloat(
                        "Damage Reduction", &value->damageReduction,
                        0.0f, 1.0f)) {
                    commit(std::move(before), "defense.reduction");
                }
                seal();
            }
        } else if (selection_.track == CombatActionTrackKind::Vfx) {
            auto* value = FindById(
                definition.presentation.vfxCues, selection_.id);
            if (value != nullptr) {
                ACTION::CombatActionDefinition before = definition;
                std::string guid = value->assetGuid.value;
                if (DrawAssetField(
                        &scene.GetAssetDatabase(),
                        { "VFX Asset", AssetType::VfxEffect }, guid)) {
                    value->assetGuid.value = std::move(guid);
                    commit(std::move(before), "vfx.asset");
                }
                before = definition;
                if (InputTextString("Socket", value->socketName)) {
                    commit(std::move(before), "vfx.socket");
                }
                seal();
                before = definition;
                if (ImGui::DragFloat(
                        "Time", &value->timeSeconds, 0.005f,
                        0.0f, definition.durationSeconds, "%.3f s")) {
                    ACTION::NormalizeCombatAction(definition);
                    commit(std::move(before), "vfx.time");
                }
                seal();
                before = definition;
                if (ImGui::DragFloat(
                        "Scale", &value->scale, 0.01f, 0.0f, 100.0f)) {
                    commit(std::move(before), "vfx.scale");
                }
                seal();
            }
        }

        if (selection_.id != 0u &&
            selection_.track != CombatActionTrackKind::Presentation) {
            ImGui::Separator();
            if (ImGui::Button(
                    "Delete Selected Item", ImVec2(-1.0f, 0.0f))) {
                DeleteSelection();
            }
        }
        ImGui::End();
#else
        (void)scene;
#endif
    }

} // namespace HIKARI::EDITOR
