#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"
#include "Assets/Vfx/HIKARI_VfxEnumNames.h"

#include <array>
#include <string_view>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxInspectorControls.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxParticleMaterialField.h"
#include "Render3D/Vfx/HIKARI_ParticleRenderInput.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
void VfxWorkspaceController::DrawEmitterAdvanced(AssetDatabase& database, const VFX::VfxEmitterData& emitter) {
#if defined(HIKARI_WITH_EDITOR)
    ImGui::TextWrapped(TEXT::Localize("editor.ui.layer.id.s.e916c5").c_str(), emitter.id.c_str());
    VfxInspectorPropertyEditor orderEditor(*this, database, emitter.id, statusMessage_);
    orderEditor.Unsigned(VfxUiText("Sibling order").c_str(), "order", emitter.order);
    ImGui::SeparatorText(VfxUiText("Attachment / Follow").c_str());
    VfxInspectorPropertyEditor attachmentEditor(*this, database, emitter.attachment.id, statusMessage_);
    attachmentEditor.Choice(VfxUiText("Attachment Mode").c_str(), "mode", VFX::ToString(emitter.attachment.mode),
                            VFX::kVfxAttachmentModeChoices.data(), VFX::kVfxAttachmentModeChoices.size());
    attachmentEditor.Choice(VfxUiText("Follow").c_str(), "follow", VFX::ToString(emitter.attachment.follow),
                            VFX::kVfxFollowModeChoices.data(), VFX::kVfxFollowModeChoices.size());
    if (emitter.attachment.mode == VFX::VfxAttachmentMode::OwnerSocket) {
        attachmentEditor.Text(VfxUiText("Socket Name").c_str(), "socketName", emitter.attachment.socketName);
    }
    attachmentEditor.Vec3(VfxUiText("Local Offset").c_str(), "offset.position", emitter.attachment.offset.position);
    attachmentEditor.Vec3(VfxUiText("Local Rotation (degrees)").c_str(), "offset.rotationEulerDegrees",
                          emitter.attachment.offset.rotationEulerDegrees, 0.1f);
    attachmentEditor.Vec3(VfxUiText("Local Scale").c_str(), "offset.scale", emitter.attachment.offset.scale);

    ImGui::SeparatorText(VfxUiText("Typed Special Outputs").c_str());
    if (emitter.specialOutputs.empty()) {
        ImGui::TextDisabled(
            VfxUiText("No special output profiles. Add one to expose the typed renderer profile.").c_str());
    } else if (selection_.kind == VfxWorkspaceSelectionKind::SpecialOutput) {
        ImGui::TextDisabled(TEXT::Localize("editor.ui.editing.selected.special.output.profile.s.008a05").c_str(),
                            selection_.targetId.c_str());
    }
    bool removeOutput = false;
    std::string removeOutputId{};
    for (const VFX::VfxSpecialOutputData& output : emitter.specialOutputs) {
        if (selection_.kind == VfxWorkspaceSelectionKind::SpecialOutput && selection_.targetId != output.id) {
            continue;
        }
        ImGui::PushID(output.id.c_str());
        ImGui::Separator();
        if (ImGui::Selectable(output.id.c_str(), selection_.kind == VfxWorkspaceSelectionKind::SpecialOutput &&
                                                     selection_.targetId == output.id)) {
            Select({VfxWorkspaceSelectionKind::SpecialOutput, output.id, emitter.id});
        }
        VfxInspectorPropertyEditor outputEditor(*this, database, output.id, statusMessage_);
        outputEditor.Bool(VfxUiText("Enabled").c_str(), "enabled", output.enabled);
        outputEditor.Choice(VfxUiText("Profile").c_str(), "type", VFX::ToString(output.type),
                            VFX::kVfxSpecialOutputTypeChoices.data(), VFX::kVfxSpecialOutputTypeChoices.size());
        switch (output.type) {
        case VFX::VfxSpecialOutputType::ScreenImpulse:
            outputEditor.Float(VfxUiText("Duration").c_str(), "screenImpulse.durationSeconds",
                               output.screenImpulse.durationSeconds, 0.01f, VFX::kVfxScreenDurationMin,
                               VFX::kVfxScreenDurationMax);
            outputEditor.Float(VfxUiText("Max Radius").c_str(), "screenImpulse.maxRadius",
                               output.screenImpulse.maxRadius, 0.01f, VFX::kVfxScreenRadiusMin,
                               VFX::kVfxScreenRadiusMax);
            outputEditor.Float(VfxUiText("Ring Thickness").c_str(), "screenImpulse.ringThickness",
                               output.screenImpulse.ringThickness, 0.001f, VFX::kVfxScreenRingThicknessMin,
                               VFX::kVfxScreenRingThicknessMax);
            outputEditor.Float(VfxUiText("Distortion").c_str(), "screenImpulse.distortion",
                               output.screenImpulse.distortion, 0.01f, VFX::kVfxScreenDistortionMin,
                               VFX::kVfxScreenDistortionMax);
            outputEditor.Float(VfxUiText("Chromatic Aberration").c_str(), "screenImpulse.chromaticAberration",
                               output.screenImpulse.chromaticAberration, 0.001f, VFX::kVfxScreenChromaticMin,
                               VFX::kVfxScreenChromaticMax);
            outputEditor.Float(VfxUiText("Flash").c_str(), "screenImpulse.flash", output.screenImpulse.flash, 0.01f,
                               VFX::kVfxScreenFlashMin, VFX::kVfxScreenFlashMax);
            outputEditor.Float(VfxUiText("Intensity").c_str(), "screenImpulse.intensity",
                               output.screenImpulse.intensity, 0.01f, VFX::kVfxScreenIntensityMin,
                               VFX::kVfxScreenIntensityMax);
            outputEditor.Color("Color", "screenImpulse.color", output.screenImpulse.color);
            outputEditor.Float(VfxUiText("Wave Frequency").c_str(), "screenImpulse.waveFrequency",
                               output.screenImpulse.waveFrequency, 0.01f, VFX::kVfxScreenWaveFrequencyMin,
                               VFX::kVfxScreenWaveFrequencyMax);
            outputEditor.Float(VfxUiText("Wave Amplitude").c_str(), "screenImpulse.waveAmplitude",
                               output.screenImpulse.waveAmplitude, 0.01f, VFX::kVfxScreenWaveAmplitudeMin,
                               VFX::kVfxScreenWaveAmplitudeMax);
            outputEditor.Float(VfxUiText("Wave Damping").c_str(), "screenImpulse.waveDamping",
                               output.screenImpulse.waveDamping, 0.01f, VFX::kVfxScreenWaveDampingMin,
                               VFX::kVfxScreenWaveDampingMax);
            outputEditor.Unsigned(VfxUiText("Wave Count").c_str(), "screenImpulse.waveCount",
                                  output.screenImpulse.waveCount, VFX::kVfxScreenImpulseWaveCountMin,
                                  VFX::kVfxScreenImpulseWaveCountMax);
            break;
        case VFX::VfxSpecialOutputType::SkinnedAfterimage:
            outputEditor.Unsigned(VfxUiText("Sample Count").c_str(), "skinnedAfterimage.sampleCount",
                                  output.skinnedAfterimage.sampleCount, VFX::kVfxAfterimageSampleCountMin,
                                  VFX::kVfxAfterimageSampleCountMax);
            outputEditor.Float(VfxUiText("Lifetime").c_str(), "skinnedAfterimage.lifetimeSeconds",
                               output.skinnedAfterimage.lifetimeSeconds, 0.01f, VFX::kVfxAfterimageLifetimeMin,
                               VFX::kVfxAfterimageLifetimeMax);
            outputEditor.Float(VfxUiText("Opacity").c_str(), "skinnedAfterimage.opacity",
                               output.skinnedAfterimage.opacity, 0.01f, VFX::kVfxAfterimageOpacityMin,
                               VFX::kVfxAfterimageOpacityMax);
            outputEditor.Float(VfxUiText("Fresnel").c_str(), "skinnedAfterimage.fresnel",
                               output.skinnedAfterimage.fresnel, 0.01f, VFX::kVfxAfterimageFresnelMin,
                               VFX::kVfxAfterimageFresnelMax);
            outputEditor.Float(VfxUiText("Dissolve").c_str(), "skinnedAfterimage.dissolve",
                               output.skinnedAfterimage.dissolve, 0.01f, VFX::kVfxAfterimageDissolveMin,
                               VFX::kVfxAfterimageDissolveMax);
            outputEditor.Float(VfxUiText("Shell Offset").c_str(), "skinnedAfterimage.shellOffset",
                               output.skinnedAfterimage.shellOffset, 0.001f, VFX::kVfxAfterimageShellOffsetMin,
                               VFX::kVfxAfterimageShellOffsetMax);
            outputEditor.Color("Color", "skinnedAfterimage.color", output.skinnedAfterimage.color);
            outputEditor.Float(VfxUiText("Intensity").c_str(), "skinnedAfterimage.intensity",
                               output.skinnedAfterimage.intensity, 0.01f, VFX::kVfxAfterimageIntensityMin,
                               VFX::kVfxAfterimageIntensityMax);
            break;
        case VFX::VfxSpecialOutputType::Light:
            outputEditor.Color("Color", "light.color", output.light.color);
            outputEditor.Float(VfxUiText("Luminous Flux (lm)").c_str(), "light.luminousFlux", output.light.luminousFlux,
                               100.0f, VFX::kVfxLightFluxMin, VFX::kVfxLightFluxMax);
            outputEditor.Float(VfxUiText("Range").c_str(), "light.range", output.light.range, 0.05f,
                               VFX::kVfxLightRangeMin, VFX::kVfxLightRangeMax);
            outputEditor.Float(VfxUiText("Source Radius").c_str(), "light.sourceRadius", output.light.sourceRadius,
                               0.01f, VFX::kVfxLightSourceRadiusMin, VFX::kVfxLightSourceRadiusMax);
            outputEditor.Float(VfxUiText("Flicker Frequency").c_str(), "light.flickerFrequency",
                               output.light.flickerFrequency, 0.1f, VFX::kVfxLightFlickerFrequencyMin,
                               VFX::kVfxLightFlickerFrequencyMax);
            outputEditor.Float(VfxUiText("Flicker Amplitude").c_str(), "light.flickerAmplitude",
                               output.light.flickerAmplitude, 0.01f, VFX::kVfxLightFlickerAmplitudeMin,
                               VFX::kVfxLightFlickerAmplitudeMax);
            outputEditor.Unsigned(VfxUiText("Priority").c_str(), "light.priority", output.light.priority,
                                  VFX::kVfxLightPriorityMin, VFX::kVfxLightPriorityMax);
            outputEditor.Bool(VfxUiText("Casts Shadows").c_str(), "light.castsShadows", output.light.castsShadows);
            break;
        case VFX::VfxSpecialOutputType::WeaponPathDistortion:
        default:
            outputEditor.Float(VfxUiText("Distortion").c_str(), "weaponPathDistortion.distortion",
                               output.weaponPathDistortion.distortion, 0.01f, VFX::kVfxWeaponDistortionMin,
                               VFX::kVfxWeaponDistortionMax);
            outputEditor.Float(VfxUiText("Blur").c_str(), "weaponPathDistortion.blur", output.weaponPathDistortion.blur,
                               0.01f, VFX::kVfxWeaponBlurMin, VFX::kVfxWeaponBlurMax);
            outputEditor.Float(VfxUiText("Opacity").c_str(), "weaponPathDistortion.opacity",
                               output.weaponPathDistortion.opacity, 0.01f, VFX::kVfxWeaponOpacityMin,
                               VFX::kVfxWeaponOpacityMax);
            outputEditor.Float(VfxUiText("Emissive").c_str(), "weaponPathDistortion.emissive",
                               output.weaponPathDistortion.emissive, 0.01f, VFX::kVfxWeaponEmissiveMin,
                               VFX::kVfxWeaponEmissiveMax);
            outputEditor.Float(VfxUiText("Flow").c_str(), "weaponPathDistortion.flow", output.weaponPathDistortion.flow,
                               0.01f, VFX::kVfxWeaponFlowMin, VFX::kVfxWeaponFlowMax);
            outputEditor.Float(VfxUiText("Width").c_str(), "weaponPathDistortion.width",
                               output.weaponPathDistortion.width, 0.001f, VFX::kVfxWeaponWidthMin,
                               VFX::kVfxWeaponWidthMax);
            outputEditor.Float(VfxUiText("Lifetime").c_str(), "weaponPathDistortion.lifetimeSeconds",
                               output.weaponPathDistortion.lifetimeSeconds, 0.01f, VFX::kVfxWeaponLifetimeMin,
                               VFX::kVfxWeaponLifetimeMax);
            outputEditor.Color("Body Color", "weaponPathDistortion.bodyColor", output.weaponPathDistortion.bodyColor);
            outputEditor.Color("Edge Color", "weaponPathDistortion.edgeColor", output.weaponPathDistortion.edgeColor);
            break;
        }
        if (ImGui::SmallButton(VfxUiText("Remove Output").c_str())) {
            removeOutput = true;
            removeOutputId = output.id;
        }
        ImGui::PopID();
    }
    const auto addOutput = [this, &emitter](VFX::VfxSpecialOutputType type) {
        std::string outputId{};
        std::string message{};
        if (!AddSpecialOutput(emitter.id, type, {}, outputId, message)) {
            statusMessage_ = message;
        }
    };
    if (ImGui::Button(VfxUiText("Add WeaponPathDistortion Output").c_str())) {
        addOutput(VFX::VfxSpecialOutputType::WeaponPathDistortion);
    }
    ImGui::SameLine();
    if (ImGui::Button(VfxUiText("Add ScreenImpulse Output").c_str())) {
        addOutput(VFX::VfxSpecialOutputType::ScreenImpulse);
    }
    ImGui::SameLine();
    if (ImGui::Button(VfxUiText("Add SkinnedAfterimage Output").c_str())) {
        addOutput(VFX::VfxSpecialOutputType::SkinnedAfterimage);
    }
    if (ImGui::Button(VfxUiText("Add Light Output").c_str())) {
        addOutput(VFX::VfxSpecialOutputType::Light);
    }
    if (removeOutput) {
        std::string message{};
        (void)RemoveSpecialOutput(removeOutputId, message);
        statusMessage_ = message;
    }
#else
    (void)database;
    (void)emitter;
#endif
}
} // namespace HIKARI::EDITOR
