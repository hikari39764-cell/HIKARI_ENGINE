#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"

#include <array>
#include <string_view>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Vfx/HIKARI_VfxGeometryPresetCatalog.h"
#include "Assets/Vfx/HIKARI_VfxEnumNames.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxInspectorControls.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxParticleMaterialField.h"
#include "Editor/Widgets/HIKARI_AssetFieldWidget.h"
#include "Render3D/Vfx/HIKARI_ParticleRenderInput.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {
namespace {
#if defined(HIKARI_WITH_EDITOR)
// 形状プリセット一覧の簡易シルエット。3D プレビューの代わりに、
// どの形か一目で分かる図を描く。
void DrawShapeSwatch(ImDrawList& list, const ImVec2& center, float size, VFX::VfxRenderShapeType shape, ImU32 color) {
    const float r = size * 0.5f;
    const float t = 1.8f;
    switch (shape) {
    case VFX::VfxRenderShapeType::Sprite:
        list.AddRect(ImVec2(center.x - r * 0.9f, center.y - r * 0.65f),
                     ImVec2(center.x + r * 0.9f, center.y + r * 0.65f), color, 0.0f, 0, t);
        break;
    case VFX::VfxRenderShapeType::Ribbon:
        list.AddRect(ImVec2(center.x - r * 0.9f, center.y - r * 0.14f),
                     ImVec2(center.x + r * 0.9f, center.y + r * 0.14f), color, 0.0f, 0, t);
        break;
    case VFX::VfxRenderShapeType::Ring:
        list.PathArcTo(center, r * 0.8f, 0.6f, 4.2f, 24);
        list.PathStroke(color, 0, t);
        break;
    case VFX::VfxRenderShapeType::MeshBox:
        list.AddRect(ImVec2(center.x - r * 0.66f, center.y - r * 0.66f),
                     ImVec2(center.x + r * 0.66f, center.y + r * 0.66f), color, 0.0f, 0, t);
        break;
    case VFX::VfxRenderShapeType::MeshSphere: list.AddCircle(center, r * 0.8f, color, 0, t); break;
    case VFX::VfxRenderShapeType::MeshHemisphere:
        list.PathArcTo(center, r * 0.8f, 3.14159265f, 6.28318531f, 24);
        list.PathStroke(color, 0, t);
        list.AddLine(ImVec2(center.x - r * 0.8f, center.y), ImVec2(center.x + r * 0.8f, center.y), color, t);
        break;
    case VFX::VfxRenderShapeType::MeshCylinder:
        list.AddRect(ImVec2(center.x - r * 0.6f, center.y - r * 0.8f), ImVec2(center.x + r * 0.6f, center.y + r * 0.8f),
                     color, 0.0f, 0, t);
        list.AddLine(ImVec2(center.x - r * 0.6f, center.y - r * 0.45f),
                     ImVec2(center.x + r * 0.6f, center.y - r * 0.45f), color, t * 0.6f);
        break;
    case VFX::VfxRenderShapeType::MeshCone:
        list.AddTriangle(ImVec2(center.x, center.y - r * 0.85f), ImVec2(center.x - r * 0.7f, center.y + r * 0.8f),
                         ImVec2(center.x + r * 0.7f, center.y + r * 0.8f), color, t);
        break;
    case VFX::VfxRenderShapeType::MeshCapsule:
        list.AddRect(ImVec2(center.x - r * 0.5f, center.y - r * 0.8f), ImVec2(center.x + r * 0.5f, center.y + r * 0.8f),
                     color, r * 0.5f, 0, t);
        break;
    case VFX::VfxRenderShapeType::MeshTorus:
        list.AddCircle(center, r * 0.85f, color, 0, t);
        list.AddCircle(center, r * 0.4f, color, 0, t);
        break;
    case VFX::VfxRenderShapeType::CrossPlane:
        list.AddRect(ImVec2(center.x - r * 0.85f, center.y - r * 0.12f),
                     ImVec2(center.x + r * 0.85f, center.y + r * 0.12f), color, 0.0f, 0, t);
        list.AddRect(ImVec2(center.x - r * 0.12f, center.y - r * 0.85f),
                     ImVec2(center.x + r * 0.12f, center.y + r * 0.85f), color, 0.0f, 0, t);
        break;
    default: list.AddCircle(center, r * 0.6f, color, 0, t); break;
    }
}

VfxEmitterInspectorPage DrawPageNavigation(VfxEmitterInspectorPage current, bool particles) {
    struct Page {
        VfxEmitterInspectorPage id;
        const char* label;
        bool particlesOnly;
    };
    constexpr std::array pages{Page{VfxEmitterInspectorPage::Common, "ui.vfx.layer.shape", false},
                               Page{VfxEmitterInspectorPage::Render, "ui.vfx.layer.appearance", false},
                               Page{VfxEmitterInspectorPage::Spawn, "ui.vfx.layer.emission", true},
                               Page{VfxEmitterInspectorPage::Position, "ui.vfx.layer.motion", true},
                               Page{VfxEmitterInspectorPage::Advanced, "ui.vfx.layer.advanced", false}};
    auto requested = current;
    constexpr EditorGlyph icons[]{EditorGlyph::Model, EditorGlyph::Material, EditorGlyph::Vfx, EditorGlyph::Translate,
                                  EditorGlyph::Settings};
    bool first = true;
    for (size_t i = 0; i < pages.size(); ++i) {
        const auto& page = pages[i];
        if (page.particlesOnly && !particles)
            continue;
        if (!first)
            NextToolbarItem();
        first = false;
        if (IconToggleButton(icons[i], page.label, current == page.id, {28, 28}, TEXT::LocalizeCStr(page.label)))
            requested = page.id;
    }
    for (const auto& page : pages)
        if (page.id == current) {
            ImGui::TextUnformatted(TEXT::LocalizeCStr(page.label));
            break;
        }
    ImGui::Separator();
    return requested;
}
#endif
} // namespace

