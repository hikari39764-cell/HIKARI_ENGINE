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
                    "Color, particle material and render shape",
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
            editor.Unsigned(
                "Maximum Particles",
                "maximumParticles",
                emitter.spawn.maximumParticles,
                1u,
                RENDER3D::PARTICLES::kGpuParticlePoolCapacity);
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
            constexpr std::array<std::string_view, 3u> shapes{
                "Point", "Sphere", "Cone"
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
            break;
        }
        case VfxEmitterInspectorPage::Render: {
            ImGui::SeparatorText("Color");
            VfxInspectorPropertyEditor colorEditor(
                *this, database, emitter.color.id, statusMessage_);
            colorEditor.AnimatedColor(
                "Color over Life", "value", emitter.color.value);

            ImGui::SeparatorText("Render Common");
            AssetGuid selected = emitter.renderCommon.materialAssetGuid;
            AssetGuid created{};
            if (DrawVfxParticleMaterialField(
                    database,
                    particleMaterialCatalog_,
                    emitter.renderCommon.materialAssetGuid,
                    selected,
                    created,
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
                    if (!RefreshAuthoredAsset(created, refreshMessage)) {
                        statusMessage_ +=
                            "; live Particle Material refresh failed: " +
                            refreshMessage;
                    } else if (!refreshMessage.empty()) {
                        statusMessage_ += "; " + refreshMessage;
                    }
                }
            }
            VfxInspectorPropertyEditor renderCommonEditor(
                *this,
                database,
                emitter.renderCommon.id,
                statusMessage_);
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

            ImGui::SeparatorText("Render Shape");
            VfxInspectorPropertyEditor renderShapeEditor(
                *this,
                database,
                emitter.renderShape.id,
                statusMessage_);
            ImGui::TextDisabled("Shape: Sprite");
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
