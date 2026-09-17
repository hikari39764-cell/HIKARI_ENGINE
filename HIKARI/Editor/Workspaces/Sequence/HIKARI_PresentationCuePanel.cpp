#include "HIKARI_PresentationCuePanel.h"
#include "Editor/Widgets/HIKARI_AssetFieldWidget.h"
#include "Scene/HIKARI_CinematicSequence.h"
#include <algorithm>
#include <array>
#include <cstring>
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
    bool DrawPresentationCuePanel(CinematicSequence& sequence,
        const AssetDatabase& assets, bool editingAllowed) {
#if defined(HIKARI_WITH_EDITOR)
        using namespace SEQUENCER;
        if (!ImGui::CollapsingHeader("Sound and VFX cues")) return false;
        ImGui::TextWrapped("Cue intervals share the camera sequence clock. Runtime pause, seek and stop apply to all cues. Reverse playback mutes these cues.");
        bool changed = false;
        auto& track = sequence.presentationCueTrack;
        ImGui::BeginDisabled(!editingAllowed);
        changed |= ImGui::Checkbox("Enable presentation cues", &track.enabled);
        ImGui::BeginDisabled(track.clips.size() >= kMaximumPresentationCues);
        const bool soundAdded = ImGui::Button("Add sound");
        ImGui::SameLine();
        const bool vfx = ImGui::Button("Add VFX");
        if (soundAdded || vfx) {
            PresentationCueClip clip;
            while (std::any_of(track.clips.begin(), track.clips.end(),
                [&](const auto& existing) { return existing.id == clip.id; })) ++clip.id;
            clip.kind = vfx ? PresentationCueKind::Vfx : PresentationCueKind::Audio;
            clip.name = vfx ? "VFX cue" : "Sound cue";
            clip.soundPath = "Assets/Audio/";
            track.clips.push_back(std::move(clip));
            changed = true;
        }
        ImGui::EndDisabled();
        uint64_t remove = 0u;
        for (auto& clip : track.clips) {
            ImGui::PushID(static_cast<int>(clip.id));
            if (ImGui::TreeNode("cue", "%s [%llu]", clip.name.c_str(), clip.id)) {
                std::array<char, 1025> buffer{};
                std::memcpy(buffer.data(), clip.name.data(), (std::min)(clip.name.size(), buffer.size()-1u));
                if (ImGui::InputText("Name", buffer.data(), buffer.size())) { clip.name = buffer.data(); changed = true; }
                changed |= ImGui::DragFloat("Start seconds", &clip.startSeconds, 0.05f, 0.0f, kMaxSequenceDurationSeconds, "%.2f");
                changed |= ImGui::DragFloat("Duration seconds", &clip.durationSeconds, 0.05f, 0.01f, kMaxSequenceDurationSeconds, "%.2f");
                const auto* selected = FindSequenceBinding(sequence.bindings, clip.targetBinding);
                if (ImGui::BeginCombo("Target binding", selected ? selected->name.c_str() : "World origin / 2D sound")) {
                    if (ImGui::Selectable("World origin / 2D sound", !clip.targetBinding.IsValid())) { clip.targetBinding = {}; changed = true; }
                    for (const auto& binding : sequence.bindings) {
                        ImGui::PushID(static_cast<int>(binding.id.value));
                        if (ImGui::Selectable(binding.name.c_str(), binding.id == clip.targetBinding)) { clip.targetBinding = binding.id; changed = true; }
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
                if (clip.kind == PresentationCueKind::Vfx) {
                    AssetFieldOptions options;
                    options.label = "Effect"; options.requiredType = AssetType::VfxEffect;
                    changed |= DrawAssetField(&assets, options, clip.vfxAsset.value);
                } else {
                    buffer.fill(0);
                    std::memcpy(buffer.data(), clip.soundPath.data(), (std::min)(clip.soundPath.size(), buffer.size()-1u));
                    if (ImGui::InputText("Sound path", buffer.data(), buffer.size())) { clip.soundPath = buffer.data(); changed = true; }
                    ImGui::TextDisabled("Project-relative WAV/MP3/etc. below Assets/Audio");
                    int bus = static_cast<int>(clip.audio.bus);
                    if (ImGui::Combo("Bus", &bus, "Music\0Effects\0Voice\0")) { clip.audio.bus = static_cast<GameProjectAudioBus>(bus); changed = true; }
                    changed |= ImGui::SliderFloat("Volume", &clip.audio.volume, 0.0f, 1.0f);
                    bool spatial = clip.audio.spatial != 0u;
                    if (ImGui::Checkbox("3D mono source", &spatial)) { clip.audio.spatial = spatial ? 1u : 0u; changed = true; }
                    if (spatial) {
                        changed |= ImGui::DragFloat("Full-volume distance", &clip.audio.source.minimumDistance, 0.1f, 0.01f, 1000.0f);
                        changed |= ImGui::DragFloat("Silent distance", &clip.audio.source.maximumDistance, 0.1f, 0.02f, 10000.0f);
                    }
                }
                if (ImGui::SmallButton("Remove cue")) remove = clip.id;
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (remove) { std::erase_if(track.clips, [&](const auto& cue) { return cue.id == remove; }); changed = true; }
        ImGui::EndDisabled();
        std::string error;
        if (!ValidatePresentationCueTrack(track, error)) ImGui::TextWrapped("%s", error.c_str());
        if (changed) NormalizeCinematicSequence(sequence);
        return changed;
#else
        (void)sequence; (void)assets; (void)editingAllowed; return false;
#endif
    }
}