#if defined(HIKARI_WITH_EDITOR)
// 調整レイヤーは下位の兄弟とその子孫にだけ効くので、幾何や生成の頁を持たない。
void VfxWorkspaceController::DrawAdjustmentLayerInspector(AssetDatabase& database, const VFX::VfxEmitterData& emitter) {
    VfxInspectorPropertyEditor edit(*this, database, emitter.id, statusMessage_);
    edit.Text("Name", "name", emitter.name);
    VfxInspectorPropertyEditor basic(*this, database, emitter.basic.id, statusMessage_);
    basic.Bool("Enabled", "enabled", emitter.basic.enabled);
    ImGui::TextWrapped("%s", TEXT::Localize("ui.vfx.adjustment.scope").c_str());
    edit.Float(VfxUiText("Start Delay").c_str(), "startDelay", emitter.startDelay, 0.01f, 0,
               preview_.GetPreviewTimelineDuration());
    edit.Float(VfxUiText("Active Duration (0 = until asset end)").c_str(), "activeDuration", emitter.activeDuration,
               0.01f, 0, preview_.GetPreviewTimelineDuration());
    if (ImGui::TreeNode(TEXT::Localize("ui.vfx.effects.space.frame").c_str())) {
        VfxInspectorPropertyEditor frame(*this, database, emitter.emitterTransform.id, statusMessage_);
        frame.Vec3(VfxUiText("Position").c_str(), "transform.position", emitter.emitterTransform.transform.position);
        frame.Vec3(VfxUiText("Rotation (degrees)").c_str(), "transform.rotationEulerDegrees",
                   emitter.emitterTransform.transform.rotationEulerDegrees, 0.1f);
        frame.Vec3(VfxUiText("Scale").c_str(), "transform.scale", emitter.emitterTransform.transform.scale);
        DrawLayerAnimation(database, emitter);
        ImGui::TreePop();
    }
    DrawLayerMatte(database, emitter);
    DrawLayerEffects(database, emitter);
}

void VfxWorkspaceController::DrawGeometryPresetPicker(const VFX::VfxEmitterData& emitter) {
    // 形状プリセット: 形状＋パラメータ＋変形を一括で適用する。
    // 新規の図元を増やさず、既存の組み合わせに名前を付けたもの。
    if (ImGui::Button(VfxUiText("Geometry Preset").c_str(), ImVec2(220.0f, 0.0f))) {
        ImGui::OpenPopup("VfxGeometryPresetPicker");
    }
    if (ImGui::BeginPopup("VfxGeometryPresetPicker")) {
        constexpr int kColumns = 6;
        constexpr float kCellSize = 72.0f;
        int column = 0;
        std::string_view category{};
        for (const auto& preset : VFX::GetVfxGeometryPresetCatalog()) {
            if (preset.category != category) {
                if (column != 0) {
                    ImGui::NewLine();
                    column = 0;
                }
                ImGui::SeparatorText(std::string(preset.category).c_str());
                category = preset.category;
            }
            if (column != 0)
                ImGui::SameLine();
            ImGui::PushID(preset.id.data());
            ImGui::BeginGroup();
            const ImVec2 origin = ImGui::GetCursorScreenPos();
            const bool clicked = ImGui::InvisibleButton("swatch", ImVec2(kCellSize, kCellSize));
            ImDrawList* list = ImGui::GetWindowDrawList();
            list->AddRectFilled(origin, ImVec2(origin.x + kCellSize, origin.y + kCellSize),
                                ImGui::GetColorU32(clicked ? ImGuiCol_ButtonActive : ImGuiCol_Button), 4.0f);
            DrawShapeSwatch(*list, ImVec2(origin.x + kCellSize * 0.5f, origin.y + kCellSize * 0.5f), kCellSize * 0.7f,
                            preset.shape.type, ImGui::GetColorU32(ImGuiCol_Text));
            ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + kCellSize);
            ImGui::TextWrapped("%s", preset.displayName.data());
            ImGui::PopTextWrapPos();
            ImGui::EndGroup();
            ImGui::PopID();
            if (clicked) {
                (void)ApplyGeometryPreset(emitter.id, preset.id, statusMessage_);
                ImGui::CloseCurrentPopup();
            }
            column = (column + 1) % kColumns;
        }
        ImGui::EndPopup();
    }
}

