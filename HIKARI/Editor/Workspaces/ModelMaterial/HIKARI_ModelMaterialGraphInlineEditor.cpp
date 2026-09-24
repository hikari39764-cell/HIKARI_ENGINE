#include "Core/Text/HIKARI_Localization.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <string_view>
#include <utility>
#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/Material/Authoring/HIKARI_MaterialTemplateFactory.h"
#include "Editor/Widgets/HIKARI_AssetFieldWidget.h"
#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif
namespace HIKARI::EDITOR {
bool ModelMaterialWorkspaceController::DrawMaterialGraphNodeInlineControls(DocumentSceneBase& scene,
                                                                           MaterialAssetData& source,
                                                                           MaterialGraphData& graph,
                                                                           MaterialGraphNodeData& node, float x,
                                                                           float y, float width) {
#if defined(HIKARI_WITH_EDITOR)
    const ImVec2 restore = ImGui::GetCursorScreenPos();
    bool capturesPointer = false;
    ImGui::PushID(node.id.c_str());
    ImGui::SetCursorScreenPos({x, y});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6.0f, 4.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing, ImVec2(5.0f, 4.0f));
    ImGui::PushClipRect({x, y}, {x + width, y + MATERIAL_GRAPH_UI::NodeInlineHeight(node.kind)}, true);
    ImGui::BeginGroup();
    ImGui::PushItemWidth(width);
    if (node.kind == MaterialNodeKind::FunctionCall && !node.assetReference.IsValid()) {
        ImGui::TextColored(ImVec4{1.0f, 0.42f, 0.35f, 1.0f}, TEXT::Localize("editor.missingFunction").c_str());
    }
    const auto trackItem = [&capturesPointer]() {
        capturesPointer = capturesPointer || ImGui::IsItemHovered() || ImGui::IsItemActive();
    };
    const auto drawParameter = [&](std::string_view id, const char* label, std::string_view pin = {}) {
        capturesPointer |= DrawGraphParameter(source, graph, &node, id, label, width, pin);
    };
    const auto drawTexture = [&, this](std::string_view resourceId, const char* label, std::string_view inputPin = {}) {
        MaterialGraphResourceData* resource = FindMaterialGraphResource(graph, resourceId);
        if (resource == nullptr) {
            ImGui::TextDisabled(TEXT::Localize("editor.ui.s.missing.f1ed86").c_str(), label);
            return;
        }
        const bool controlledByDefault =
            inputPin.empty() || MATERIAL_GRAPH_UI::InputUsesResourceDefault(graph, node, inputPin, resourceId);
        ImGui::PushID(resource->id.c_str());
        ImGui::TextDisabled("%s", label);
        if (!controlledByDefault) {
            ImGui::SameLine();
            ImGui::TextDisabled(TEXT::Localize("editor.ui.linked.a089f6").c_str());
            ImGui::PopID();
            return;
        }
        std::string textureGuid = resource->assetGuid.value;
        AssetFieldOptions options{};
        options.label = label;
        options.requiredType = AssetType::Texture;
        options.drawLabel = false;
        options.width = width;
        if (DrawAssetField(&scene.GetAssetDatabase(), options, textureGuid)) {
            MaterialAssetData before = source;
            resource->assetGuid = AssetGuid{textureGuid};
            resource->enabled = resource->assetGuid.IsValid();
            authoringDocument_.CommitMaterial(std::move(before), "Bind " + resource->displayName);
        }
        trackItem();
        ImGui::PopID();
    };
    capturesPointer = DrawMaterialGraphVariableInlineControl(scene, source, node) || capturesPointer;
    if (node.kind == MaterialNodeKind::Parameter) {
        MaterialGraphParameterData* parameter = FindMaterialGraphParameter(graph, node.referenceId);
        if (parameter == nullptr) {
            if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.create.parameter.95275c", "+ Create Parameter").c_str(),
                              {width, 0.0f})) {
                MaterialAssetData before = source;
                MaterialGraphParameterData created{};
                created.id = "Parameter_" + std::to_string(authoringIdCounter_++);
                created.displayName = "Parameter";
                created.defaultValue.type = node.valueType;
                created.exposed = true;
                graph.parameters.push_back(std::move(created));
                node.referenceId = graph.parameters.back().id;
                authoringDocument_.CommitMaterial(std::move(before), "Create material parameter");
            }
            trackItem();
        } else {
            ImGui::TextDisabled("%s", parameter->displayName.empty() ? parameter->id.c_str()
                                                                     : parameter->displayName.c_str());
            MaterialAssetData typeBefore = source;
            if (MATERIAL_GRAPH_UI::DrawMaterialNodeTypeControl(node, parameter)) {
                authoringDocument_.CommitMaterial(std::move(typeBefore), "Change parameter type");
            }
            MaterialAssetData before = source;
            const bool changed = MATERIAL_GRAPH_UI::DrawCompactMaterialValue(
                parameter->defaultValue, MATERIAL_GRAPH_UI::LooksLikeColor(parameter->displayName, parameter->id));
            trackItem();
            if (parameter->defaultValue.type == MaterialValueType::Bool) {
                if (changed) {
                    authoringDocument_.CommitMaterial(std::move(before), "Edit material parameter");
                }
            } else {
                TrackMaterialItem(std::move(before), "Edit material parameter", changed);
            }
        }
    } else if (node.kind == MaterialNodeKind::Constant) {
        MaterialAssetData typeBefore = source;
        if (MATERIAL_GRAPH_UI::DrawMaterialNodeTypeControl(node, nullptr)) {
            authoringDocument_.CommitMaterial(std::move(typeBefore), "Change constant type");
        }
        MaterialAssetData before = source;
        const bool changed = MATERIAL_GRAPH_UI::DrawCompactMaterialValue(
            node.value, MATERIAL_GRAPH_UI::LooksLikeColor(node.displayName, node.id));
        trackItem();
        if (node.value.type == MaterialValueType::Bool) {
            if (changed) {
                authoringDocument_.CommitMaterial(std::move(before), "Edit material constant");
            }
        } else {
            TrackMaterialItem(std::move(before), "Edit material constant", changed);
        }
    } else if (node.kind == MaterialNodeKind::TextureSample) {
        MaterialGraphResourceData* resource = FindMaterialGraphResource(graph, node.referenceId);
        if (resource == nullptr) {
            if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.texture.resource.93d8ae", "+ Texture Resource").c_str(),
                              {width, 0.0f})) {
                MaterialAssetData before = source;
                MaterialGraphResourceData created{};
                created.id = "Texture_" + std::to_string(authoringIdCounter_++);
                created.displayName = "Texture";
                created.type = MaterialResourceType::Texture2D;
                graph.resources.push_back(std::move(created));
                node.referenceId = graph.resources.back().id;
                authoringDocument_.CommitMaterial(std::move(before), "Create texture sample resource");
            }
            trackItem();
        } else {
            ImGui::TextDisabled("%s",
                                resource->displayName.empty() ? resource->id.c_str() : resource->displayName.c_str());
            std::string textureGuid = resource->assetGuid.value;
            AssetFieldOptions options{};
            options.label = "Node Texture";
            options.requiredType = AssetType::Texture;
            options.drawLabel = false;
            options.width = width;
            if (DrawAssetField(&scene.GetAssetDatabase(), options, textureGuid)) {
                MaterialAssetData before = source;
                resource->assetGuid = AssetGuid{textureGuid};
                resource->enabled = resource->assetGuid.IsValid();
                authoringDocument_.CommitMaterial(std::move(before), "Bind texture sample asset");
            }
            trackItem();
            constexpr MaterialResourceSemantic kTextureSemantics[]{
                MaterialResourceSemantic::Custom,      MaterialResourceSemantic::BaseColor,
                MaterialResourceSemantic::Normal,      MaterialResourceSemantic::MetallicRoughness,
                MaterialResourceSemantic::Occlusion,   MaterialResourceSemantic::Emissive,
                MaterialResourceSemantic::Specular,    MaterialResourceSemantic::SpecularColor,
                MaterialResourceSemantic::Mask,        MaterialResourceSemantic::MatCap,
                MaterialResourceSemantic::FaceSdf,     MaterialResourceSemantic::HairShadow,
                MaterialResourceSemantic::HairControl, MaterialResourceSemantic::EyeControl,
                MaterialResourceSemantic::Thickness,   MaterialResourceSemantic::Fabric,
            };
            int semanticIndex = 0;
            for (int index = 0; index < static_cast<int>(std::size(kTextureSemantics)); ++index) {
                if (kTextureSemantics[index] == resource->semantic) {
                    semanticIndex = index;
                    break;
                }
            }
            const auto semanticLabel = [](void* data, int index, const char** text) {
                const auto* semantics = static_cast<const MaterialResourceSemantic*>(data);
                *text = ToString(semantics[index]).data();
                return true;
            };
            ImGui::SetNextItemWidth(width);
            if (ImGui::Combo("##TextureSemantic", &semanticIndex, semanticLabel,
                             const_cast<MaterialResourceSemantic*>(kTextureSemantics),
                             static_cast<int>(std::size(kTextureSemantics)))) {
                MaterialAssetData before = source;
                resource->semantic = kTextureSemantics[semanticIndex];
                resource->displayName = std::string(ToString(resource->semantic)) + " Texture";
                authoringDocument_.CommitMaterial(std::move(before), "Change texture sample semantic");
            }
            trackItem();
            ImGui::TextDisabled(TEXT::Localize("editor.ui.color.space.s.derived.from.semantic.11955d").c_str(),
                                resource->semantic == MaterialResourceSemantic::BaseColor ||
                                        resource->semantic == MaterialResourceSemantic::Emissive ||
                                        resource->semantic == MaterialResourceSemantic::SpecularColor ||
                                        resource->semantic == MaterialResourceSemantic::MatCap
                                    ? "sRGB"
                                    : "Linear");
            int uvSet = static_cast<int>(resource->texCoord);
            ImGui::SetNextItemWidth(width);
            if (ImGui::Combo("##TextureUvSet", &uvSet, "UV0\0UV1\0")) {
                MaterialAssetData before = source;
                resource->texCoord = static_cast<uint32_t>(uvSet);
                authoringDocument_.CommitMaterial(std::move(before), "Change texture sample UV set");
            }
            trackItem();
        }
    } else if (node.kind == MaterialNodeKind::RampDiffuse || node.kind == MaterialNodeKind::MultiRampDiffuse) {
        capturesPointer = DrawMaterialGraphRampNodeInlineControls(scene, source, graph, node, width) || capturesPointer;
    } else if (node.kind == MaterialNodeKind::PbrSurface) {
        ImGui::SeparatorText(TEXT::Localize("editor.ui.surface.cda05c").c_str());
        drawParameter("parameter.baseColor", "Base Color", "BaseColor");
        drawParameter("parameter.metallic", "Metallic", "Metallic");
        drawParameter("parameter.roughness", "Roughness", "Roughness");
        drawParameter("parameter.specular", "Specular", "Specular");
        drawParameter("parameter.specularColor", "Specular Color", "SpecularColor");
        drawParameter("parameter.normalScale", "Normal Scale");
        drawParameter("parameter.occlusionStrength", "Occlusion", "Occlusion");
        ImGui::SeparatorText(TEXT::Localize("ui.vfx.text.Emission").c_str());
        drawParameter("parameter.emissive", "Emission", "Emissive");
        drawParameter("parameter.emissiveStrength", "Emission Power");
        ImGui::TextDisabled(TEXT::Localize("editor.ui.use.texture.sample.nodes.for.every.image.input.3c4ba8").c_str());
    } else if (node.kind == MaterialNodeKind::SurfaceOutput) {
        ImGui::SeparatorText(TEXT::Localize("editor.ui.mesh.shader.surface.fa0149").c_str());
        int alphaMode = static_cast<int>(source.surface.alphaMode);
        const char* kAlphaModes[]{TEXT::LocalizeCStr("ui.inspector.worldSprite.alphaOpaque"),
                                  TEXT::LocalizeCStr("ui.vfx.uvTarget.Mask"),
                                  TEXT::LocalizeCStr("ui.terrain.rampBlend")};
        ImGui::SetNextItemWidth(width);
        if (ImGui::Combo("##SurfaceAlphaMode", &alphaMode, kAlphaModes, 3)) {
            MaterialAssetData before = source;
            source.surface.alphaMode = static_cast<MaterialAlphaMode>(alphaMode);
            authoringDocument_.CommitMaterial(std::move(before), "Change surface alpha mode");
        }
        trackItem();
        if (source.surface.alphaMode == MaterialAlphaMode::Mask) {
            ImGui::TextUnformatted(TEXT::Localize("ui.inspector.worldSprite.alphaCutoff").c_str());
            ImGui::SetNextItemWidth(width);
            MaterialAssetData before = source;
            const bool changed =
                ImGui::SliderFloat("##SurfaceAlphaCutoff", &source.surface.alphaCutoff, 0.0f, 1.0f, "%.3f");
            trackItem();
            TrackMaterialItem(std::move(before), "Edit surface alpha cutoff", changed);
        } else if (source.surface.alphaMode == MaterialAlphaMode::Blend) {
            ImGui::TextUnformatted(TEXT::Localize("editor.ui.source.color.6d2042").c_str());
            int representation = static_cast<int>(source.surface.alphaRepresentation);
            const char* kRepresentations[]{TEXT::LocalizeCStr("editor.ui.straight.2501c6"),
                                           TEXT::LocalizeCStr("editor.ui.premultiplied.f64bae")};
            ImGui::SetNextItemWidth(width);
            if (ImGui::Combo("##SurfaceAlphaRepresentation", &representation, kRepresentations, 2)) {
                MaterialAssetData before = source;
                source.surface.alphaRepresentation = static_cast<MaterialAlphaRepresentation>(representation);
                authoringDocument_.CommitMaterial(std::move(before), "Change alpha representation");
            }
            trackItem();
            ImGui::TextWrapped(source.surface.alphaRepresentation == MaterialAlphaRepresentation::Premultiplied
                                   ? "Advanced: source pixels already contain RGB *= Alpha. "
                                     "Ordinary PNG/FBX eye overlays are usually not premultiplied."
                                   : "Recommended for ordinary PNG/FBX textures and eye overlays. "
                                     "The renderer premultiplies the lit result at output.");
        }
        MaterialAssetData before = source;
        if (ImGui::Checkbox(TEXT::LocalizeWindow("ui.assetInspector.doubleSided", "Double Sided").c_str(),
                            &source.surface.doubleSided)) {
            authoringDocument_.CommitMaterial(std::move(before), "Toggle double-sided surface");
        }
        trackItem();
    } else if (node.kind == MaterialNodeKind::SpecularBand) {
        drawParameter("parameter.specularBandWidth", "Width", "Width");
        drawParameter("parameter.specularBandSoftness", "Softness", "Softness");
        drawParameter("parameter.specularIntensity", "Intensity", "Intensity");
        drawParameter("parameter.specularPower", "Power", "Power");
        drawParameter("parameter.specularTint", "Tint", "Tint");
    } else if (node.kind == MaterialNodeKind::FresnelRim) {
        drawParameter("parameter.rimColor", "Color", "Color");
        drawParameter("parameter.rimPower", "Power", "Power");
        drawParameter("parameter.rimWidth", "Width", "Width");
        drawParameter("parameter.rimIntensity", "Intensity", "Intensity");
        drawParameter("parameter.rimLightAlignment", "Light Align", "LightAlignment");
    } else if (node.kind == MaterialNodeKind::FaceSdf) {
        capturesPointer = DrawFaceRampNodeControl(scene, source, graph, width) || capturesPointer;
        ImGui::TextDisabled(
            TEXT::Localize("editor.ui.required.texture.sample.r.sdf.linear.mirrored.sdf.a08605").c_str());
        MaterialGraphParameterData* faceRight = FindMaterialGraphParameter(graph, "parameter.faceRight");
        MaterialGraphParameterData* faceForward = FindMaterialGraphParameter(graph, "parameter.faceForward");
        if (faceRight != nullptr && faceForward != nullptr) {
            const MATH::Vec4 current = faceForward->defaultValue.numeric;
            int forwardAxis =
                std::abs(current.z) >= std::abs(current.x) ? (current.z >= 0.0f ? 0 : 1) : (current.x >= 0.0f ? 2 : 3);
            constexpr const char* kForwardAxes[]{"+Z", "-Z", "+X", "-X"};
            ImGui::SetNextItemWidth(width);
            if (ImGui::Combo("##FaceForwardAxis", &forwardAxis, kForwardAxes, 4)) {
                MaterialAssetData before = source;
                constexpr MATH::Vec4 kForward[]{
                    {0.0f, 0.0f, 1.0f, 0.0f},
                    {0.0f, 0.0f, -1.0f, 0.0f},
                    {1.0f, 0.0f, 0.0f, 0.0f},
                    {-1.0f, 0.0f, 0.0f, 0.0f},
                };
                constexpr MATH::Vec4 kRight[]{
                    {1.0f, 0.0f, 0.0f, 0.0f},
                    {-1.0f, 0.0f, 0.0f, 0.0f},
                    {0.0f, 0.0f, -1.0f, 0.0f},
                    {0.0f, 0.0f, 1.0f, 0.0f},
                };
                faceForward->defaultValue.numeric = kForward[forwardAxis];
                faceRight->defaultValue.numeric = kRight[forwardAxis];
                authoringDocument_.CommitMaterial(std::move(before), "Change Face SDF head forward axis");
            }
            trackItem();
        }
        drawParameter("parameter.faceShadowSoftness", "Softness", "Softness");
        drawParameter("parameter.toonDirectGain", "Direct Gain", "DirectGain");
        drawParameter("parameter.faceShadowOffset", "Offset");
    } else if (node.kind == MaterialNodeKind::AnisotropicHair) {
        ImGui::SeparatorText(TEXT::Localize("editor.ui.strand.direction.72e7c6").c_str());
        ImGui::TextDisabled(TEXT::Localize("editor.ui.optional.haircontrol.texture.sample.b.mask.c999dd").c_str());
        ImGui::TextDisabled(
            TEXT::Localize("editor.ui.no.map.uses.mesh.tangent.rg.flow.and.a.shift.specialize.it.945f54").c_str());
        ImGui::SeparatorText(TEXT::Localize("editor.ui.dual.kajiya.kay.lobes.604a62").c_str());
        drawParameter("parameter.hairShift", "Shift", "Shift");
        drawParameter("parameter.hairWidth", "Width", "Width");
        drawParameter("parameter.hairSecondaryShift", "Second Shift");
        drawParameter("parameter.hairIntensity", "Intensity");
        drawParameter("parameter.hairPrimaryPower", "Primary Sharpness", "PrimaryPower");
        drawParameter("parameter.hairSecondaryPower", "Second Sharpness", "SecondaryPower");
        drawParameter("parameter.hairSecondaryIntensity", "Second Intensity", "SecondaryIntensity");
        drawParameter("parameter.hairFlowStrength", "Flow Strength", "FlowStrength");
        const char* hairTint = FindMaterialGraphParameter(graph, "parameter.hairHighlightTint")
                                   ? "parameter.hairHighlightTint"
                                   : "parameter.specularTint";
        drawParameter(hairTint, "Tint");
    } else if (node.kind == MaterialNodeKind::MatCap) {
        ImGui::TextDisabled(TEXT::Localize("editor.ui.required.matcap.texture.sample.color.image.f694e6").c_str());
        drawParameter("parameter.matCapTint", "Tint");
        drawParameter("parameter.matCapIntensity", "Intensity");
        drawParameter("parameter.matCapViewCorrection", "View Correction");
        drawParameter("parameter.matCapSphericalNormalBlend", "Eye Sphere Normal");
    } else if (node.kind == MaterialNodeKind::EyeLighting) {
        capturesPointer = DrawEyeLightingNodeInlineControls(source, graph, width) || capturesPointer;
    } else if (node.kind == MaterialNodeKind::CorneaHighlight) {
        capturesPointer = DrawCorneaHighlightNodeInlineControls(source, graph, width) || capturesPointer;
    } else if (node.kind == MaterialNodeKind::SkinLighting) {
        ImGui::TextDisabled(TEXT::Localize("editor.ui.optional.thickness.texture.sample.r.mask.fad5ae").c_str());
        drawParameter("parameter.skinWrap", "Light Wrap", "Wrap");
        drawParameter("parameter.skinSubsurface", "Subsurface", "Subsurface");
        drawParameter("parameter.skinTint", "Scatter Tint", "Tint");
    } else if (node.kind == MaterialNodeKind::FabricSheen) {
        ImGui::TextDisabled(TEXT::Localize("editor.ui.optional.fabric.texture.sample.b.mask.441d08").c_str());
        drawParameter("parameter.fabricSheen", "Sheen", "Sheen");
        drawParameter("parameter.fiberRoughness", "Roughness", "FiberRoughness");
        drawParameter("parameter.fabricSheenPower", "Power", "Power");
        drawParameter("parameter.fabricSheenIntensity", "Intensity", "Intensity");
    } else if (node.kind == MaterialNodeKind::HybridLighting) {
        ImGui::TextDisabled(TEXT::Localize("editor.ui.0.npr.endpoint.1.pbr.endpoint.ef4a45").c_str());
        drawParameter("parameter.directDiffusePbrBlend", "Diffuse PBR", "DirectDiffuseBlend");
        drawParameter("parameter.directSpecularPbrBlend", "Specular PBR", "DirectSpecularBlend");
        drawParameter("parameter.indirectPbrBlend", "Indirect PBR", "IndirectBlend");
    } else if (node.kind == MaterialNodeKind::Outline) {
        MaterialGraphParameterData* mode = FindMaterialGraphParameter(graph, "parameter.outlineMode");
        if (mode != nullptr) {
            int current = mode->defaultValue.numeric.x < 0.5f ? 0 : mode->defaultValue.numeric.x < 1.5f ? 1 : 2;
            const char* kModes[]{TEXT::LocalizeCStr("ui.vfx.layer.geometry"),
                                 TEXT::LocalizeCStr("editor.ui.screen.5cc847"),
                                 TEXT::LocalizeCStr("editor.ui.both.1f4698")};
            ImGui::SetNextItemWidth(width);
            if (ImGui::Combo("##OutlineMode", &current, kModes, 3)) {
                MaterialAssetData before = source;
                mode->defaultValue.numeric.x = static_cast<float>(current);
                authoringDocument_.CommitMaterial(std::move(before), "Change outline mode");
            }
            trackItem();
            drawParameter("parameter.outlineColor", "Color", "Color");
            drawParameter("parameter.outlineThickness", "Thickness", "Thickness");
            DrawOutlineThicknessPresets(source, graph);
            if (current != 1) {
                ImGui::TextDisabled(
                    MESHRENDERER::HasGeometryOutlineFeatureBackend()
                        ? TEXT::LocalizeCStr("editor.ui.geometry.backend.ready.closed.meshes.work.best.7ad1e3")
                        : TEXT::LocalizeCStr(
                              "editor.ui.geometry.backend.unavailable.check.renderer.diagnostics.189c4d"));
            }
            if (current != 0) {
                ImGui::TextDisabled(
                    TEXT::Localize("editor.ui.screen.backend.per.material.color.and.pixel.thickness.supports.h.cdf407")
                        .c_str());
            }
        }
    } else if (node.kind == MaterialNodeKind::HairShadow) {
        ImGui::TextDisabled(TEXT::Localize("editor.ui.required.hairshadow.texture.sample.r.shadowmask.1deaf5").c_str());
        ImGui::TextDisabled(TEXT::Localize("editor.ui.linear.r.1.lit.visibility.0.hair.shadow.31c5b4").c_str());
        drawParameter("parameter.hairShadowStrength", "Strength", "Strength");
    }
    ImGui::PopItemWidth();
    ImGui::EndGroup();
    ImGui::PopClipRect();
    ImGui::PopStyleVar(2);
    capturesPointer = capturesPointer || ImGui::IsItemHovered();
    ImGui::PopID();
    ImGui::SetCursorScreenPos(restore);
    return capturesPointer;
#else
    (void)scene;
    (void)source;
    (void)graph;
    (void)node;
    (void)x;
    (void)y;
    (void)width;
    return false;
#endif
}
} // namespace HIKARI::EDITOR
