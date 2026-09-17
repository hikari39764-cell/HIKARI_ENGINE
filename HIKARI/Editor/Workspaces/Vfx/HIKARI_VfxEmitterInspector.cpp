#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

#include "Core/Text/HIKARI_Localization.h"

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
    namespace {
#if defined(HIKARI_WITH_EDITOR)
        VfxEmitterInspectorPage DrawPageNavigation(VfxEmitterInspectorPage current, bool particles) {
            struct Page { VfxEmitterInspectorPage id; const char* label; bool particlesOnly; };
            constexpr std::array pages{
                Page{VfxEmitterInspectorPage::Common, "ui.vfx.layer.shape", false},
                Page{VfxEmitterInspectorPage::Render, "ui.vfx.layer.appearance", false},
                Page{VfxEmitterInspectorPage::Spawn, "ui.vfx.layer.emission", true},
                Page{VfxEmitterInspectorPage::Position, "ui.vfx.layer.motion", true},
                Page{VfxEmitterInspectorPage::Advanced, "ui.vfx.layer.advanced", false}
            };
            auto requested = current;
            if (ImGui::BeginCombo("##page", TEXT::Localize(
                    current == VfxEmitterInspectorPage::Common ? pages[0].label :
                    current == VfxEmitterInspectorPage::Render ? pages[1].label :
                    current == VfxEmitterInspectorPage::Spawn ? pages[2].label :
                    current == VfxEmitterInspectorPage::Position ? pages[3].label : pages[4].label).c_str())) {
                for (const auto& page : pages) {
                    if (page.particlesOnly && !particles) continue;
                    if (ImGui::Selectable(TEXT::Localize(page.label).c_str(), current == page.id)) requested = page.id;
                }
                ImGui::EndCombo();
            }
            ImGui::Separator();
            return requested;
        }
#endif
    }

    void VfxWorkspaceController::DrawEmitterInspector(AssetDatabase& database, const VFX::VfxEmitterData& source) {
#if defined(HIKARI_WITH_EDITOR)
        const VFX::VfxEmitterData emitter = source;
        if (emitterInspectorEmitterId_ != emitter.id) {
            emitterInspectorEmitterId_ = emitter.id;
            emitterInspectorPage_ = VfxEmitterInspectorPage::Common;
        }
        if (selection_.kind == VfxWorkspaceSelectionKind::Bindings) emitterInspectorPage_ = VfxEmitterInspectorPage::Bindings;
        if (selection_.kind == VfxWorkspaceSelectionKind::SpecialOutput) emitterInspectorPage_ = VfxEmitterInspectorPage::Advanced;
        ImGui::TextWrapped("%s", emitter.name.c_str());
        ImGui::TextDisabled("%s  |  %s", VFX::ToString(emitter.renderShape.type).data(), VFX::ToString(emitter.renderCommon.materialMode).data());
        const auto requested = DrawPageNavigation(emitterInspectorPage_, emitter.kind == VFX::VfxLayerKind::Particles);
        if (requested != emitterInspectorPage_) {
            emitterInspectorPage_ = requested;
            Select({VfxWorkspaceSelectionKind::Emitter, emitter.id, emitter.id});
        }
        switch (emitterInspectorPage_) {
        case VfxEmitterInspectorPage::Common: {
            if (emitter.kind != VFX::VfxLayerKind::Group && emitter.history.mode != VFX::VfxHistoryMode::Trail) {
            ImGui::SeparatorText(TEXT::Localize("ui.vfx.layer.geometry").c_str());
            VfxInspectorPropertyEditor renderShapeEditor(
                *this,
                database,
                emitter.renderShape.id,
                statusMessage_);
            const auto renderShapes = VFX::GetVfxRenderShapeChoices();
            renderShapeEditor.Choice(
                VfxUiText("Geometry").c_str(),
                "type",
                VFX::ToString(emitter.renderShape.type),
                renderShapes.data(),
                renderShapes.size());
            if (emitter.renderShape.type == VFX::VfxRenderShapeType::Sprite ||
                emitter.renderShape.type == VFX::VfxRenderShapeType::Ring ||
                emitter.renderShape.type == VFX::VfxRenderShapeType::Ribbon) {
                constexpr std::array<std::string_view, 4u> orientationModes{
                    "RotatedBillboard",
                    "FixedYAxis",
                    "DirectionalBillboard",
                    "Fixed",
                };
                renderShapeEditor.Choice(
                    VfxUiText("Orientation").c_str(),
                    "billboardMode",
                    VFX::ToString(emitter.renderShape.billboardMode),
                    orientationModes.data(),
                    orientationModes.size());
            }
            if (emitter.renderShape.type ==
                VFX::VfxRenderShapeType::Ribbon) {
                renderShapeEditor.Unsigned(
                    VfxUiText("Ribbon Segments").c_str(),
                    "ribbonSegmentCount",
                    emitter.renderShape.ribbonSegmentCount,
                    2u,
                    64u);
                (void)DrawLayerParameter(emitter, emitter.renderShape.id, "ribbonWidth");
            }
            if (emitter.renderShape.type ==
                VFX::VfxRenderShapeType::Ring) {
                (void)DrawLayerParameter(emitter, emitter.renderShape.id, "ringOuterRadius");
                (void)DrawLayerParameter(emitter, emitter.renderShape.id, "ringInnerRadius");
                renderShapeEditor.Unsigned(
                    VfxUiText("Ring Segments").c_str(),
                    "ringSegmentCount",
                    emitter.renderShape.ringSegmentCount,
                    3u,
                    64u);
                (void)DrawLayerParameter(emitter, emitter.renderShape.id, "ringStartAngleDegrees");
                (void)DrawLayerParameter(emitter, emitter.renderShape.id, "ringEndAngleDegrees");
            }
            if (emitter.renderShape.type == VFX::VfxRenderShapeType::MeshCylinder ||
                emitter.renderShape.type == VFX::VfxRenderShapeType::MeshCone) {
                renderShapeEditor.Unsigned(VfxUiText("Radial segments").c_str(), "ringSegmentCount",
                    emitter.renderShape.ringSegmentCount, 3u, 64u);
                ImGui::TextWrapped(VfxUiText("Open shell along local Y. Scale controls diameter and length; Rotation sets its direction. UV wraps around the shell.").c_str());
            }
            if (emitter.renderShape.type == VFX::VfxRenderShapeType::CrossPlane)
                ImGui::TextWrapped(VfxUiText("Two intersecting planes in local space. Useful for flames and beams viewed from different sides.").c_str());
            }
            ImGui::SeparatorText(TEXT::Localize("ui.vfx.layer.timing").c_str());

            VfxInspectorPropertyEditor emitterEditor(
                *this, database, emitter.id, statusMessage_);
            emitterEditor.Text(VfxUiText("Name").c_str(), "name", emitter.name);
            if (emitter.kind == VFX::VfxLayerKind::Shape) {
                constexpr std::string_view modes[]{"None", "Afterimage", "Trail"};
                emitterEditor.Choice(VfxUiText("Motion history").c_str(), "history.mode",
                    modes[static_cast<size_t>(emitter.history.mode)], modes, 3);
                if (emitter.history.mode != VFX::VfxHistoryMode::None) {
                    emitterEditor.Float(VfxUiText("History duration").c_str(), "history.duration", emitter.history.duration, 0.01f, 0.01f, 2);
                    emitterEditor.Unsigned(VfxUiText("History samples").c_str(), "history.samples", emitter.history.samples, 2, 16);
                    emitterEditor.Float(VfxUiText("Teleport break distance").c_str(), "history.breakDistance", emitter.history.breakDistance, 0.1f, 0.01f, 10000);
                    if (emitter.history.mode == VFX::VfxHistoryMode::Trail) {
                        VfxInspectorPropertyEditor geometry(*this, database, emitter.renderShape.id, statusMessage_);
                        (void)DrawLayerParameter(emitter, emitter.renderShape.id, "ribbonWidth");
                        ImGui::TextWrapped("%s", VfxUiText("Trail connects the layer's local X edges through world-space motion. Animate position or attach it to a moving object.").c_str());
                    }
                }
            }
            emitterEditor.Float(
                VfxUiText("Start Delay (seconds)").c_str(),
                "startDelay",
                emitter.startDelay,
                0.01f,
                0.0f,
                document_.Asset().durationSeconds > 0.0f
                    ? document_.Asset().durationSeconds
                    : 3600.0f);
            emitterEditor.Float(
                VfxUiText("Active Duration (0 = until asset end)").c_str(),
                "activeDuration",
                emitter.activeDuration,
                0.01f,
                0.0f,
                document_.Asset().durationSeconds > 0.0f
                    ? document_.Asset().durationSeconds
                    : 3600.0f);

            ImGui::SeparatorText(VfxUiText("Basic").c_str());
            VfxInspectorPropertyEditor basicEditor(
                *this, database, emitter.basic.id, statusMessage_);
            basicEditor.Bool(VfxUiText("Enabled").c_str(), "enabled", emitter.basic.enabled);
            if (emitter.kind == VFX::VfxLayerKind::Particles) {
            basicEditor.Bool(VfxUiText("Follow Emitter (Local Space)").c_str(),"localSimulation",emitter.basic.localSimulation);
            ImGui::TextWrapped(VfxUiText("Off: particles retain their spawn transform. Forces use the simulation space.").c_str());
            basicEditor.FloatDistribution(
                "Lifetime",
                "lifetime",
                emitter.basic.lifetime,
                0.01f,
                0.001f,
                3600.0f);

            }
            ImGui::SeparatorText(TEXT::Localize("ui.vfx.layer.offset").c_str());
            VfxInspectorPropertyEditor transformEditor(
                *this,
                database,
                emitter.emitterTransform.id,
                statusMessage_);
            transformEditor.Vec3(
                VfxUiText("Position").c_str(),
                "transform.position",
                emitter.emitterTransform.transform.position);
            transformEditor.Vec3(
                VfxUiText("Rotation (degrees)").c_str(),
                "transform.rotationEulerDegrees",
                emitter.emitterTransform.transform.rotationEulerDegrees,
                0.1f);
            transformEditor.Vec3(
                VfxUiText("Scale").c_str(),
                "transform.scale",
                emitter.emitterTransform.transform.scale);

            if (emitter.kind != VFX::VfxLayerKind::Particles) DrawLayerAnimation(database, emitter);
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
                    VfxUiText("GPU capacity is derived from spawn rate and maximum lifetime.").c_str());
            }
            ImGui::SeparatorText(VfxUiText("Emission volume").c_str());
            VfxInspectorPropertyEditor shapeEditor(
                *this, database, emitter.shape.id, statusMessage_);
            constexpr std::array<std::string_view, 7u> shapes{
                "Point", "Sphere", "Cone", "Box",
                "Cylinder", "Circle", "Edge"
            };
            shapeEditor.Choice(
                VfxUiText("Type").c_str(),
                "type",
                VFX::ToString(emitter.shape.type),
                shapes.data(),
                shapes.size());
            if (emitter.shape.type != VFX::VfxShapeType::Point) {
                shapeEditor.FloatDistribution(
                    "Radius",
                    "radius",
                    emitter.shape.radius,
                    0.01f,
                    0.0f,
                    100000.0f);
            }
            if (emitter.shape.type == VFX::VfxShapeType::Cone) {
                shapeEditor.Float(
                    VfxUiText("Cone Angle (degrees)").c_str(),
                    "coneAngleDegrees",
                    emitter.shape.coneAngleDegrees,
                    0.1f,
                    0.0f,
                    180.0f);
            }
            if (emitter.shape.type == VFX::VfxShapeType::Box) {
                shapeEditor.Vec3(
                    VfxUiText("Box Half Extents").c_str(),
                    "boxHalfExtents",
                    emitter.shape.boxHalfExtents,
                    0.01f);
            }
            if (emitter.shape.type == VFX::VfxShapeType::Cylinder) {
                shapeEditor.Float(
                    VfxUiText("Cylinder Height").c_str(),
                    "cylinderHeight",
                    emitter.shape.cylinderHeight,
                    0.01f,
                    0.0f,
                    100000.0f);
            }
            if (emitter.shape.type == VFX::VfxShapeType::Circle) {
                shapeEditor.Float(
                    VfxUiText("Circle Inner Radius").c_str(),
                    "circleInnerRadius",
                    emitter.shape.circleInnerRadius,
                    0.01f,
                    0.0f,
                    100000.0f);
                shapeEditor.Bool(
                    VfxUiText("Emit On Edge Only").c_str(),
                    "circleEmitOnEdge",
                    emitter.shape.circleEmitOnEdge);
            }
            if (emitter.shape.type == VFX::VfxShapeType::Edge) {
                shapeEditor.Float(
                    VfxUiText("Edge Length").c_str(),
                    "edgeLength",
                    emitter.shape.edgeLength,
                    0.01f,
                    0.0f,
                    100000.0f);
            }
            break;
        }
        case VfxEmitterInspectorPage::Position: {
            ImGui::SeparatorText(VfxUiText("Position").c_str());
            VfxInspectorPropertyEditor positionEditor(
                *this, database, emitter.position.id, statusMessage_);
            positionEditor.FloatDistribution(
                "Radial Speed",
                "radialSpeed",
                emitter.position.radialSpeed,
                0.01f);
            positionEditor.AnimatedVec3(
                VfxUiText("Position over Life").c_str(),
                "value",
                emitter.position.value);

            ImGui::SeparatorText(VfxUiText("Forces").c_str());
            VfxInspectorPropertyEditor forcesEditor(
                *this, database, emitter.forces.id, statusMessage_);
            forcesEditor.Vec3(
                VfxUiText("Gravity").c_str(), "gravity", emitter.forces.gravity);
            forcesEditor.Float(
                VfxUiText("Drag").c_str(),
                "drag",
                emitter.forces.drag,
                0.01f,
                0.0f,
                1000.0f);
            forcesEditor.Bool(VfxUiText("Plane Collision").c_str(),"collisionPlaneEnabled",emitter.forces.collisionPlaneEnabled);
            if (emitter.forces.collisionPlaneEnabled) {
                forcesEditor.Float(VfxUiText("Plane Y (simulation space)").c_str(),"collisionPlaneHeight",emitter.forces.collisionPlaneHeight,0.01f,-100000.0f,100000.0f);
                forcesEditor.Float(VfxUiText("Bounce").c_str(),"restitution",emitter.forces.restitution,0.01f,0.0f,1.0f);
                forcesEditor.Float(VfxUiText("Friction").c_str(),"friction",emitter.forces.friction,0.01f,0.0f,1.0f);
                ImGui::TextDisabled(VfxUiText("Visual particle centers only; this is not gameplay collision.").c_str());
            }
            ImGui::SeparatorText(VfxUiText("Rotation over life").c_str());
            {
            VfxInspectorPropertyEditor editor(
                *this, database, emitter.rotation.id, statusMessage_);
            editor.AnimatedVec3(
                "Rotation over Life",
                "eulerDegrees",
                emitter.rotation.eulerDegrees,
                0.1f);
            }
            ImGui::SeparatorText(VfxUiText("Size over life").c_str());
            {
            VfxInspectorPropertyEditor editor(
                *this, database, emitter.scale.id, statusMessage_);
            editor.AnimatedVec3(
                "Scale over Life", "value", emitter.scale.value);
            }
            break;
        }
        case VfxEmitterInspectorPage::Render: DrawEmitterAppearance(database, emitter); break;
        case VfxEmitterInspectorPage::Advanced:
            if (ImGui::CollapsingHeader(TEXT::Localize("ui.vfx.layer.parameters").c_str())) DrawEmitterBindingsInspector(emitter);
            if (ImGui::CollapsingHeader(TEXT::Localize("ui.vfx.layer.attachment").c_str())) DrawEmitterAdvanced(database, emitter);
            break;
        case VfxEmitterInspectorPage::Bindings: DrawEmitterBindingsInspector(emitter); break;
        }
#else
        (void)database; (void)source;
#endif
    }
}