void VfxWorkspaceController::DrawEmitterGeometry(AssetDatabase& database, const VFX::VfxEmitterData& emitter) {
    DrawLayerMatte(database, emitter);
    if (emitter.kind != VFX::VfxLayerKind::Group && emitter.history.mode != VFX::VfxHistoryMode::Trail) {
        ImGui::SeparatorText(TEXT::Localize("ui.vfx.layer.geometry").c_str());
        DrawGeometryPresetPicker(emitter);
        VfxInspectorPropertyEditor renderShapeEditor(*this, database, emitter.renderShape.id, statusMessage_);
        const auto renderShapes = VFX::kVfxRenderShapeTypeChoices;
        renderShapeEditor.Choice(VfxUiText("Geometry").c_str(), "type", VFX::ToString(emitter.renderShape.type),
                                 renderShapes.data(), renderShapes.size());
        if (emitter.renderShape.type == VFX::VfxRenderShapeType::Sprite ||
            emitter.renderShape.type == VFX::VfxRenderShapeType::Ring ||
            emitter.renderShape.type == VFX::VfxRenderShapeType::Ribbon) {
            const auto orientationModes = VFX::kVfxBillboardModeChoices;
            renderShapeEditor.Choice(VfxUiText("Orientation").c_str(), "billboardMode",
                                     VFX::ToString(emitter.renderShape.billboardMode), orientationModes.data(),
                                     orientationModes.size());
            // Look At Line だけ、固定するローカル軸を選べる。
            if (emitter.renderShape.billboardMode == VFX::VfxBillboardMode::CylindricalBillboard) {
                const auto axisModes = VFX::kVfxBillboardAxisChoices;
                renderShapeEditor.Choice(VfxUiText("Fixed Axis").c_str(), "billboardAxis",
                                         VFX::ToString(emitter.renderShape.billboardAxis), axisModes.data(),
                                         axisModes.size());
            }
        }
        if (emitter.renderShape.type == VFX::VfxRenderShapeType::Ribbon) {
            renderShapeEditor.Unsigned(VfxUiText("Ribbon Segments").c_str(), "ribbonSegmentCount",
                                       emitter.renderShape.ribbonSegmentCount, 2u, 64u);
            (void)DrawLayerParameter(emitter, emitter.renderShape.id, "ribbonWidth");
        }
        if (emitter.renderShape.type == VFX::VfxRenderShapeType::Ring) {
            (void)DrawLayerParameter(emitter, emitter.renderShape.id, "ringOuterRadius");
            (void)DrawLayerParameter(emitter, emitter.renderShape.id, "ringInnerRadius");
            renderShapeEditor.Unsigned(VfxUiText("Ring Segments").c_str(), "ringSegmentCount",
                                       emitter.renderShape.ringSegmentCount, 3u, 64u);
            (void)DrawLayerParameter(emitter, emitter.renderShape.id, "ringStartAngleDegrees");
            (void)DrawLayerParameter(emitter, emitter.renderShape.id, "ringEndAngleDegrees");
        }
        if (emitter.renderShape.type == VFX::VfxRenderShapeType::MeshCylinder ||
            emitter.renderShape.type == VFX::VfxRenderShapeType::MeshCone) {
            renderShapeEditor.Unsigned(VfxUiText("Radial segments").c_str(), "ringSegmentCount",
                                       emitter.renderShape.ringSegmentCount, 3u, 64u);
            if (emitter.renderShape.type == VFX::VfxRenderShapeType::MeshCylinder) {
                renderShapeEditor.Float(VfxUiText("Top Radius").c_str(), "cylinderTopRadius",
                                        emitter.renderShape.cylinderTopRadius, 0.01f, 0.0f, 100.0f);
                renderShapeEditor.Float(VfxUiText("Bottom Radius").c_str(), "cylinderBottomRadius",
                                        emitter.renderShape.cylinderBottomRadius, 0.01f, 0.0f, 100.0f);
            } else {
                renderShapeEditor.Float(VfxUiText("Base Radius").c_str(), "coneBaseRadius",
                                        emitter.renderShape.coneBaseRadius, 0.01f, 0.0f, 100.0f);
            }
            ImGui::TextWrapped(
                VfxUiText(
                    "Open shell along local Y. Scale controls diameter and length; Rotation sets its direction. UV wraps around the shell.")
                    .c_str());
        }
        if (emitter.renderShape.type == VFX::VfxRenderShapeType::CrossPlane)
            ImGui::TextWrapped(
                VfxUiText(
                    "Two intersecting planes in local space. Useful for flames and beams viewed from different sides.")
                    .c_str());
        if (emitter.renderShape.type == VFX::VfxRenderShapeType::MeshTorus) {
            renderShapeEditor.Unsigned(VfxUiText("Major Segments").c_str(), "ringSegmentCount",
                                       emitter.renderShape.ringSegmentCount, 8u, 64u);
            renderShapeEditor.Float(VfxUiText("Major Radius").c_str(), "torusMajorRadius",
                                    emitter.renderShape.torusMajorRadius, 0.01f, 0.0f, 100.0f);
            renderShapeEditor.Float(VfxUiText("Minor Radius").c_str(), "torusMinorRadius",
                                    emitter.renderShape.torusMinorRadius, 0.01f, 0.0f, 100.0f);
        }
        if (emitter.renderShape.type == VFX::VfxRenderShapeType::MeshCapsule) {
            renderShapeEditor.Unsigned(VfxUiText("Radial segments").c_str(), "ringSegmentCount",
                                       emitter.renderShape.ringSegmentCount, 8u, 64u);
            renderShapeEditor.Float(VfxUiText("Radius").c_str(), "capsuleRadius", emitter.renderShape.capsuleRadius,
                                    0.01f, 0.01f, 0.5f);
        }
        // 長さ軸を持つ形状だけ、ベンドとツイストを掛けられる。
        switch (emitter.renderShape.type) {
        case VFX::VfxRenderShapeType::Ribbon:
        case VFX::VfxRenderShapeType::MeshBox:
        case VFX::VfxRenderShapeType::MeshCylinder:
        case VFX::VfxRenderShapeType::MeshCone:
        case VFX::VfxRenderShapeType::CrossPlane:
            renderShapeEditor.Float(VfxUiText("Bend").c_str(), "bendAmount", emitter.renderShape.bendAmount, 0.01f,
                                    -4.0f, 4.0f);
            renderShapeEditor.Float(VfxUiText("Twist (degrees)").c_str(), "twistDegrees",
                                    emitter.renderShape.twistDegrees, 1.0f, -1440.0f, 1440.0f);
            break;
        default: break;
        }
        // UV 契約: 形状パラメータUVを材質UVへどう写すか。Parameter は
        // 既存挙動、WorldLength は弧長・周長・高さで U を実寸タイルする。
        const auto uvModes = VFX::kVfxShapeUvModeChoices;
        renderShapeEditor.Choice(VfxUiText("UV Mode").c_str(), "uvMode", VFX::ToString(emitter.renderShape.uvMode),
                                 uvModes.data(), uvModes.size());
        renderShapeEditor.Float(VfxUiText("UV Tile Length (world)").c_str(), "uvTileLength",
                                emitter.renderShape.uvTileLength, 0.01f, 0.0001f, 100.0f);
        renderShapeEditor.Float(VfxUiText("U Tiles").c_str(), "uvTilingU", emitter.renderShape.uvTilingU, 0.01f,
                                0.0001f, 64.0f);
        renderShapeEditor.Float(VfxUiText("V Tiles").c_str(), "uvTilingV", emitter.renderShape.uvTilingV, 0.01f,
                                0.0001f, 64.0f);
        renderShapeEditor.Float(VfxUiText("UV Rotation (degrees)").c_str(), "uvRotationDegrees",
                                emitter.renderShape.uvRotationDegrees, 0.5f, -180.0f, 180.0f);
        renderShapeEditor.Bool(VfxUiText("Swap UV Axes").c_str(), "uvSwapAxes", emitter.renderShape.uvSwapAxes);
        // 幅テーパー: 形状パラメータ 0..1 に沿って断面を収窄する。
        // ビームや斬撃の両端を尖らせるのに使う。
        renderShapeEditor.Float(VfxUiText("Taper Start").c_str(), "taperStart", emitter.renderShape.taperStart, 0.01f,
                                0.0f, 4.0f);
        renderShapeEditor.Float(VfxUiText("Taper End").c_str(), "taperEnd", emitter.renderShape.taperEnd, 0.01f, 0.0f,
                                4.0f);
    }
    DrawEmitterPlacement(database, emitter);
}

