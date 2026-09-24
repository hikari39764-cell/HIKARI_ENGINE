#include "Core/Text/HIKARI_Localization.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"

#include <algorithm>
#include <string>

#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Debug/HIKARI_MeshWireDebugRenderer.h"
#include "Render3D/Render/HIKARI_ModelRenderer.h"

#if defined(HIKARI_ENABLE_IMGUI)
#include "imgui.h"
#endif

namespace HIKARI {

namespace {
#if defined(HIKARI_ENABLE_IMGUI)
const char* ToAlphaModeText(AlphaMode mode) {
    switch (mode) {
    case AlphaMode::Opaque: return "Opaque";
    case AlphaMode::Mask: return "Mask";
    case AlphaMode::Blend: return "Blend";
    default: return "Unknown";
    }
}

const char* ResolveTexturePathDebug(const ModelAsset& asset, const TextureSlot& slot) {
    if (slot.textureIndex < 0 || slot.textureIndex >= static_cast<int>(asset.textures.size())) {
        return "<none>";
    }
    const std::string& path = asset.textures[static_cast<size_t>(slot.textureIndex)].sourcePath;
    return path.empty() ? "<empty>" : path.c_str();
}

std::string ShortGuid(const std::string& guid) { return guid.size() <= 8u ? guid : guid.substr(0u, 8u); }

const char* ResolveTextureResolvedPathDebug(const ModelAsset& asset, const TextureSlot& slot) {
    if (slot.textureIndex < 0 || slot.textureIndex >= static_cast<int>(asset.textures.size())) {
        return "<none>";
    }
    const TextureAsset3D& texture = asset.textures[static_cast<size_t>(slot.textureIndex)];
    const std::string& path = texture.resolvedPath.empty() ? texture.sourcePath : texture.resolvedPath;
    return path.empty() ? "<empty>" : path.c_str();
}

const char* ToStateText(ModelAsset::State state) {
    switch (state) {
    case ModelAsset::State::Unloaded: return "Unloaded";
    case ModelAsset::State::Loaded: return "Loaded";
    case ModelAsset::State::Failed: return "Failed";
    default: return "Unknown";
    }
}
#endif
} // namespace
void ModelComponent::RenderImGui() {
#if defined(HIKARI_ENABLE_IMGUI)
    const bool oldVisible = visible_;
    const bool oldShowSkeletonDebug = showSkeletonDebug_;
    const bool oldSkeletonDebugXRay = skeletonDebugXRay_;
    const bool oldCastShadow = castShadow_;
    const bool oldReceiveShadow = receiveShadow_;
    const bool oldRenderStatic = renderStatic_;
    const ModelRenderDebugMode oldDebugRenderMode = debugRenderMode_;
    const uint32_t oldWireColor = wireColor_;
    const uint32_t oldMaxWireLines = maxWireLines_;
    const bool oldWirePerPrimitiveColor = wirePerPrimitiveColor_;
    const uint32_t oldPostGroupMask = postGroupMask_;

    const auto notifyIfRenderStateChanged = [&]() {
        if (oldVisible != visible_ || oldShowSkeletonDebug != showSkeletonDebug_ ||
            oldSkeletonDebugXRay != skeletonDebugXRay_ || oldCastShadow != castShadow_ ||
            oldReceiveShadow != receiveShadow_ || oldRenderStatic != renderStatic_ ||
            oldDebugRenderMode != debugRenderMode_ || oldWireColor != wireColor_ || oldMaxWireLines != maxWireLines_ ||
            oldWirePerPrimitiveColor != wirePerPrimitiveColor_ || oldPostGroupMask != postGroupMask_) {
            NotifyRenderStateDirty();
        }
    };

    if (ImGui::TreeNodeEx(TEXT::LocalizeWindow("ui.assetInspector.modelSource", "Model Source").c_str())) {
        ImGui::Checkbox(TEXT::LocalizeWindow("ui.inspector.model.visible", "Visible").c_str(), &visible_);
        int postMask = static_cast<int>(postGroupMask_);
        if (ImGui::InputInt(TEXT::LocalizeWindow("ui.inspector.model.postGroupMask", "Post Group Mask").c_str(),
                            &postMask)) {
            postGroupMask_ = static_cast<uint32_t>(postMask < 0 ? 0 : postMask);
        }
        ImGui::Text(TEXT::Localize("editor.ui.asset.id.s.ccaffb").c_str(),
                    assetId_.empty() ? "<none>" : assetId_.c_str());
        if (asset_ == nullptr) {
            ImGui::TextDisabled(
                TEXT::Localize("editor.ui.a.procedural.mesh.component.may.provide.geometry.instead.3e20b4").c_str());
        }
        int debugMode = static_cast<int>(debugRenderMode_);
        const char* debugModes[] = {"Normal", "WireOverlay", "WireOnly", "BoundsOnly"};
        if (ImGui::Combo(TEXT::LocalizeWindow("editor.ui.debug.render.mode.aae35f", "Debug Render Mode").c_str(),
                         &debugMode, debugModes, 4)) {
            debugRenderMode_ = static_cast<ModelRenderDebugMode>((std::clamp)(debugMode, 0, 3));
        }
        ImGui::InputScalar("Wire Color RGBA", ImGuiDataType_U32, &wireColor_);
        int maxWireLines = static_cast<int>(maxWireLines_);
        if (ImGui::DragInt(TEXT::LocalizeWindow("editor.ui.max.wire.lines.354e7c", "Max Wire Lines").c_str(),
                           &maxWireLines, 100.0f, 0, 1000000)) {
            maxWireLines_ = static_cast<uint32_t>((std::max)(0, maxWireLines));
        }
        ImGui::Checkbox(
            TEXT::LocalizeWindow("editor.ui.wire.per.primitive.color.821328", "Wire Per Primitive Color").c_str(),
            &wirePerPrimitiveColor_);
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx(TEXT::LocalizeWindow("editor.ui.material.overrides.3934ba", "Material Overrides").c_str(),
                          ImGuiTreeNodeFlags_DefaultOpen)) {
        const size_t materialCount = asset_ != nullptr ? asset_->materials.size() : 0u;
        if (materialCount == 0u) {
            ImGui::TextDisabled(
                TEXT::Localize("editor.ui.no.imported.material.slots.are.currently.available.1ed8f0").c_str());
        }

        for (size_t materialIndex = 0u; materialIndex < materialCount; ++materialIndex) {
            const MaterialAsset& imported = asset_->materials[materialIndex];
            const ModelMaterialOverrideSlot* authoredOverride = nullptr;
            for (const ModelMaterialOverrideSlot& slot : materialOverrides_) {
                const bool stableMatch = imported.slotId.IsValid() && slot.slotId == imported.slotId;
                const bool legacyMatch = !slot.slotId.IsValid() && slot.slotIndex == materialIndex;
                if (stableMatch || legacyMatch) {
                    authoredOverride = &slot;
                    break;
                }
            }

            const Material* runtimeOverride = GetRuntimeMaterialOverride(static_cast<uint32_t>(materialIndex));
            ImGui::PushID(static_cast<int>(materialIndex));
            ImGui::Text(TEXT::Localize("editor.ui.slot.zu.s.77312e").c_str(), materialIndex,
                        imported.name.empty() ? "Unnamed" : imported.name.c_str());
            ImGui::TextDisabled(TEXT::Localize("editor.ui.binding.s.79a318").c_str(),
                                authoredOverride != nullptr && authoredOverride->materialAssetGuid.IsValid()
                                    ? ShortGuid(authoredOverride->materialAssetGuid.value).c_str()
                                    : "Imported model material");
            ImGui::TextDisabled(TEXT::Localize("editor.ui.runtime.s.420339").c_str(),
                                runtimeOverride != nullptr ? "Override ready" : "Imported fallback");
            if (runtimeOverride != nullptr &&
                ImGui::TreeNode(
                    TEXT::LocalizeWindow("editor.ui.runtime.texture.debug.03729c", "Runtime Texture Debug").c_str())) {
                auto drawRuntimeSlot = [](const char* label, const RuntimeTextureSlot& slot) {
                    ImGui::Text(TEXT::Localize("editor.ui.s.handle.d.active.s.0e7297").c_str(), label, slot.handle,
                                slot.IsActive() ? "true" : "false");
                    ImGui::TextDisabled(TEXT::Localize("editor.ui.resolved.s.57180c").c_str(),
                                        slot.resolvedPath.empty() ? "<none>" : slot.resolvedPath.c_str());
                };
                drawRuntimeSlot("BaseColor", runtimeOverride->GetTextureSlot(MaterialTextureUsage::BaseColor));
                drawRuntimeSlot("Normal", runtimeOverride->GetTextureSlot(MaterialTextureUsage::Normal));
                drawRuntimeSlot("MetallicRoughness",
                                runtimeOverride->GetTextureSlot(MaterialTextureUsage::MetallicRoughness));
                drawRuntimeSlot("Occlusion", runtimeOverride->GetTextureSlot(MaterialTextureUsage::Occlusion));
                drawRuntimeSlot("Emissive", runtimeOverride->GetTextureSlot(MaterialTextureUsage::Emissive));
                drawRuntimeSlot("Specular", runtimeOverride->GetTextureSlot(MaterialTextureUsage::Specular));
                drawRuntimeSlot("SpecularColor", runtimeOverride->GetTextureSlot(MaterialTextureUsage::SpecularColor));
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx(TEXT::LocalizeWindow("ui.debugView.shadow", "Shadow").c_str())) {
        ImGui::Checkbox(TEXT::LocalizeWindow("ui.inspector.model.castShadow", "Cast Shadow").c_str(), &castShadow_);
        ImGui::Checkbox(TEXT::LocalizeWindow("ui.inspector.model.receiveShadow", "Receive Shadow").c_str(),
                        &receiveShadow_);
        ImGui::Checkbox(TEXT::LocalizeWindow("ui.inspector.model.renderStatic", "Render Static").c_str(),
                        &renderStatic_);
        ImGui::TreePop();
    }

    if (asset_ == nullptr) {
        ImGui::TextUnformatted(TEXT::Localize("editor.ui.asset.none.517a6c").c_str());
        notifyIfRenderStateChanged();
        return;
    }

    const size_t matrixNodeCount = static_cast<size_t>(std::count_if(
        asset_->nodes.begin(), asset_->nodes.end(), [](const ModelNode& node) { return node.hasLocalMatrix; }));
    const size_t skinNodeCount = static_cast<size_t>(std::count_if(
        asset_->nodes.begin(), asset_->nodes.end(), [](const ModelNode& node) { return node.skinIndex >= 0; }));
    size_t meshCount = asset_->meshes.size();
    size_t primitiveCount = 0;
    size_t skinnedPrimitiveCount = 0;
    size_t skinnedVertexCount = 0;
    const SkinnedVertex3D* firstSkinnedVertex = nullptr;
    for (const MeshAsset& mesh : asset_->meshes) {
        primitiveCount += mesh.primitives.size();
        for (const MeshPrimitive& primitive : mesh.primitives) {
            if (!primitive.skinnedVertices.empty()) {
                ++skinnedPrimitiveCount;
                skinnedVertexCount += primitive.skinnedVertices.size();
                if (firstSkinnedVertex == nullptr) {
                    firstSkinnedVertex = &primitive.skinnedVertices.front();
                }
            }
        }
    }

    if (ImGui::TreeNodeEx(TEXT::LocalizeWindow("editor.ui.model.basic.b26c88", "Model Basic").c_str())) {
        ImGui::Text(TEXT::Localize("editor.ui.asset.s.f75f74").c_str(), asset_->GetName().c_str());
        ImGui::Text(TEXT::Localize("ui.assetBrowser.tooltipSource").c_str(), asset_->GetSourcePath().c_str());
        ImGui::Text(TEXT::Localize("ui.quality.stateFormat").c_str(), ToStateText(asset_->GetState()));
        ImGui::Text(TEXT::Localize("editor.ui.has.legacy.runtime.mesh.s.d93988").c_str(),
                    asset_->GetLegacyRuntimeMesh() ? "Yes" : "No");
        ImGui::Text(TEXT::Localize("editor.ui.has.legacy.runtime.material.s.1c80a5").c_str(),
                    asset_->GetLegacyRuntimeMaterial() ? "Yes" : "No");
        if (const Material* material = asset_->GetLegacyRuntimeMaterial()) {
            const MATH::Vec4& color = material->GetBaseColor();
            ImGui::Text(TEXT::Localize("editor.ui.basecolor.2f.2f.2f.2f.032572").c_str(), color.x, color.y, color.z,
                        color.w);
            auto drawRuntimeSlot = [](const char* label, const RuntimeTextureSlot& slot) {
                ImGui::Text(TEXT::Localize("editor.ui.s.handle.d.source.s.39f78c").c_str(), label, slot.handle,
                            slot.sourcePath.empty() ? "<none>" : slot.sourcePath.c_str());
                ImGui::Text(TEXT::Localize("editor.ui.resolved.s.57180c").c_str(),
                            slot.resolvedPath.empty() ? "<none>" : slot.resolvedPath.c_str());
            };
            drawRuntimeSlot("BaseColor", material->GetTextureSlot(MaterialTextureUsage::BaseColor));
            drawRuntimeSlot("Normal", material->GetTextureSlot(MaterialTextureUsage::Normal));
            drawRuntimeSlot("MetallicRoughness", material->GetTextureSlot(MaterialTextureUsage::MetallicRoughness));
            drawRuntimeSlot("Occlusion", material->GetTextureSlot(MaterialTextureUsage::Occlusion));
            drawRuntimeSlot("Emissive", material->GetTextureSlot(MaterialTextureUsage::Emissive));
            drawRuntimeSlot("Specular", material->GetTextureSlot(MaterialTextureUsage::Specular));
            drawRuntimeSlot("SpecularColor", material->GetTextureSlot(MaterialTextureUsage::SpecularColor));
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode(TEXT::LocalizeWindow("editor.ui.model.structure.4e14e7", "Model Structure").c_str())) {
        ImGui::Text(TEXT::Localize("editor.ui.nodes.zu.b7d1d8").c_str(), asset_->nodes.size());
        ImGui::Text(TEXT::Localize("editor.ui.matrix.nodes.zu.20503e").c_str(), matrixNodeCount);
        ImGui::Text(TEXT::Localize("editor.ui.mesh.count.zu.c89f7f").c_str(), meshCount);
        ImGui::Text(TEXT::Localize("editor.ui.primitive.count.zu.20af9a").c_str(), primitiveCount);
        ImGui::Text(TEXT::Localize("editor.ui.materials.zu.390571").c_str(), asset_->materials.size());
        ImGui::Text(TEXT::Localize("editor.ui.textures.zu.9eee42").c_str(), asset_->textures.size());
        ImGui::TreePop();
    }

    if (ImGui::TreeNode(TEXT::LocalizeWindow("ui.resource.scope.materials", "Materials").c_str())) {
        for (size_t materialIndex = 0; materialIndex < asset_->materials.size(); ++materialIndex) {
            const MaterialAsset& material = asset_->materials[materialIndex];
            ImGui::PushID(static_cast<int>(materialIndex));
            const char* materialName = material.name.empty() ? "<unnamed>" : material.name.c_str();
            if (ImGui::TreeNode("Material", TEXT::Localize("editor.ui.material.zu.s.383f41").c_str(), materialIndex,
                                materialName)) {
                ImGui::Text(TEXT::Localize("editor.ui.base.color.factor.3f.3f.3f.3f.a2962c").c_str(),
                            material.baseColorFactor.x, material.baseColorFactor.y, material.baseColorFactor.z,
                            material.baseColorFactor.w);
                ImGui::Text(TEXT::Localize("editor.ui.base.color.texture.index.d.texcoord.d.source.s.345c7d").c_str(),
                            material.baseColorTexture.textureIndex, material.baseColorTexture.texCoord,
                            ResolveTexturePathDebug(*asset_, material.baseColorTexture));
                ImGui::Text(TEXT::Localize("editor.ui.resolved.s.57180c").c_str(),
                            ResolveTextureResolvedPathDebug(*asset_, material.baseColorTexture));
                ImGui::Text(TEXT::Localize("editor.ui.metallic.roughness.3f.3f.2e8376").c_str(),
                            material.metallicFactor, material.roughnessFactor);
                ImGui::Text(
                    TEXT::Localize("editor.ui.metallic.roughness.texture.index.d.texcoord.d.source.s.00041b").c_str(),
                    material.metallicRoughnessTexture.textureIndex, material.metallicRoughnessTexture.texCoord,
                    ResolveTexturePathDebug(*asset_, material.metallicRoughnessTexture));
                ImGui::Text(TEXT::Localize("editor.ui.resolved.s.57180c").c_str(),
                            ResolveTextureResolvedPathDebug(*asset_, material.metallicRoughnessTexture));
                ImGui::Text(
                    TEXT::Localize("editor.ui.normal.texture.index.d.texcoord.d.scale.3f.source.s.d01fcd").c_str(),
                    material.normalTexture.textureIndex, material.normalTexture.texCoord, material.normalTexture.scale,
                    ResolveTexturePathDebug(*asset_, material.normalTexture));
                ImGui::Text(TEXT::Localize("editor.ui.resolved.s.57180c").c_str(),
                            ResolveTextureResolvedPathDebug(*asset_, material.normalTexture));
                ImGui::Text(TEXT::Localize("editor.ui.occlusion.texture.index.d.texcoord.d.strength.3f.source.s.30f593")
                                .c_str(),
                            material.occlusionTexture.textureIndex, material.occlusionTexture.texCoord,
                            material.occlusionTexture.strength,
                            ResolveTexturePathDebug(*asset_, material.occlusionTexture));
                ImGui::Text(TEXT::Localize("editor.ui.resolved.s.57180c").c_str(),
                            ResolveTextureResolvedPathDebug(*asset_, material.occlusionTexture));
                ImGui::Text(TEXT::Localize("editor.ui.emissive.factor.3f.3f.3f.8e6c77").c_str(),
                            material.emissiveFactor.x, material.emissiveFactor.y, material.emissiveFactor.z);
                ImGui::Text(TEXT::Localize("editor.ui.emissive.strength.3f.9dd344").c_str(), material.emissiveStrength);
                ImGui::Text(TEXT::Localize("editor.ui.emissive.texture.index.d.texcoord.d.source.s.c7549f").c_str(),
                            material.emissiveTexture.textureIndex, material.emissiveTexture.texCoord,
                            ResolveTexturePathDebug(*asset_, material.emissiveTexture));
                ImGui::Text(TEXT::Localize("editor.ui.resolved.s.57180c").c_str(),
                            ResolveTextureResolvedPathDebug(*asset_, material.emissiveTexture));
                ImGui::Text(TEXT::Localize("editor.ui.alpha.mode.s.47f9df").c_str(),
                            ToAlphaModeText(material.alphaMode));
                ImGui::Text(TEXT::Localize("editor.ui.alpha.cutoff.3f.246c24").c_str(), material.alphaCutoff);
                ImGui::Text(TEXT::Localize("editor.ui.double.sided.s.dee3d8").c_str(),
                            material.doubleSided ? "Yes" : "No");
                ImGui::Text(TEXT::Localize("editor.ui.feature.bits.0x.08x.02f895").c_str(), material.featureBits);
                ImGui::Text(TEXT::Localize("editor.ui.unlit.s.85426c").c_str(),
                            (material.featureBits & MATERIAL_FEATURES::Unlit) != 0 ? "Yes" : "No");
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode(TEXT::LocalizeWindow("editor.ui.skinning.5d643d", "Skinning").c_str())) {
        ImGui::Text(TEXT::Localize("editor.ui.skin.nodes.zu.94b051").c_str(), skinNodeCount);
        ImGui::Checkbox(TEXT::LocalizeWindow("editor.ui.show.skeleton.debug.ee912e", "Show Skeleton Debug").c_str(),
                        &showSkeletonDebug_);
        ImGui::Checkbox(TEXT::LocalizeWindow("editor.ui.skeleton.debug.xray.ca5e64", "Skeleton Debug XRay").c_str(),
                        &skeletonDebugXRay_);
        ImGui::Text(TEXT::Localize("editor.ui.skins.zu.b4a238").c_str(), asset_->GetSkinCount());
        ImGui::Text(TEXT::Localize("editor.ui.skinned.mesh.s.97b327").c_str(), asset_->HasSkinnedMesh() ? "Yes" : "No");
        ImGui::Text(TEXT::Localize("editor.ui.skinned.primitive.count.zu.7b8af1").c_str(), skinnedPrimitiveCount);
        ImGui::Text(TEXT::Localize("editor.ui.skinned.vertex.count.zu.818288").c_str(), skinnedVertexCount);
        if (firstSkinnedVertex != nullptr) {
            ImGui::Text(TEXT::Localize("editor.ui.first.joints.u.u.u.u.fe446e").c_str(),
                        static_cast<unsigned>(firstSkinnedVertex->joints[0]),
                        static_cast<unsigned>(firstSkinnedVertex->joints[1]),
                        static_cast<unsigned>(firstSkinnedVertex->joints[2]),
                        static_cast<unsigned>(firstSkinnedVertex->joints[3]));
            ImGui::Text(TEXT::Localize("editor.ui.first.weights.3f.3f.3f.3f.3e7f16").c_str(),
                        firstSkinnedVertex->weights[0], firstSkinnedVertex->weights[1], firstSkinnedVertex->weights[2],
                        firstSkinnedVertex->weights[3]);
        }
        for (size_t skinIndex = 0; skinIndex < asset_->skins.size(); ++skinIndex) {
            const SkeletonAsset& skin = asset_->skins[skinIndex];
            ImGui::PushID(static_cast<int>(skinIndex));
            if (ImGui::TreeNode("Skin", TEXT::Localize("editor.ui.skin.zu.s.39df7d").c_str(), skinIndex,
                                skin.name.c_str())) {
                ImGui::Text(TEXT::Localize("editor.ui.skeleton.root.node.d.3fcbe4").c_str(), skin.skeletonRootNode);
                ImGui::Text(TEXT::Localize("editor.ui.joint.count.zu.bd6df9").c_str(), skin.joints.size());
                const size_t maxDebugJoints = (std::min)(skin.joints.size(), static_cast<size_t>(32));
                for (size_t jointIndex = 0; jointIndex < maxDebugJoints; ++jointIndex) {
                    const SkeletonJoint& joint = skin.joints[jointIndex];
                    ImGui::BulletText("[%zu] %s node=%d parentJoint=%d", jointIndex, joint.name.c_str(),
                                      joint.nodeIndex, joint.parentJoint);
                }
                if (skin.joints.size() > maxDebugJoints) {
                    ImGui::Text(TEXT::Localize("editor.ui.zu.more.joints.26caef").c_str(),
                                skin.joints.size() - maxDebugJoints);
                }
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::TreePop();
    }

    const MODELRENDERER::ModelRendererDebugStats& rendererStats = MODELRENDERER::GetDebugStats();
    const MODELRENDERER::ModelRendererCacheStats& rendererCacheStats = rendererStats.cache;
    const MESHRENDERER::MeshRendererDebugStats& meshRendererStats = MESHRENDERER::GetDebugStats();

    if (ImGui::TreeNode(TEXT::LocalizeWindow("editor.ui.runtime.skinning.e5a59b", "Runtime Skinning").c_str())) {
        ImGui::Text(TEXT::Localize("editor.ui.gpu.driven.skinned.commands.zu.27b660").c_str(),
                    meshRendererStats.gpuDrivenSkinnedCommandCount);
        ImGui::Text(TEXT::Localize("editor.ui.gpu.driven.skinned.source.records.zu.c63bfd").c_str(),
                    meshRendererStats.gpuDrivenSkinnedSourceRecordCount);
        ImGui::Text(TEXT::Localize("editor.ui.skinned.gpu.draws.zu.484e22").c_str(),
                    meshRendererStats.skinnedGpuDrawCount);
        ImGui::Text(TEXT::Localize("editor.ui.uploaded.joints.zu.201370").c_str(),
                    meshRendererStats.uploadedJointCount);
        ImGui::TreePop();
    }

    if (ImGui::TreeNode(TEXT::LocalizeWindow("editor.ui.performance.stats.2d6182", "Performance Stats").c_str())) {
        ImGui::TextUnformatted(TEXT::Localize("editor.ui.modelrenderer.d0b42a").c_str());
        ImGui::Text(TEXT::Localize("editor.ui.frame.kind.s.6033fa").c_str(),
                    MODELRENDERER::ToString(rendererStats.frameKind));
        ImGui::Text(TEXT::Localize("editor.ui.rendermodel.cache.requests.u.742141").c_str(),
                    rendererCacheStats.renderModelCacheRequestCount);
        ImGui::Text(TEXT::Localize("editor.ui.rendermodel.cache.hit.miss.u.u.225694").c_str(),
                    rendererCacheStats.renderModelCacheHitCount, rendererCacheStats.renderModelCacheMissCount);

        ImGui::Separator();
        ImGui::TextUnformatted(TEXT::Localize("editor.ui.meshrenderer.39671f").c_str());
        ImGui::Text(TEXT::Localize("editor.ui.primitive.mesh.cache.hit.miss.zu.zu.85fe00").c_str(),
                    meshRendererStats.primitiveMeshCacheHitCount, meshRendererStats.primitiveMeshCacheMissCount);
        ImGui::Text(TEXT::Localize("editor.ui.skinned.primitive.mesh.cache.hit.miss.zu.zu.082f4f").c_str(),
                    meshRendererStats.primitiveSkinnedMeshCacheHitCount,
                    meshRendererStats.primitiveSkinnedMeshCacheMissCount);
        ImGui::Text(TEXT::Localize("editor.ui.texture.cache.hit.miss.zu.zu.5a94d6").c_str(),
                    meshRendererStats.materialTextureCacheHitCount, meshRendererStats.materialTextureCacheMissCount);
        ImGui::Text(TEXT::Localize("editor.ui.pso.cache.hit.miss.zu.zu.37d432").c_str(),
                    meshRendererStats.psoCacheHitCount, meshRendererStats.psoCacheMissCount);
        ImGui::Text(TEXT::Localize("editor.ui.materialfx.profile.cache.hit.miss.fail.zu.zu.zu.665fd8").c_str(),
                    meshRendererStats.materialFxProfileCacheHitCount, meshRendererStats.materialFxProfileCacheMissCount,
                    meshRendererStats.materialFxProfileCacheFailCount);
        ImGui::Text(TEXT::Localize("editor.ui.normalmapped.primitives.zu.a6b992").c_str(),
                    meshRendererStats.normalMappedPrimitiveCount);
        ImGui::Text(TEXT::Localize("editor.ui.normalmap.fallbacks.zu.f27965").c_str(),
                    meshRendererStats.normalMapFallbackCount);
        ImGui::Text(TEXT::Localize("editor.ui.normaltexture.cache.hit.miss.zu.zu.7268d9").c_str(),
                    meshRendererStats.normalTextureCacheHitCount, meshRendererStats.normalTextureCacheMissCount);
        ImGui::Text(TEXT::Localize("editor.ui.emissive.mapped.primitives.zu.2b97c1").c_str(),
                    meshRendererStats.emissiveMappedPrimitiveCount);
        ImGui::Text(TEXT::Localize("editor.ui.emissivemap.fallbacks.zu.819da8").c_str(),
                    meshRendererStats.emissiveMapFallbackCount);
        ImGui::Text(TEXT::Localize("editor.ui.emissivetexture.cache.hit.miss.zu.zu.316513").c_str(),
                    meshRendererStats.emissiveTextureCacheHitCount, meshRendererStats.emissiveTextureCacheMissCount);
        const MESHWIREDEBUG::MeshWireDebugStats& wireStats = MESHWIREDEBUG::GetDebugStats();
        ImGui::Separator();
        ImGui::TextUnformatted(TEXT::Localize("editor.ui.wire.debug.aa3fac").c_str());
        ImGui::Text(TEXT::Localize("editor.ui.submitted.models.lines.zu.zu.08aa44").c_str(),
                    wireStats.submittedModelCount, wireStats.submittedLineCount);
        ImGui::Text(TEXT::Localize("editor.ui.truncated.models.zu.66ad0b").c_str(), wireStats.truncatedModelCount);
        ImGui::Text(TEXT::Localize("editor.ui.cache.hit.miss.zu.zu.87111f").c_str(), wireStats.cacheHitCount,
                    wireStats.cacheMissCount);
        ImGui::TreePop();
    }

    if (ImGui::TreeNode(TEXT::LocalizeWindow("editor.ui.animation.clips.cfc4b5", "Animation Clips").c_str())) {
        ImGui::Text(TEXT::Localize("editor.ui.animation.clips.zu.e21df6").c_str(), asset_->animations.size());
        for (const AnimationClip& clip : asset_->animations) {
            ImGui::BulletText("%s (%.2fs)", clip.name.c_str(), clip.durationSec);
        }
        ImGui::TreePop();
    }
    notifyIfRenderStateChanged();
#endif
}

} // namespace HIKARI
