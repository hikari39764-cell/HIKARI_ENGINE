#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

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
    void VfxWorkspaceController::DrawEmitterAppearance(AssetDatabase& database, const VFX::VfxEmitterData& emitter) {
#if defined(HIKARI_WITH_EDITOR)
        ImGui::SeparatorText(VfxUiText("Color").c_str());
        VfxInspectorPropertyEditor colorEditor(
            *this, database, emitter.color.id, statusMessage_);
        colorEditor.AnimatedColor(
            VfxUiText("Color over Life").c_str(), "value", emitter.color.value);

        if (emitter.kind == VFX::VfxLayerKind::Group) return;

        ImGui::SeparatorText(VfxUiText("Surface and lighting").c_str());
        VfxInspectorPropertyEditor renderCommonEditor(
            *this,
            database,
            emitter.renderCommon.id,
            statusMessage_);
        const auto materialModes = VFX::GetVfxMaterialModeChoices();
        renderCommonEditor.Choice(
            VfxUiText("Lighting / material").c_str(),
            "materialMode",
            VFX::ToString(emitter.renderCommon.materialMode),
            materialModes.data(),
            materialModes.size());
        if (!VFX::IsCustomVfxMaterial(emitter.renderCommon.materialMode)) {
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
            VfxUiText("Blend Mode").c_str(),
            "blendMode",
            VFX::ToString(emitter.renderCommon.blendMode),
            blendModes.data(),
            blendModes.size());
        constexpr std::array<std::string_view, 4u> depthModes{
            "WorldOccluded",
            "SoftIntersection",
            "ContactSurface",
            "ForegroundAccent",
        };
        renderCommonEditor.Choice(
            VfxUiText("Depth Policy").c_str(),
            "depthMode",
            VFX::ToString(emitter.renderCommon.depthMode),
            depthModes.data(),
            depthModes.size());
        if (emitter.renderCommon.depthMode ==
            VFX::VfxParticleDepthMode::SoftIntersection) {
            renderCommonEditor.Float(
                VfxUiText("Soft Intersection Distance").c_str(),
                "softIntersectionDistance",
                emitter.renderCommon.softIntersectionDistance,
                0.01f,
                0.001f,
                10.0f);
        }
        if (emitter.renderCommon.depthMode ==
            VFX::VfxParticleDepthMode::ContactSurface) {
            renderCommonEditor.Float(
                VfxUiText("Surface Depth Bias").c_str(),
                "depthBias",
                emitter.renderCommon.depthBias,
                0.00005f,
                0.0f,
                0.02f);
        }

        DrawLayerEffects(database, emitter);
        if (ImGui::CollapsingHeader(VfxUiText("Texture animation / flipbook").c_str())) {
        renderCommonEditor.Unsigned(
            VfxUiText("Columns").c_str(),
            "flipbookColumns",
            emitter.renderCommon.flipbookColumns,
            1u,
            64u);
        renderCommonEditor.Unsigned(
            VfxUiText("Rows").c_str(),
            "flipbookRows",
            emitter.renderCommon.flipbookRows,
            1u,
            64u);
        renderCommonEditor.Unsigned(
            VfxUiText("Frame Count").c_str(),
            "flipbookFrameCount",
            emitter.renderCommon.flipbookFrameCount,
            1u,
            emitter.renderCommon.flipbookColumns *
                emitter.renderCommon.flipbookRows);
        renderCommonEditor.Float(
            VfxUiText("Frames Per Second").c_str(),
            "flipbookFramesPerSecond",
            emitter.renderCommon.flipbookFramesPerSecond,
            0.1f,
            0.0f,
            240.0f);
        renderCommonEditor.Bool(
            VfxUiText("Loop").c_str(),
            "flipbookLoop",
            emitter.renderCommon.flipbookLoop);
        renderCommonEditor.Bool(
            VfxUiText("Random Start").c_str(),
            "flipbookRandomStart",
            emitter.renderCommon.flipbookRandomStart);
        }
#else
        (void)database; (void)emitter;
#endif
    }
}