// アンカー・接続ターゲット・Mesh 出力: レイヤーの幾何をどこへどう張るか。
void VfxWorkspaceController::DrawEmitterPlacement(AssetDatabase& database, const VFX::VfxEmitterData& emitter) {
    // アンカーは形状中心と変換原点を分ける。刀身のように端から伸ばす
    // レイヤーはここをずらすと長さを変えても基準点が動かない。
    {
        VfxInspectorPropertyEditor anchorEditor(*this, database, emitter.id, statusMessage_);
        anchorEditor.Vec3(VfxUiText("Anchor (local)").c_str(), "anchor", emitter.anchor);
    }
    // 接続ターゲット: 幾何を「このレイヤー → 参照点」の区間へ張る。
    // 参照点は別レイヤーか、エフェクト内の固定点。
    {
        VfxInspectorPropertyEditor connectEditor(*this, database, emitter.id, statusMessage_);
        connectEditor.Choice(VfxUiText("Connect To").c_str(), "connectTarget.mode",
                             VFX::ToString(emitter.connectTarget.mode), VFX::kVfxConnectTargetModeChoices.data(),
                             VFX::kVfxConnectTargetModeChoices.size());
        if (emitter.connectTarget.mode != VFX::VfxConnectTargetMode::None) {
            if (emitter.connectTarget.mode == VFX::VfxConnectTargetMode::LayerOrigin) {
                connectEditor.Text(VfxUiText("Target Layer").c_str(), "connectTarget.targetLayerId",
                                   emitter.connectTarget.targetLayerId);
            } else {
                connectEditor.Vec3(VfxUiText("Target Point (effect space)").c_str(), "connectTarget.localPosition",
                                   emitter.connectTarget.localPosition);
            }
            connectEditor.Float(VfxUiText("Pivot").c_str(), "connectTarget.pivot", emitter.connectTarget.pivot, 0.01f,
                                0.0f, 1.0f);
            connectEditor.Float(VfxUiText("Length Scale").c_str(), "connectTarget.lengthScale",
                                emitter.connectTarget.lengthScale, 0.01f, 0.0f, 8.0f);
            connectEditor.Float(VfxUiText("Start Offset").c_str(), "connectTarget.startOffset",
                                emitter.connectTarget.startOffset, 0.01f, 0.0f, 100.0f);
            connectEditor.Float(VfxUiText("End Offset").c_str(), "connectTarget.endOffset",
                                emitter.connectTarget.endOffset, 0.01f, 0.0f, 100.0f);
            connectEditor.Bool(VfxUiText("Orient").c_str(), "connectTarget.orient", emitter.connectTarget.orient);
            connectEditor.Bool(VfxUiText("Stretch").c_str(), "connectTarget.stretch", emitter.connectTarget.stretch);
        }
    }
    // Mesh 出力: 手続き形状ではなくモデル素材をレイヤーの幾何にする。
    {
        ImGui::SeparatorText(VfxUiText("Mesh Output").c_str());
        VfxInspectorPropertyEditor meshEditor(*this, database, emitter.id, statusMessage_);
        meshEditor.Bool(VfxUiText("Use Model Asset").c_str(), "meshOutput.enabled", emitter.meshOutput.enabled);
        if (emitter.meshOutput.enabled) {
            const std::string meshLabel = VfxUiText("Mesh");
            std::string meshGuid = emitter.meshOutput.meshAssetGuid.value;
            AssetFieldOptions options{};
            options.label = meshLabel;
            options.requiredType = AssetType::Model;
            if (DrawAssetField(&database, options, meshGuid)) {
                std::string assignmentMessage{};
                (void)SetProperty(emitter.id, "meshOutput.meshAssetGuid", meshGuid, assignmentMessage, 0u, &database);
                statusMessage_ = assignmentMessage;
            }
            meshEditor.Bool(VfxUiText("Cast Shadows").c_str(), "meshOutput.castShadows",
                            emitter.meshOutput.castShadows);
            meshEditor.Bool(VfxUiText("Receive Shadows").c_str(), "meshOutput.receiveShadow",
                            emitter.meshOutput.receiveShadow);
        }
    }
}

