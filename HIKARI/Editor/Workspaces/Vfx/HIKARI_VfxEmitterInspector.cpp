#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxInspectorControls.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxParticleMaterialField.h"
#include "Render3D/Vfx/HIKARI_ParticleRenderInput.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
    namespace {
#if defined(HIKARI_WITH_EDITOR)
        struct EmitterInspectorPageDescriptor {
            VfxEmitterInspectorPage page;
            EditorGlyph glyph;
            const char* id;
            const char* title;
            const char* tooltip;
        };

        constexpr std::array<EmitterInspectorPageDescriptor, 8u>
            kEmitterInspectorPages{
                EmitterInspectorPageDescriptor{
                    VfxEmitterInspectorPage::Common,
                    EditorGlyph::Inspector,
                    "Common",
                    "Common",
                    "Common emitter settings and transform",
                },
                EmitterInspectorPageDescriptor{
                    VfxEmitterInspectorPage::Spawn,
                    EditorGlyph::Vfx,
                    "Spawn",
                    "Spawn",
                    "Spawn and particle capacity",
                },
                EmitterInspectorPageDescriptor{
                    VfxEmitterInspectorPage::Position,
                    EditorGlyph::Translate,
                    "Position",
                    "Position",
                    "Position, velocity and forces",
                },
                EmitterInspectorPageDescriptor{
                    VfxEmitterInspectorPage::Rotation,
                    EditorGlyph::Rotate,
                    "Rotation",
                    "Rotation",
                    "Rotation over particle lifetime",
                },
                EmitterInspectorPageDescriptor{
                    VfxEmitterInspectorPage::Scale,
                    EditorGlyph::Scale,
                    "Scale",
                    "Scale",
                    "Scale over particle lifetime",
                },
                EmitterInspectorPageDescriptor{
                    VfxEmitterInspectorPage::Shape,
                    EditorGlyph::Object,
                    "Shape",
                    "Shape",
                    "Emission shape",
                },
                EmitterInspectorPageDescriptor{
                    VfxEmitterInspectorPage::Render,
                    EditorGlyph::Material,
                    "Render",
                    "Render",
                    "Color, material and render shape",
                },
                EmitterInspectorPageDescriptor{
                    VfxEmitterInspectorPage::Bindings,
                    EditorGlyph::Dependency,
                    "Bindings",
                    "Bindings",
                    "Exposed input bindings",
                },
            };

        const EmitterInspectorPageDescriptor& GetPageDescriptor(
            VfxEmitterInspectorPage page) noexcept {

            const auto found = std::find_if(
                kEmitterInspectorPages.begin(),
                kEmitterInspectorPages.end(),
                [page](const EmitterInspectorPageDescriptor& descriptor) {
                    return descriptor.page == page;
                });
            return found != kEmitterInspectorPages.end()
                ? *found
                : kEmitterInspectorPages.front();
        }

        VfxEmitterInspectorPage DrawPageNavigation(
            VfxEmitterInspectorPage currentPage) {

            VfxEmitterInspectorPage requestedPage = currentPage;
            const ImVec2 buttonSize{ 30.0f, 30.0f };
            const float navigationHeight = buttonSize.y +
                ImGui::GetStyle().ScrollbarSize + 8.0f;
            ImGui::BeginChild(
                "##EmitterInspectorNavigation",
                ImVec2(0.0f, navigationHeight),
                false,
                ImGuiWindowFlags_HorizontalScrollbar);
            for (std::size_t index = 0u;
                 index < kEmitterInspectorPages.size();
                 ++index) {
                const EmitterInspectorPageDescriptor& descriptor =
                    kEmitterInspectorPages[index];
                if (index != 0u) {
                    ImGui::SameLine(0.0f, 4.0f);
                }
                if (IconToggleButton(
                        descriptor.glyph,
                        descriptor.id,
                        descriptor.page == currentPage,
                        buttonSize,
                        descriptor.tooltip)) {
                    requestedPage = descriptor.page;
                }
            }
            ImGui::EndChild();
            return requestedPage;
        }

#endif
    } // namespace

    void VfxWorkspaceController::DrawEmitterInspector(
        AssetDatabase& database,
        const VFX::VfxEmitterData& source) {
#if defined(HIKARI_WITH_EDITOR)
        const VFX::VfxEmitterData emitter = source;
        if (emitterInspectorEmitterId_ != emitter.id) {
            emitterInspectorEmitterId_ = emitter.id;
            emitterInspectorPage_ = selection_.kind ==
                    VfxWorkspaceSelectionKind::Bindings
                ? VfxEmitterInspectorPage::Bindings
                : VfxEmitterInspectorPage::Common;
        } else if (selection_.kind ==
                   VfxWorkspaceSelectionKind::Bindings) {
            emitterInspectorPage_ = VfxEmitterInspectorPage::Bindings;
        } else if (selection_.kind ==
                   VfxWorkspaceSelectionKind::SpecialOutput) {
            // Output profiles are authored on Common. Route hierarchy clicks
            // back here even when the user was previously on Spawn/Render.
            emitterInspectorPage_ = VfxEmitterInspectorPage::Common;
        }

        const VfxEmitterInspectorPage requestedPage =
            DrawPageNavigation(emitterInspectorPage_);
        if (requestedPage != emitterInspectorPage_) {
            emitterInspectorPage_ = requestedPage;
            Select({
                requestedPage == VfxEmitterInspectorPage::Bindings
                    ? VfxWorkspaceSelectionKind::Bindings
                    : VfxWorkspaceSelectionKind::Emitter,
                emitter.id,
                emitter.id,
            });
        }

        const EmitterInspectorPageDescriptor& pageDescriptor =
            GetPageDescriptor(emitterInspectorPage_);
        ImGui::SeparatorText(pageDescriptor.title);

        switch (emitterInspectorPage_) {
        case VfxEmitterInspectorPage::Common: {
            ImGui::TextDisabled("EMITTER");
            ImGui::TextWrapped("Stable ID: %s", emitter.id.c_str());
            VfxInspectorPropertyEditor emitterEditor(
                *this, database, emitter.id, statusMessage_);
            emitterEditor.Text("Name", "name", emitter.name);
            emitterEditor.Unsigned("Sibling Order", "order", emitter.order);
            emitterEditor.Float(
                "Start Delay (seconds)",
                "startDelay",
                emitter.startDelay,
                0.01f,
                0.0f,
                document_.Asset().durationSeconds > 0.0f
                    ? document_.Asset().durationSeconds
                    : 3600.0f);
            emitterEditor.Float(
                "Active Duration (0 = until asset end)",
                "activeDuration",
                emitter.activeDuration,
                0.01f,
                0.0f,
                document_.Asset().durationSeconds > 0.0f
                    ? document_.Asset().durationSeconds
                    : 3600.0f);

            ImGui::SeparatorText("Basic");
            VfxInspectorPropertyEditor basicEditor(
                *this, database, emitter.basic.id, statusMessage_);
            basicEditor.Bool("Enabled", "enabled", emitter.basic.enabled);
            basicEditor.FloatDistribution(
                "Lifetime",
                "lifetime",
                emitter.basic.lifetime,
                0.01f,
                0.001f,
                3600.0f);

            ImGui::SeparatorText("Emitter Transform");
            VfxInspectorPropertyEditor transformEditor(
                *this,
                database,
                emitter.emitterTransform.id,
                statusMessage_);
            transformEditor.Vec3(
                "Position",
                "transform.position",
                emitter.emitterTransform.transform.position);
            transformEditor.Vec3(
                "Rotation (degrees)",
                "transform.rotationEulerDegrees",
                emitter.emitterTransform.transform.rotationEulerDegrees,
                0.1f);
            transformEditor.Vec3(
                "Scale",
                "transform.scale",
                emitter.emitterTransform.transform.scale);

            ImGui::SeparatorText("Attachment / Follow");
            VfxInspectorPropertyEditor attachmentEditor(
                *this,
                database,
                emitter.attachment.id,
                statusMessage_);
            constexpr std::array<std::string_view, 3u> attachmentModes{
                "World", "Owner", "OwnerSocket"
            };
            attachmentEditor.Choice(
                "Attachment Mode",
                "mode",
                VFX::ToString(emitter.attachment.mode),
                attachmentModes.data(),
                attachmentModes.size());
            constexpr std::array<std::string_view, 4u> followModes{
                "None", "Position", "Rotation", "Transform"
            };
            attachmentEditor.Choice(
                "Follow",
                "follow",
                VFX::ToString(emitter.attachment.follow),
                followModes.data(),
                followModes.size());
            if (emitter.attachment.mode == VFX::VfxAttachmentMode::OwnerSocket) {
                attachmentEditor.Text(
                    "Socket Name",
                    "socketName",
                    emitter.attachment.socketName);
            }
            attachmentEditor.Vec3(
                "Local Offset",
                "offset.position",
                emitter.attachment.offset.position);
            attachmentEditor.Vec3(
                "Local Rotation (degrees)",
                "offset.rotationEulerDegrees",
                emitter.attachment.offset.rotationEulerDegrees,
                0.1f);
            attachmentEditor.Vec3(
                "Local Scale",
                "offset.scale",
                emitter.attachment.offset.scale);

            ImGui::SeparatorText("Typed Special Outputs");
            constexpr std::array<std::string_view, 3u> specialOutputTypes{
                "WeaponPathDistortion", "ScreenImpulse", "SkinnedAfterimage"
            };
            if (emitter.specialOutputs.empty()) {
                ImGui::TextDisabled(
                    "No special output profiles. Add one to expose the typed renderer profile.");
            } else if (selection_.kind ==
                       VfxWorkspaceSelectionKind::SpecialOutput) {
                ImGui::TextDisabled(
                    "Editing selected special output profile: %s",
                    selection_.targetId.c_str());
            }
            bool removeOutput = false;
            std::string removeOutputId{};
            for (const VFX::VfxSpecialOutputData& output :
                 emitter.specialOutputs) {
                if (selection_.kind ==
                        VfxWorkspaceSelectionKind::SpecialOutput &&
                    selection_.targetId != output.id) {
                    continue;
                }
                ImGui::PushID(output.id.c_str());
                ImGui::Separator();
                if (ImGui::Selectable(
                        output.id.c_str(),
                        selection_.kind ==
                            VfxWorkspaceSelectionKind::SpecialOutput &&
                        selection_.targetId == output.id)) {
                    Select({ VfxWorkspaceSelectionKind::SpecialOutput,
                        output.id, emitter.id });
                }
                VfxInspectorPropertyEditor outputEditor(
                    *this, database, output.id, statusMessage_);
                outputEditor.Bool("Enabled", "enabled", output.enabled);
                outputEditor.Choice(
                    "Profile",
                    "type",
                    VFX::ToString(output.type),
                    specialOutputTypes.data(),
                    specialOutputTypes.size());
                switch (output.type) {
                case VFX::VfxSpecialOutputType::ScreenImpulse:
                    outputEditor.Float(
                        "Duration", "screenImpulse.durationSeconds",
                        output.screenImpulse.durationSeconds, 0.01f,
                        VFX::kVfxScreenDurationMin,
                        VFX::kVfxScreenDurationMax);
                    outputEditor.Float(
                        "Max Radius", "screenImpulse.maxRadius",
                        output.screenImpulse.maxRadius, 0.01f,
                        VFX::kVfxScreenRadiusMin,
                        VFX::kVfxScreenRadiusMax);
                    outputEditor.Float(
                        "Ring Thickness", "screenImpulse.ringThickness",
                        output.screenImpulse.ringThickness, 0.001f,
                        VFX::kVfxScreenRingThicknessMin,
                        VFX::kVfxScreenRingThicknessMax);
                    outputEditor.Float(
                        "Distortion", "screenImpulse.distortion",
                        output.screenImpulse.distortion, 0.01f,
                        VFX::kVfxScreenDistortionMin,
                        VFX::kVfxScreenDistortionMax);
                    outputEditor.Float(
                        "Chromatic Aberration", "screenImpulse.chromaticAberration",
                        output.screenImpulse.chromaticAberration, 0.001f,
                        VFX::kVfxScreenChromaticMin,
                        VFX::kVfxScreenChromaticMax);
                    outputEditor.Float(
                        "Flash", "screenImpulse.flash",
                        output.screenImpulse.flash, 0.01f,
                        VFX::kVfxScreenFlashMin,
                        VFX::kVfxScreenFlashMax);
                    outputEditor.Float(
                        "Intensity", "screenImpulse.intensity",
                        output.screenImpulse.intensity, 0.01f,
                        VFX::kVfxScreenIntensityMin,
                        VFX::kVfxScreenIntensityMax);
                    outputEditor.Color(
                        "Color", "screenImpulse.color", output.screenImpulse.color);
                    outputEditor.Float(
                        "Wave Frequency", "screenImpulse.waveFrequency",
                        output.screenImpulse.waveFrequency, 0.01f,
                        VFX::kVfxScreenWaveFrequencyMin,
                        VFX::kVfxScreenWaveFrequencyMax);
                    outputEditor.Float(
                        "Wave Amplitude", "screenImpulse.waveAmplitude",
                        output.screenImpulse.waveAmplitude, 0.01f,
                        VFX::kVfxScreenWaveAmplitudeMin,
                        VFX::kVfxScreenWaveAmplitudeMax);
                    outputEditor.Float(
                        "Wave Damping", "screenImpulse.waveDamping",
                        output.screenImpulse.waveDamping, 0.01f,
                        VFX::kVfxScreenWaveDampingMin,
                        VFX::kVfxScreenWaveDampingMax);
                    outputEditor.Unsigned(
                        "Wave Count", "screenImpulse.waveCount",
                        output.screenImpulse.waveCount,
                        VFX::kVfxScreenImpulseWaveCountMin,
                        VFX::kVfxScreenImpulseWaveCountMax);
                    break;
                case VFX::VfxSpecialOutputType::SkinnedAfterimage:
                    outputEditor.Unsigned(
                        "Sample Count", "skinnedAfterimage.sampleCount",
                        output.skinnedAfterimage.sampleCount,
                        VFX::kVfxAfterimageSampleCountMin,
                        VFX::kVfxAfterimageSampleCountMax);
                    outputEditor.Float(
                        "Lifetime", "skinnedAfterimage.lifetimeSeconds",
                        output.skinnedAfterimage.lifetimeSeconds, 0.01f,
                        VFX::kVfxAfterimageLifetimeMin,
                        VFX::kVfxAfterimageLifetimeMax);
                    outputEditor.Float(
                        "Opacity", "skinnedAfterimage.opacity",
                        output.skinnedAfterimage.opacity, 0.01f,
                        VFX::kVfxAfterimageOpacityMin,
                        VFX::kVfxAfterimageOpacityMax);
                    outputEditor.Float(
                        "Fresnel", "skinnedAfterimage.fresnel",
                        output.skinnedAfterimage.fresnel, 0.01f,
                        VFX::kVfxAfterimageFresnelMin,
                        VFX::kVfxAfterimageFresnelMax);
                    outputEditor.Float(
                        "Dissolve", "skinnedAfterimage.dissolve",
                        output.skinnedAfterimage.dissolve, 0.01f,
                        VFX::kVfxAfterimageDissolveMin,
                        VFX::kVfxAfterimageDissolveMax);
                    outputEditor.Float(
                        "Shell Offset", "skinnedAfterimage.shellOffset",
                        output.skinnedAfterimage.shellOffset, 0.001f,
                        VFX::kVfxAfterimageShellOffsetMin,
                        VFX::kVfxAfterimageShellOffsetMax);
                    outputEditor.Color(
                        "Color", "skinnedAfterimage.color",
                        output.skinnedAfterimage.color);
                    outputEditor.Float(
                        "Intensity", "skinnedAfterimage.intensity",
                        output.skinnedAfterimage.intensity, 0.01f,
                        VFX::kVfxAfterimageIntensityMin,
                        VFX::kVfxAfterimageIntensityMax);
                    break;
                case VFX::VfxSpecialOutputType::WeaponPathDistortion:
                default:
                    outputEditor.Float(
                        "Distortion", "weaponPathDistortion.distortion",
                        output.weaponPathDistortion.distortion, 0.01f,
                        VFX::kVfxWeaponDistortionMin,
                        VFX::kVfxWeaponDistortionMax);
                    outputEditor.Float(
                        "Blur", "weaponPathDistortion.blur",
                        output.weaponPathDistortion.blur, 0.01f,
                        VFX::kVfxWeaponBlurMin,
                        VFX::kVfxWeaponBlurMax);
                    outputEditor.Float(
                        "Opacity", "weaponPathDistortion.opacity",
                        output.weaponPathDistortion.opacity, 0.01f,
                        VFX::kVfxWeaponOpacityMin,
                        VFX::kVfxWeaponOpacityMax);
                    outputEditor.Float(
                        "Emissive", "weaponPathDistortion.emissive",
                        output.weaponPathDistortion.emissive, 0.01f,
                        VFX::kVfxWeaponEmissiveMin,
                        VFX::kVfxWeaponEmissiveMax);
                    outputEditor.Float(
                        "Flow", "weaponPathDistortion.flow",
                        output.weaponPathDistortion.flow, 0.01f,
                        VFX::kVfxWeaponFlowMin,
                        VFX::kVfxWeaponFlowMax);
                    outputEditor.Float(
                        "Width", "weaponPathDistortion.width",
                        output.weaponPathDistortion.width, 0.001f,
                        VFX::kVfxWeaponWidthMin,
                        VFX::kVfxWeaponWidthMax);
                    outputEditor.Float(
                        "Lifetime", "weaponPathDistortion.lifetimeSeconds",
                        output.weaponPathDistortion.lifetimeSeconds, 0.01f,
                        VFX::kVfxWeaponLifetimeMin,
                        VFX::kVfxWeaponLifetimeMax);
                    outputEditor.Color(
                        "Body Color", "weaponPathDistortion.bodyColor",
                        output.weaponPathDistortion.bodyColor);
                    outputEditor.Color(
                        "Edge Color", "weaponPathDistortion.edgeColor",
                        output.weaponPathDistortion.edgeColor);
                    break;
                }
                if (ImGui::SmallButton("Remove Output")) {
                    removeOutput = true;
                    removeOutputId = output.id;
                }
                ImGui::PopID();
            }
            const auto addOutput = [this, &emitter](
                VFX::VfxSpecialOutputType type) {
                std::string outputId{};
                std::string message{};
                if (!AddSpecialOutput(
                        emitter.id, type, {}, outputId, message)) {
                    statusMessage_ = message;
                }
            };
            if (ImGui::Button("Add WeaponPathDistortion Output")) {
                addOutput(VFX::VfxSpecialOutputType::WeaponPathDistortion);
            }
            ImGui::SameLine();
            if (ImGui::Button("Add ScreenImpulse Output")) {
                addOutput(VFX::VfxSpecialOutputType::ScreenImpulse);
            }
            ImGui::SameLine();
            if (ImGui::Button("Add SkinnedAfterimage Output")) {
                addOutput(VFX::VfxSpecialOutputType::SkinnedAfterimage);
            }
            if (removeOutput) {
                std::string message{};
                (void)RemoveSpecialOutput(removeOutputId, message);
                statusMessage_ = message;
            }
            break;
        }
        case VfxEmitterInspectorPage::Spawn: {
            VfxInspectorPropertyEditor editor(
                *this, database, emitter.spawn.id, statusMessage_);
            editor.Float(
                "Rate / second",
                "ratePerSecond",
                emitter.spawn.ratePerSecond,
                0.1f,
                0.0f,
                1000000.0f);
            editor.Unsigned(
                "Burst Count",
                "burstCount",
                emitter.spawn.burstCount,
                0u,
                emitter.spawn.maximumParticles);
            constexpr std::array<std::string_view, 2u> capacityModes{
                "Automatic", "Fixed"
            };
            editor.Choice(
                "Capacity Mode",
                "capacityMode",
                VFX::ToString(emitter.spawn.capacityMode),
                capacityModes.data(),
                capacityModes.size());
            editor.Unsigned(
                emitter.spawn.capacityMode ==
                    VFX::VfxParticleCapacityMode::Automatic
                    ? "Capacity Ceiling"
                    : "Maximum Particles",
                "maximumParticles",
                emitter.spawn.maximumParticles,
                1u,
                RENDER3D::PARTICLES::kGpuParticlePoolCapacity);
            if (emitter.spawn.capacityMode ==
                VFX::VfxParticleCapacityMode::Automatic) {
                ImGui::TextDisabled(
                    "GPU capacity is derived from spawn rate and maximum lifetime.");
            }
            break;
        }
        case VfxEmitterInspectorPage::Position: {
            ImGui::SeparatorText("Position");
            VfxInspectorPropertyEditor positionEditor(
                *this, database, emitter.position.id, statusMessage_);
            positionEditor.FloatDistribution(
                "Radial Speed",
                "radialSpeed",
                emitter.position.radialSpeed,
                0.01f);
            positionEditor.AnimatedVec3(
                "Position over Life",
                "value",
                emitter.position.value);

            ImGui::SeparatorText("Forces");
            VfxInspectorPropertyEditor forcesEditor(
                *this, database, emitter.forces.id, statusMessage_);
            forcesEditor.Vec3(
                "Gravity", "gravity", emitter.forces.gravity);
            forcesEditor.Float(
                "Drag",
                "drag",
                emitter.forces.drag,
                0.01f,
                0.0f,
                1000.0f);
            break;
        }
        case VfxEmitterInspectorPage::Rotation: {
            VfxInspectorPropertyEditor editor(
                *this, database, emitter.rotation.id, statusMessage_);
            editor.AnimatedVec3(
                "Rotation over Life",
                "eulerDegrees",
                emitter.rotation.eulerDegrees,
                0.1f);
            break;
        }
        case VfxEmitterInspectorPage::Scale: {
            VfxInspectorPropertyEditor editor(
                *this, database, emitter.scale.id, statusMessage_);
            editor.AnimatedVec3(
                "Scale over Life", "value", emitter.scale.value);
            break;
        }
        case VfxEmitterInspectorPage::Shape: {
            VfxInspectorPropertyEditor editor(
                *this, database, emitter.shape.id, statusMessage_);
            constexpr std::array<std::string_view, 7u> shapes{
                "Point", "Sphere", "Cone", "Box",
                "Cylinder", "Circle", "Edge"
            };
            editor.Choice(
                "Type",
                "type",
                VFX::ToString(emitter.shape.type),
                shapes.data(),
                shapes.size());
            if (emitter.shape.type != VFX::VfxShapeType::Point) {
                editor.FloatDistribution(
                    "Radius",
                    "radius",
                    emitter.shape.radius,
                    0.01f,
                    0.0f,
                    100000.0f);
            }
            if (emitter.shape.type == VFX::VfxShapeType::Cone) {
                editor.Float(
                    "Cone Angle (degrees)",
                    "coneAngleDegrees",
                    emitter.shape.coneAngleDegrees,
                    0.1f,
                    0.0f,
                    180.0f);
            }
            if (emitter.shape.type == VFX::VfxShapeType::Box) {
                editor.Vec3(
                    "Box Half Extents",
                    "boxHalfExtents",
                    emitter.shape.boxHalfExtents,
                    0.01f);
            }
            if (emitter.shape.type == VFX::VfxShapeType::Cylinder) {
                editor.Float(
                    "Cylinder Height",
                    "cylinderHeight",
                    emitter.shape.cylinderHeight,
                    0.01f,
                    0.0f,
                    100000.0f);
            }
            if (emitter.shape.type == VFX::VfxShapeType::Circle) {
                editor.Float(
                    "Circle Inner Radius",
                    "circleInnerRadius",
                    emitter.shape.circleInnerRadius,
                    0.01f,
                    0.0f,
                    100000.0f);
                editor.Bool(
                    "Emit On Edge Only",
                    "circleEmitOnEdge",
                    emitter.shape.circleEmitOnEdge);
            }
            if (emitter.shape.type == VFX::VfxShapeType::Edge) {
                editor.Float(
                    "Edge Length",
                    "edgeLength",
                    emitter.shape.edgeLength,
                    0.01f,
                    0.0f,
                    100000.0f);
            }
            break;
        }
        case VfxEmitterInspectorPage::Render: {
            ImGui::SeparatorText("Color");
            VfxInspectorPropertyEditor colorEditor(
                *this, database, emitter.color.id, statusMessage_);
            colorEditor.AnimatedColor(
                "Color over Life", "value", emitter.color.value);

            ImGui::SeparatorText("Render Common");
            VfxInspectorPropertyEditor renderCommonEditor(
                *this,
                database,
                emitter.renderCommon.id,
                statusMessage_);
            constexpr std::array<std::string_view, 2u> materialModes{
                "StandardUnlit", "CustomMaterial"
            };
            renderCommonEditor.Choice(
                "Appearance",
                "materialMode",
                VFX::ToString(emitter.renderCommon.materialMode),
                materialModes.data(),
                materialModes.size());
            if (emitter.renderCommon.materialMode ==
                VFX::VfxMaterialMode::StandardUnlit) {
                AssetGuid selected =
                    emitter.renderCommon.colorTextureAssetGuid;
                if (DrawVfxParticleTextureField(
                        database,
                        particleTextureCatalog_,
                        emitter.renderCommon.colorTextureAssetGuid,
                        selected,
                        statusMessage_)) {
                    std::string assignmentMessage{};
                    (void)SetProperty(
                        emitter.renderCommon.id,
                        "colorTextureAssetGuid",
                        selected.value,
                        assignmentMessage,
                        0u,
                        &database);
                    statusMessage_ = assignmentMessage;
                }
            } else {
                AssetGuid selected =
                    emitter.renderCommon.materialAssetGuid;
                AssetGuid created{};
                AssetGuid editRequested{};
                if (DrawVfxParticleMaterialField(
                        database,
                        particleMaterialCatalog_,
                        emitter.renderCommon.materialAssetGuid,
                        selected,
                        created,
                        editRequested,
                        statusMessage_)) {
                    std::string assignmentMessage{};
                    const bool assigned = SetProperty(
                        emitter.renderCommon.id,
                        "materialAssetGuid",
                        selected.value,
                        assignmentMessage,
                        0u,
                        &database);
                    statusMessage_ = assignmentMessage;
                    if (assigned && created.IsValid()) {
                        std::string refreshMessage{};
                        if (!RefreshAuthoredAsset(
                                created, refreshMessage)) {
                            statusMessage_ +=
                                "; live Material refresh failed: " +
                                refreshMessage;
                        } else if (!refreshMessage.empty()) {
                            statusMessage_ += "; " + refreshMessage;
                        }
                    }
                }
                if (editRequested.IsValid()) {
                    if (!materialEditCallback_) {
                        statusMessage_ =
                            "Material editor is not available";
                    } else {
                        std::string editMessage{};
                        const bool opened = materialEditCallback_(
                            editRequested, editMessage);
                        statusMessage_ = editMessage.empty()
                            ? opened
                                ? "Opening Material editor"
                                : "Failed to open Material editor"
                            : std::move(editMessage);
                    }
                }
            }
            constexpr std::array<std::string_view, 2u> blendModes{
                "Alpha", "Additive"
            };
            renderCommonEditor.Choice(
                "Blend Mode",
                "blendMode",
                VFX::ToString(emitter.renderCommon.blendMode),
                blendModes.data(),
                blendModes.size());
            renderCommonEditor.Bool(
                "Depth Test",
                "depthTest",
                emitter.renderCommon.depthTest);
            ImGui::SeparatorText("Flipbook");
            renderCommonEditor.Unsigned(
                "Columns",
                "flipbookColumns",
                emitter.renderCommon.flipbookColumns,
                1u,
                64u);
            renderCommonEditor.Unsigned(
                "Rows",
                "flipbookRows",
                emitter.renderCommon.flipbookRows,
                1u,
                64u);
            renderCommonEditor.Unsigned(
                "Frame Count",
                "flipbookFrameCount",
                emitter.renderCommon.flipbookFrameCount,
                1u,
                emitter.renderCommon.flipbookColumns *
                    emitter.renderCommon.flipbookRows);
            renderCommonEditor.Float(
                "Frames Per Second",
                "flipbookFramesPerSecond",
                emitter.renderCommon.flipbookFramesPerSecond,
                0.1f,
                0.0f,
                240.0f);
            renderCommonEditor.Bool(
                "Loop",
                "flipbookLoop",
                emitter.renderCommon.flipbookLoop);
            renderCommonEditor.Bool(
                "Random Start",
                "flipbookRandomStart",
                emitter.renderCommon.flipbookRandomStart);

            ImGui::SeparatorText("Render Shape");
            VfxInspectorPropertyEditor renderShapeEditor(
                *this,
                database,
                emitter.renderShape.id,
                statusMessage_);
            constexpr std::array<std::string_view, 3u> renderShapes{
                "Sprite", "Ribbon", "Ring"
            };
            renderShapeEditor.Choice(
                "Render Shape",
                "type",
                VFX::ToString(emitter.renderShape.type),
                renderShapes.data(),
                renderShapes.size());
            constexpr std::array<std::string_view, 4u> orientationModes{
                "RotatedBillboard",
                "FixedYAxis",
                "DirectionalBillboard",
                "Fixed",
            };
            renderShapeEditor.Choice(
                "Sprite Orientation",
                "billboardMode",
                VFX::ToString(emitter.renderShape.billboardMode),
                orientationModes.data(),
                orientationModes.size());
            if (emitter.renderShape.type ==
                VFX::VfxRenderShapeType::Ribbon) {
                renderShapeEditor.Unsigned(
                    "Ribbon Segments",
                    "ribbonSegmentCount",
                    emitter.renderShape.ribbonSegmentCount,
                    2u,
                    256u);
                renderShapeEditor.Float(
                    "Ribbon Width",
                    "ribbonWidth",
                    emitter.renderShape.ribbonWidth,
                    0.01f,
                    0.0f,
                    1000.0f);
            }
            if (emitter.renderShape.type ==
                VFX::VfxRenderShapeType::Ring) {
                renderShapeEditor.Float(
                    "Ring Outer Radius",
                    "ringOuterRadius",
                    emitter.renderShape.ringOuterRadius,
                    0.01f,
                    0.0f,
                    100000.0f);
                renderShapeEditor.Float(
                    "Ring Inner Radius",
                    "ringInnerRadius",
                    emitter.renderShape.ringInnerRadius,
                    0.01f,
                    0.0f,
                    100000.0f);
                renderShapeEditor.Unsigned(
                    "Ring Segments",
                    "ringSegmentCount",
                    emitter.renderShape.ringSegmentCount,
                    3u,
                    256u);
                renderShapeEditor.Float(
                    "Ring Start Angle (degrees)",
                    "ringStartAngleDegrees",
                    emitter.renderShape.ringStartAngleDegrees,
                    0.1f,
                    -3600.0f,
                    3600.0f);
                renderShapeEditor.Float(
                    "Ring End Angle (degrees)",
                    "ringEndAngleDegrees",
                    emitter.renderShape.ringEndAngleDegrees,
                    0.1f,
                    -3600.0f,
                    3600.0f);
            }
            break;
        }
        case VfxEmitterInspectorPage::Bindings:
            DrawEmitterBindingsInspector(emitter);
            break;
        }
#else
        (void)database;
        (void)source;
#endif
    }

} // namespace HIKARI::EDITOR
