#include "Editor/Workspaces/TerrainSurface/HIKARI_TerrainSurfaceWorkspaceController.h"

#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
#include "Editor/HIKARI_EditorViewportInput.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspaceHost.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_SceneComponentId.h"
#include "Scene/HIKARI_SceneDocumentQueries.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Terrain/HIKARI_SurfaceLayerRegistry.h"
#include "Scene/Terrain/HIKARI_TerrainSurface.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceComponent.h"

#include <algorithm>
#include <array>
#include <string>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include "imgui_internal.h"
#endif

namespace HIKARI::EDITOR {

#if defined(HIKARI_WITH_EDITOR)
    namespace {
        EditorGlyph TerrainToolGlyph(EditorTerrainToolMode mode) {
            switch (mode) {
            case EditorTerrainToolMode::Raise: return EditorGlyph::TerrainRaise;
            case EditorTerrainToolMode::Lower: return EditorGlyph::TerrainLower;
            case EditorTerrainToolMode::Smooth: return EditorGlyph::TerrainSmooth;
            case EditorTerrainToolMode::Flatten: return EditorGlyph::TerrainFlatten;
            case EditorTerrainToolMode::Mound: return EditorGlyph::TerrainMound;
            case EditorTerrainToolMode::Pit: return EditorGlyph::TerrainPit;
            }
            return EditorGlyph::TerrainRaise;
        }

        // ツールボタンはアイコン描画に切り替え、ローカライズ文字列に
        // 依存しない。フォントの言語設定やグリフ欠落で文字が化ける
        // 問題を根本から回避する。名称はツールチップで表示する。
        bool ToolButton(
            EditorTerrainToolMode mode,
            const char* asciiId,
            const char* localizedName,
            bool selected,
            ImVec4 activeTint,
            float width) {
            if (selected) {
                ImGui::PushStyleColor(
                    ImGuiCol_Button,
                    ImVec4(activeTint.x, activeTint.y, activeTint.z, 0.85f));
                ImGui::PushStyleColor(
                    ImGuiCol_ButtonHovered,
                    ImVec4(activeTint.x, activeTint.y, activeTint.z, 1.0f));
                ImGui::PushStyleColor(
                    ImGuiCol_ButtonActive,
                    ImVec4(activeTint.x, activeTint.y, activeTint.z, 1.0f));
            }
            const float height = ImGui::GetFrameHeight() * 1.5f;
            const bool pressed = ImGui::Button(asciiId, ImVec2(width, height));
            if (selected) {
                ImGui::PopStyleColor(3);
            }
            // ボタン内にグリフを描く。
            const ImVec2 mn = ImGui::GetItemRectMin();
            const ImVec2 mx = ImGui::GetItemRectMax();
            const ImVec2 center{
                (mn.x + mx.x) * 0.5f,
                (mn.y + mx.y) * 0.5f
            };
            const ImU32 glyphColor =
                ImGui::GetColorU32(ImGuiCol_Text);
            DrawEditorGlyph(
                *ImGui::GetWindowDrawList(),
                TerrainToolGlyph(mode),
                center,
                height * 0.34f,
                glyphColor,
                2.0f);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", localizedName);
            }
            return pressed;
        }

        constexpr EditorTerrainToolMode kSculptTools[] = {
            EditorTerrainToolMode::Raise,
            EditorTerrainToolMode::Lower,
            EditorTerrainToolMode::Smooth,
            EditorTerrainToolMode::Flatten,
            EditorTerrainToolMode::Mound,
            EditorTerrainToolMode::Pit,
        };

        nlohmann::json ExtractTerrainSettingsPatch(
            const nlohmann::json& properties) {
            nlohmann::json patch = nlohmann::json::object();
            if (!properties.is_object()) {
                return patch;
            }
            static constexpr std::array<const char*, 8> keys{{
                "_schemaVersion",
                "presetId",
                "heightScale",
                "material",
                "deform",
                "shoreline",
                "layerPresetIds",
                "brush",
            }};
            for (const char* key : keys) {
                const auto found = properties.find(key);
                if (found != properties.end()) {
                    patch[key] = *found;
                }
            }
            return patch;
        }