void VfxWorkspaceController::DrawEmitterTiming(AssetDatabase& database, const VFX::VfxEmitterData& emitter) {
    ImGui::SeparatorText(TEXT::Localize("ui.vfx.layer.timing").c_str());

    VfxInspectorPropertyEditor emitterEditor(*this, database, emitter.id, statusMessage_);
    emitterEditor.Text(VfxUiText("Name").c_str(), "name", emitter.name);
    if (emitter.kind == VFX::VfxLayerKind::Shape) {
        emitterEditor.Choice(VfxUiText("Motion history").c_str(), "history.mode", VFX::ToString(emitter.history.mode),
                             VFX::kVfxHistoryModeChoices.data(), VFX::kVfxHistoryModeChoices.size());
        if (emitter.history.mode != VFX::VfxHistoryMode::None) {
            emitterEditor.Float(VfxUiText("History duration").c_str(), "history.duration", emitter.history.duration,
                                0.01f, 0.01f, 2);
            emitterEditor.Float(VfxUiText("Tail width").c_str(), "history.tailWidth", emitter.history.tailWidth, 0.01f,
                                0, 4);
            emitterEditor.Float(VfxUiText("Fade shape").c_str(), "history.fadePower", emitter.history.fadePower, 0.01f,
                                0.05f, 8);
            emitterEditor.Color(VfxUiText("Tail tint").c_str(), "history.tailColor", emitter.history.tailColor);
            emitterEditor.Unsigned(VfxUiText("History samples").c_str(), "history.samples", emitter.history.samples, 2,
                                   16);
            emitterEditor.Float(VfxUiText("Teleport break distance").c_str(), "history.breakDistance",
                                emitter.history.breakDistance, 0.1f, 0.01f, 10000);
            if (emitter.history.mode == VFX::VfxHistoryMode::Trail) {
                VfxInspectorPropertyEditor geometry(*this, database, emitter.renderShape.id, statusMessage_);
                (void)DrawLayerParameter(emitter, emitter.renderShape.id, "ribbonWidth");
                ImGui::TextWrapped(
                    "%s",
                    VfxUiText(
                        "Trail connects the layer's local X edges through world-space motion. Animate position or attach it to a moving object.")
                        .c_str());
            }
        }
    }
    emitterEditor.Float(VfxUiText("Start Delay (seconds)").c_str(), "startDelay", emitter.startDelay, 0.01f, 0.0f,
                        document_.Asset().durationSeconds > 0.0f ? document_.Asset().durationSeconds : 3600.0f);
    emitterEditor.Float(VfxUiText("Active Duration (0 = until asset end)").c_str(), "activeDuration",
                        emitter.activeDuration, 0.01f, 0.0f,
                        document_.Asset().durationSeconds > 0.0f ? document_.Asset().durationSeconds : 3600.0f);

    ImGui::SeparatorText(VfxUiText("Basic").c_str());
    VfxInspectorPropertyEditor basicEditor(*this, database, emitter.basic.id, statusMessage_);
    basicEditor.Bool(VfxUiText("Enabled").c_str(), "enabled", emitter.basic.enabled);
    if (emitter.kind == VFX::VfxLayerKind::Particles) {
        basicEditor.Bool(VfxUiText("Follow Emitter (Local Space)").c_str(), "localSimulation",
                         emitter.basic.localSimulation);
        ImGui::TextWrapped(
            VfxUiText("Off: particles retain their spawn transform. Forces use the simulation space.").c_str());
        basicEditor.FloatDistribution("Lifetime", "lifetime", emitter.basic.lifetime, 0.01f, 0.001f, 3600.0f);
    }
    ImGui::SeparatorText(TEXT::Localize("ui.vfx.layer.offset").c_str());
    VfxInspectorPropertyEditor transformEditor(*this, database, emitter.emitterTransform.id, statusMessage_);
    transformEditor.Vec3(VfxUiText("Position").c_str(), "transform.position",
                         emitter.emitterTransform.transform.position);
    transformEditor.Vec3(VfxUiText("Rotation (degrees)").c_str(), "transform.rotationEulerDegrees",
                         emitter.emitterTransform.transform.rotationEulerDegrees, 0.1f);
    transformEditor.Vec3(VfxUiText("Scale").c_str(), "transform.scale", emitter.emitterTransform.transform.scale);

    if (emitter.kind != VFX::VfxLayerKind::Particles)
        DrawLayerAnimation(database, emitter);
}

void VfxWorkspaceController::DrawEmitterSpawnPage(AssetDatabase& database, const VFX::VfxEmitterData& emitter) {
    VfxInspectorPropertyEditor editor(*this, database, emitter.spawn.id, statusMessage_);
    editor.Float("Rate / second", "ratePerSecond", emitter.spawn.ratePerSecond, 0.1f, 0.0f, 1000000.0f);
    const auto initial = std::find_if(emitter.spawn.burstTimeline.begin(), emitter.spawn.burstTimeline.end(),
                                      [](const auto& key) { return key.time == 0; });
    int initialCount = initial == emitter.spawn.burstTimeline.end() ? 0 : int(initial->count);
    if (ImGui::DragInt(TEXT::Localize("ui.vfx.spawn.initialBurst").c_str(), &initialCount, 1, 0,
                       int(emitter.spawn.maximumParticles), "%d", ImGuiSliderFlags_AlwaysClamp)) {
        if (initial != emitter.spawn.burstTimeline.end()) {
            if (initialCount == 0)
                (void)RemoveBurstKey(initial->id, statusMessage_);
            else
                (void)SetBurstKey(initial->id, 0, uint32_t(initialCount), statusMessage_,
                                  VfxInspectorPropertyEditor::MergeGroup(emitter.spawn.id, "initialBurst"));
        } else if (initialCount > 0) {
            std::string id;
            (void)AddBurstKey(emitter.id, 0, uint32_t(initialCount), {}, id, statusMessage_);
        }
    }
    if (ImGui::IsItemDeactivatedAfterEdit())
        SealPropertyEdit();
    editor.Choice("Capacity Mode", "capacityMode", VFX::ToString(emitter.spawn.capacityMode),
                  VFX::kVfxParticleCapacityModeChoices.data(), VFX::kVfxParticleCapacityModeChoices.size());
    editor.Unsigned(emitter.spawn.capacityMode == VFX::VfxParticleCapacityMode::Automatic ? "Capacity Ceiling"
                                                                                          : "Maximum Particles",
                    "maximumParticles", emitter.spawn.maximumParticles, 1u,
                    RENDER3D::PARTICLES::kGpuParticlePoolCapacity);
    if (emitter.spawn.capacityMode == VFX::VfxParticleCapacityMode::Automatic) {
        ImGui::TextDisabled(VfxUiText("GPU capacity is derived from spawn rate and maximum lifetime.").c_str());
    }
    ImGui::SeparatorText(VfxUiText("Emission volume").c_str());
    VfxInspectorPropertyEditor shapeEditor(*this, database, emitter.shape.id, statusMessage_);
    shapeEditor.Choice(VfxUiText("Type").c_str(), "type", VFX::ToString(emitter.shape.type),
                       VFX::kVfxShapeTypeChoices.data(), VFX::kVfxShapeTypeChoices.size());
    if (emitter.shape.type != VFX::VfxShapeType::Point) {
        shapeEditor.FloatDistribution("Radius", "radius", emitter.shape.radius, 0.01f, 0.0f, 100000.0f);
    }
    if (emitter.shape.type == VFX::VfxShapeType::Cone) {
        shapeEditor.Float(VfxUiText("Cone Angle (degrees)").c_str(), "coneAngleDegrees", emitter.shape.coneAngleDegrees,
                          0.1f, 0.0f, 180.0f);
    }
    if (emitter.shape.type == VFX::VfxShapeType::Box) {
        shapeEditor.Vec3(VfxUiText("Box Half Extents").c_str(), "boxHalfExtents", emitter.shape.boxHalfExtents, 0.01f);
    }
    if (emitter.shape.type == VFX::VfxShapeType::Cylinder) {
        shapeEditor.Float(VfxUiText("Cylinder Height").c_str(), "cylinderHeight", emitter.shape.cylinderHeight, 0.01f,
                          0.0f, 100000.0f);
    }
    if (emitter.shape.type == VFX::VfxShapeType::Circle) {
        shapeEditor.Float(VfxUiText("Circle Inner Radius").c_str(), "circleInnerRadius",
                          emitter.shape.circleInnerRadius, 0.01f, 0.0f, 100000.0f);
        shapeEditor.Bool(VfxUiText("Emit On Edge Only").c_str(), "circleEmitOnEdge", emitter.shape.circleEmitOnEdge);
        // 放出角度範囲。360 で全周。リングの弧に沿って火花を出したい
        // ときはここを絞り、エミッタ自体を回して狙いを付ける。
        shapeEditor.Float(VfxUiText("Circle Arc Degrees").c_str(), "circleArcDegrees", emitter.shape.circleArcDegrees,
                          1.0f, 1.0f, 360.0f);
    }
    if (emitter.shape.type == VFX::VfxShapeType::Edge) {
        shapeEditor.Float(VfxUiText("Edge Length").c_str(), "edgeLength", emitter.shape.edgeLength, 0.01f, 0.0f,
                          100000.0f);
    }
}