        void ApplyTerrainSettingsPatch(
            nlohmann::json& properties,
            const nlohmann::json& patch) {
            if (!patch.is_object()) {
                return;
            }
            if (!properties.is_object()) {
                properties = nlohmann::json::object();
            }
            for (auto it = patch.begin(); it != patch.end(); ++it) {
                properties[it.key()] = it.value();
            }
        }
    } // namespace
#endif

    void TerrainSurfaceWorkspaceController::DrawDockSpace(
        bool resetDefaultDockLayout) const {
#if defined(HIKARI_WITH_EDITOR)
        ImGuiIO& io = ImGui::GetIO();
        if ((io.ConfigFlags & ImGuiConfigFlags_DockingEnable) == 0) {
            return;
        }
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImGuiID dockspaceId =
            ImGui::GetID("HIKARI_TerrainSurfaceDockSpace_v2");
        if (ImGui::DockBuilderGetNode(dockspaceId) == nullptr ||
            resetDefaultDockLayout) {
            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(
                dockspaceId,
                ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodePos(dockspaceId, viewport->WorkPos);
            ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

            ImGuiID centerNode = dockspaceId;
            ImGuiID leftNode = 0u;
            ImGuiID rightNode = 0u;
            ImGuiID rightTop = 0u;
            ImGuiID rightBottom = 0u;
            ImGui::DockBuilderSplitNode(
                centerNode, ImGuiDir_Left, 0.18f, &leftNode, &centerNode);
            ImGui::DockBuilderSplitNode(
                centerNode, ImGuiDir_Right, 0.26f, &rightNode, &centerNode);
            ImGui::DockBuilderSplitNode(
                rightNode, ImGuiDir_Up, 0.55f, &rightTop, &rightBottom);

            ImGui::DockBuilderDockWindow(
                TEXT::LocalizeWindow(
                    "ui.window.gameView", "TerrainSurface/GameView").c_str(),
                centerNode);
            ImGui::DockBuilderDockWindow(
                (TEXT::Localize("ui.terrain.tools") +
                    "###TerrainSurface/Tools").c_str(),
                leftNode);
            ImGui::DockBuilderDockWindow(
                (TEXT::Localize("ui.terrain.presets") +
                    "###TerrainSurface/Presets").c_str(),
                rightTop);
            ImGui::DockBuilderDockWindow(
                (TEXT::Localize("ui.terrain.details") +
                    "###TerrainSurface/Details").c_str(),
                rightBottom);
            ImGui::DockBuilderFinish(dockspaceId);
        }
        ImGui::DockSpaceOverViewport(dockspaceId, viewport);
#else
        (void)resetDefaultDockLayout;
#endif
    }

    TerrainSurfaceWorkspaceResult TerrainSurfaceWorkspaceController::Draw(
        DocumentSceneBase& scene,
        EditorContext& context) {
#if defined(HIKARI_WITH_EDITOR)
        TerrainSurfaceWorkspaceResult result{};
        TERRAIN::SurfaceLayerRegistry::Get().RegisterBuiltins();
        const std::vector<TERRAIN::SurfacePreset>& layers =
            TERRAIN::SurfaceLayerRegistry::Get().Layers();

        GameObject* selected = context.selection.selectedObject;
        TerrainSurfaceComponent* terrain =
            selected != nullptr
                ? selected->GetComponent<TerrainSurfaceComponent>()
                : nullptr;
        SceneComponentData* documentComponent = nullptr;
        if (selected != nullptr && terrain != nullptr) {
            result.objectId = selected->GetDocumentId();
            SceneObjectData* documentObject = FindSceneObjectData(
                scene.GetSceneDocument(), result.objectId);
            if (documentObject != nullptr) {
                for (SceneComponentData& component :
                    documentObject->components) {
                    if (component.type == "TerrainSurfaceComponent") {
                        documentComponent = &component;
                        result.componentId = component.id;
                        break;
                    }
                }
            }
        }
        const bool canAuthor = terrain != nullptr &&
            documentComponent != nullptr &&
            result.objectId.value != 0u &&
            result.componentId.value != 0u;
        if (canAuthor) {
            result.beforePatch = ExtractTerrainSettingsPatch(
                documentComponent->properties);
        }
        bool runtimeComponentChanged = false;

        // 左: ブラシツール
        if (ImGui::Begin((TEXT::Localize("ui.terrain.tools") +
                "###TerrainSurface/Tools").c_str())) {
            ImGui::TextUnformatted(
                TEXT::Localize("ui.terrain.tool").c_str());
            const float toolWidth =
                (ImGui::GetContentRegionAvail().x -
                    ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            static const char* kToolIds[] = {
                "R##t", "L##t", "S##t", "F##t", "M##t", "P##t"
            };
            static const char* kToolNames[] = {
                "Raise", "Lower", "Smooth", "Flatten", "Mound", "Pit"
            };
            for (std::size_t i = 0; i < std::size(kSculptTools); ++i) {
                const EditorTerrainToolMode mode = kSculptTools[i];
                const bool isActive = context.terrainTool.active &&
                    context.terrainTool.mode == mode &&
                    context.terrainTool.paint ==
                        EditorTerrainPaintMode::None;
                const ImVec4 tint =
                    (mode == EditorTerrainToolMode::Raise ||
                        mode == EditorTerrainToolMode::Mound)
                        ? ImVec4(0.25f, 0.55f, 0.95f, 1.0f)
                        : (mode == EditorTerrainToolMode::Lower ||
                            mode == EditorTerrainToolMode::Pit)
                            ? ImVec4(0.95f, 0.45f, 0.25f, 1.0f)
                            : ImVec4(0.35f, 0.75f, 0.45f, 1.0f);
                if (ToolButton(
                        mode, kToolIds[i], kToolNames[i],
                        isActive, tint, toolWidth)) {
                    context.terrainTool.mode = mode;
                    context.terrainTool.paint =
                        EditorTerrainPaintMode::None;
                    context.terrainTool.active = true;
                }
                if ((i % 2u) == 0u) {
                    ImGui::SameLine();
                }
            }

            ImGui::SeparatorText(
                TEXT::Localize("ui.terrain.paintPreset").c_str());
            if (ImGui::RadioButton(
                    TEXT::Localize("ui.terrain.none").c_str(),
                    context.terrainTool.paint ==
                        EditorTerrainPaintMode::None)) {
                context.terrainTool.paint = EditorTerrainPaintMode::None;
            }
            ImGui::SameLine();
            if (ImGui::RadioButton(
                    TEXT::Localize("ui.terrain.erase").c_str(),
                    context.terrainTool.paint ==
                        EditorTerrainPaintMode::Erase)) {
                context.terrainTool.paint = EditorTerrainPaintMode::Erase;
                context.terrainTool.active = true;
            }
            // preset 名は id そのものではなくローカライズ表示にする。
            static const struct {
                const char* id;
                EditorTerrainPaintMode mode;
                const char* labelKey;
            } kPaintPresets[] = {
                { "sand", EditorTerrainPaintMode::Sand, "ui.terrain.sand" },
                { "snow", EditorTerrainPaintMode::Snow, "ui.terrain.snow" },
                { "dirt", EditorTerrainPaintMode::Dirt, "ui.terrain.dirt" },
            };
            int paintCol = 0;
            for (const auto& entry : kPaintPresets) {
                if (TERRAIN::SurfaceLayerRegistry::Get().Find(entry.id) ==
                        nullptr) {
                    continue;
                }
                if (paintCol++ > 0) {
                    ImGui::SameLine();
                }
                if (ImGui::RadioButton(
                        TEXT::Localize(entry.labelKey).c_str(),
                        context.terrainTool.paint == entry.mode)) {
                    context.terrainTool.paint = entry.mode;
                    context.terrainTool.active = true;
                }
            }

            ImGui::SeparatorText(
                TEXT::Localize("ui.terrain.brush").c_str());
            if (terrain != nullptr) {
                TERRAIN::TerrainBrush brush = terrain->GetBrush();
                bool brushChanged = false;
                brushChanged |= ImGui::SliderFloat(
                    TEXT::Localize("ui.terrain.radius").c_str(),
                    &brush.radius, 0.1f, 20.0f, "%.2f");
                brushChanged |= ImGui::SliderFloat(
                    TEXT::Localize("ui.terrain.strength").c_str(),
                    &brush.strength, 0.0f, 1.0f, "%.3f");
                brushChanged |= ImGui::SliderFloat(
                    TEXT::Localize("ui.terrain.hardness").c_str(),
                    &brush.hardness, 0.0f, 1.0f, "%.3f");
                brushChanged |= ImGui::SliderFloat(
                    TEXT::Localize("ui.terrain.falloff").c_str(),
                    &brush.falloff, 0.0f, 1.0f, "%.3f");
                if (brushChanged) {
                    if (canAuthor) {
                        terrain->SetBrush(brush);
                        runtimeComponentChanged = true;
                    }
                }
            } else {
                ImGui::TextDisabled(
                    TEXT::Localize("ui.terrain.selectTerrain").c_str());
            }
            ImGui::Spacing();
            ImGui::TextWrapped(
                "%s",
                TEXT::Localize("ui.terrain.brushHint").c_str());
        }
        ImGui::End();

        // 右上: オブジェクト + プリセット
        if (ImGui::Begin((TEXT::Localize("ui.terrain.presets") +
                "###TerrainSurface/Presets").c_str())) {
            ImGui::SeparatorText(
                TEXT::Localize("ui.terrain.objects").c_str());
            scene.GetWorld().ForEachObjectWith<TerrainSurfaceComponent>(
                [&](GameObject& object, TerrainSurfaceComponent&) {
                    const bool isSelected =
                        context.selection.selectedObject == &object;
                    if (ImGui::Selectable(
                            object.GetName().c_str(),
                            isSelected)) {
                        context.selection.selectedObject = &object;
                    }
                });

            ImGui::SeparatorText(
                TEXT::Localize("ui.terrain.surfacePresets").c_str());
            ImGui::TextWrapped(
                "%s",
                TEXT::Localize("ui.terrain.presetHint").c_str());
            if (terrain != nullptr) {
                const std::string current = terrain->GetSurface().presetId;
                for (const TERRAIN::SurfacePreset& layer : layers) {
                    const bool isCurrent = current == layer.id;
                    if (ImGui::Selectable(
                            layer.id.c_str(),
                            isCurrent,
                            ImGuiSelectableFlags_AllowDoubleClick)) {
                        if (canAuthor) {
                            terrain->ApplySolidPreset(layer.id);
                            runtimeComponentChanged = true;
                        }
                    }
                }
            } else {
                ImGui::TextDisabled(
                    TEXT::Localize("ui.terrain.selectTerrain").c_str());
            }
        }
        ImGui::End();

        // 右下: 詳細
        if (ImGui::Begin((TEXT::Localize("ui.terrain.details") +
                "###TerrainSurface/Details").c_str())) {
            if (terrain != nullptr) {
                TERRAIN::ShorelineParams shoreline =
                    terrain->GetSurface().shoreline;
                TERRAIN::DeformParams deform =
                    terrain->GetSurface().deform;
                bool shorelineChanged = false;
                bool deformChanged = false;
                ImGui::SeparatorText(
                    TEXT::Localize("ui.terrain.shoreline").c_str());
                shorelineChanged |= ImGui::SliderFloat(
                    TEXT::Localize("ui.terrain.edgeFalloff").c_str(),
                    &shoreline.edgeFalloff, 0.0f, 40.0f, "%.2f");
                shorelineChanged |= ImGui::SliderFloat(
                    TEXT::Localize("ui.terrain.edgeDepth").c_str(),
                    &shoreline.edgeDepth, 0.0f, 20.0f, "%.2f");
                shorelineChanged |= ImGui::SliderFloat(
                    TEXT::Localize("ui.terrain.wetness").c_str(),
                    &shoreline.wetness, 0.0f, 1.0f, "%.2f");

                ImGui::SeparatorText(
                    TEXT::Localize("ui.terrain.deform").c_str());
                deformChanged |= ImGui::SliderFloat(
                    TEXT::Localize("ui.terrain.maxDepth").c_str(),
                    &deform.maxDepth, 0.0f, 2.0f, "%.3f");
                deformChanged |= ImGui::SliderFloat(
                    TEXT::Localize("ui.terrain.stampRadius").c_str(),
                    &deform.stampRadius, 0.05f, 2.0f, "%.3f");
                deformChanged |= ImGui::SliderFloat(
                    TEXT::Localize("ui.terrain.rimStrength").c_str(),
                    &deform.rimStrength, 0.0f, 1.0f, "%.3f");
                int responseScale = static_cast<int>(
                    deform.responseResolutionScale);
                if (ImGui::SliderInt(
                        TEXT::Localize(
                            "ui.terrain.responseResolutionScale").c_str(),
                        &responseScale, 1, 4)) {
                    deform.responseResolutionScale =
                        static_cast<uint32_t>(responseScale);
                    deformChanged = true;
                }
                deformChanged |= ImGui::SliderFloat(
                    TEXT::Localize("ui.terrain.restDepthScale").c_str(),
                    &deform.restDepthScale, 0.0f, 1.0f, "%.3f");
                deformChanged |= ImGui::SliderFloat(
                    TEXT::Localize("ui.terrain.minimumDepth").c_str(),
                    &deform.minimumDepth, 0.0f, 0.5f, "%.3f m");
                deformChanged |= ImGui::SliderFloat(
                    TEXT::Localize("ui.terrain.settleStartRatio").c_str(),
                    &deform.settleStartRatio, 0.0f, 1.0f, "%.3f");
                deformChanged |= ImGui::SliderFloat(
                    TEXT::Localize("ui.terrain.settleSeconds").c_str(),
                    &deform.settleSeconds, 0.0f, 1.0f, "%.3f s");
                deformChanged |= ImGui::SliderFloat(
                    TEXT::Localize("ui.terrain.impactDepthScale").c_str(),
                    &deform.impactDepthScale, 0.0f, 0.2f, "%.3f");
                deformChanged |= ImGui::SliderFloat(
                    TEXT::Localize("ui.terrain.rimWidthScale").c_str(),
                    &deform.rimWidthScale, 0.05f, 1.0f, "%.3f");
                deformChanged |= ImGui::SliderFloat(
                    TEXT::Localize("ui.terrain.refill").c_str(),
                    &deform.refillPerSecond, 0.0f, 1.0f, "%.4f");
                deformChanged |= ImGui::SliderFloat(
                    TEXT::Localize("ui.terrain.diffusion").c_str(),
                    &deform.diffusionPerSecond, 0.0f, 8.0f, "%.3f");
                deformChanged |= ImGui::SliderFloat(
                    TEXT::Localize("ui.terrain.plasticity").c_str(),
                    &deform.plasticity, 0.0f, 1.0f, "%.3f");
                deformChanged |= ImGui::SliderFloat(
                    TEXT::Localize("ui.terrain.plasticRefill").c_str(),
                    &deform.plasticRefillPerSecond,
                    0.0f, 0.1f, "%.4f");
                deformChanged |= ImGui::SliderFloat(
                    TEXT::Localize("ui.terrain.recoveryDelay").c_str(),
                    &deform.recoveryDelaySeconds,
                    0.0f, 30.0f, "%.2f s");

                if (shorelineChanged) {
                    if (canAuthor) {
                        terrain->SetShorelineParams(shoreline);
                        runtimeComponentChanged = true;
                    }
                }
                if (deformChanged) {
                    if (canAuthor) {
                        terrain->SetDeformParams(deform);
                        runtimeComponentChanged = true;
                    }
                }
            } else {
                ImGui::TextDisabled(
                    TEXT::Localize("ui.terrain.selectTerrain").c_str());
            }
        }
        ImGui::End();
        result.editActive = canAuthor && ImGui::IsAnyItemActive();
        if (runtimeComponentChanged && canAuthor) {
            terrain->SerializeSettings(result.afterPatch);
            result.componentChanged =
                result.afterPatch != result.beforePatch;
            if (result.componentChanged) {
                ApplyTerrainSettingsPatch(
                    documentComponent->properties,
                    result.afterPatch);
            }
        }
        return result;
#else
        (void)scene;
        (void)context;
        return {};
#endif
    }

} // namespace HIKARI::EDITOR