void VfxWorkspaceController::DrawEmitterMotionPage(AssetDatabase& database, const VFX::VfxEmitterData& emitter) {
    ImGui::SeparatorText(VfxUiText("Position").c_str());
    VfxInspectorPropertyEditor positionEditor(*this, database, emitter.position.id, statusMessage_);
    positionEditor.FloatDistribution("Radial Speed", "radialSpeed", emitter.position.radialSpeed, 0.01f);
    positionEditor.AnimatedVec3(VfxUiText("Position over Life").c_str(), "value", emitter.position.value);

    ImGui::SeparatorText(VfxUiText("Forces").c_str());
    VfxInspectorPropertyEditor forcesEditor(*this, database, emitter.forces.id, statusMessage_);
    forcesEditor.Vec3(VfxUiText("Gravity").c_str(), "gravity", emitter.forces.gravity);
    forcesEditor.Float(VfxUiText("Drag").c_str(), "drag", emitter.forces.drag, 0.01f, 0.0f, 1000.0f);
    if (ImGui::TreeNode(VfxUiText("Flow fields").c_str())) {
        const auto& field = emitter.forces.field;
        forcesEditor.Float(VfxUiText("Curl turbulence").c_str(), "field.turbulence", field.turbulence, 0.05f, 0, 10000);
        forcesEditor.Float(VfxUiText("Field frequency").c_str(), "field.frequency", field.frequency, 0.01f, 0.001f,
                           1000);
        forcesEditor.Float(VfxUiText("Field evolution").c_str(), "field.evolution", field.evolution, 0.01f, -1000,
                           1000);
        forcesEditor.Float(VfxUiText("Vortex strength").c_str(), "field.vortex", field.vortex, 0.05f, -10000, 10000);
        forcesEditor.Float(VfxUiText("Attraction / repulsion").c_str(), "field.attraction", field.attraction, 0.05f,
                           -10000, 10000);
        forcesEditor.Vec3(VfxUiText("Field center").c_str(), "field.center", field.center);
        forcesEditor.Vec3(VfxUiText("Field axis").c_str(), "field.axis", field.axis);
        forcesEditor.Float(VfxUiText("Field radius (0 = global)").c_str(), "field.radius", field.radius, 0.05f, 0,
                           100000);
        ImGui::TreePop();
    }
    forcesEditor.Bool(VfxUiText("Plane Collision").c_str(), "collisionPlaneEnabled",
                      emitter.forces.collisionPlaneEnabled);
    if (emitter.forces.collisionPlaneEnabled) {
        forcesEditor.Float(VfxUiText("Plane Y (simulation space)").c_str(), "collisionPlaneHeight",
                           emitter.forces.collisionPlaneHeight, 0.01f, -100000.0f, 100000.0f);
        forcesEditor.Float(VfxUiText("Bounce").c_str(), "restitution", emitter.forces.restitution, 0.01f, 0.0f, 1.0f);
        forcesEditor.Float(VfxUiText("Friction").c_str(), "friction", emitter.forces.friction, 0.01f, 0.0f, 1.0f);
        ImGui::TextDisabled(VfxUiText("Visual particle centers only; this is not gameplay collision.").c_str());
    }
    ImGui::SeparatorText(VfxUiText("Rotation over life").c_str());
    {
        VfxInspectorPropertyEditor editor(*this, database, emitter.rotation.id, statusMessage_);
        editor.AnimatedVec3("Rotation over Life", "eulerDegrees", emitter.rotation.eulerDegrees, 0.1f);
    }
    ImGui::SeparatorText(VfxUiText("Size over life").c_str());
    {
        VfxInspectorPropertyEditor editor(*this, database, emitter.scale.id, statusMessage_);
        editor.AnimatedVec3("Scale over Life", "value", emitter.scale.value);
    }
}

#endif

void VfxWorkspaceController::DrawEmitterInspector(AssetDatabase& database, const VFX::VfxEmitterData& source) {
#if defined(HIKARI_WITH_EDITOR)
    const VFX::VfxEmitterData emitter = source;
    if (emitterInspectorEmitterId_ != emitter.id) {
        emitterInspectorEmitterId_ = emitter.id;
        emitterInspectorPage_ = VfxEmitterInspectorPage::Common;
    }
    if (selection_.kind == VfxWorkspaceSelectionKind::Bindings)
        emitterInspectorPage_ = VfxEmitterInspectorPage::Bindings;
    if (selection_.kind == VfxWorkspaceSelectionKind::SpecialOutput)
        emitterInspectorPage_ = VfxEmitterInspectorPage::Advanced;
    if (emitter.kind == VFX::VfxLayerKind::Adjustment) {
        DrawAdjustmentLayerInspector(database, emitter);
        return;
    }
    ImGui::TextWrapped("%s", emitter.name.c_str());
    if (emitter.kind == VFX::VfxLayerKind::Group)
        ImGui::TextDisabled("%s", TEXT::Localize("ui.vfx.layer.newGroup").c_str());
    else
        ImGui::TextDisabled("%s  |  %s", VFX::ToString(emitter.renderShape.type).data(),
                            VFX::ToString(emitter.renderCommon.materialMode).data());
    const auto requested = DrawPageNavigation(emitterInspectorPage_, emitter.kind == VFX::VfxLayerKind::Particles);
    if (requested != emitterInspectorPage_) {
        emitterInspectorPage_ = requested;
        Select({VfxWorkspaceSelectionKind::Emitter, emitter.id, emitter.id});
    }
    switch (emitterInspectorPage_) {
    case VfxEmitterInspectorPage::Common:
        DrawEmitterGeometry(database, emitter);
        DrawEmitterTiming(database, emitter);
        break;
    case VfxEmitterInspectorPage::Spawn: DrawEmitterSpawnPage(database, emitter); break;
    case VfxEmitterInspectorPage::Position: DrawEmitterMotionPage(database, emitter); break;
    case VfxEmitterInspectorPage::Render: DrawEmitterAppearance(database, emitter); break;
    case VfxEmitterInspectorPage::Advanced:
        if (ImGui::CollapsingHeader(TEXT::Localize("ui.vfx.layer.parameters").c_str()))
            DrawEmitterBindingsInspector(emitter);
        if (ImGui::CollapsingHeader(TEXT::Localize("ui.vfx.layer.attachment").c_str()))
            DrawEmitterAdvanced(database, emitter);
        break;
    case VfxEmitterInspectorPage::Bindings: DrawEmitterBindingsInspector(emitter); break;
    }
#else
    (void)database;
    (void)source;
#endif
}
} // namespace HIKARI::EDITOR
