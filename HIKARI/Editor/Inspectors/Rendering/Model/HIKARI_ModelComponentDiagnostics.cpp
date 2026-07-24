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
            case AlphaMode::Opaque:
                return "Opaque";
            case AlphaMode::Mask:
                return "Mask";
            case AlphaMode::Blend:
                return "Blend";
            default:
                return "Unknown";
            }
        }

        const char* ResolveTexturePathDebug(
            const ModelAsset& asset,
            const TextureSlot& slot) {

            if (slot.textureIndex < 0 ||
                slot.textureIndex >=
                    static_cast<int>(asset.textures.size())) {

                return "<none>";
            }
            const std::string& path =
                asset.textures[
                    static_cast<size_t>(slot.textureIndex)]
                    .sourcePath;
            return path.empty() ? "<empty>" : path.c_str();
        }

        std::string ShortGuid(const std::string& guid) {
            return guid.size() <= 8u
                ? guid
                : guid.substr(0u, 8u);
        }

        const char* ResolveTextureResolvedPathDebug(
            const ModelAsset& asset,
            const TextureSlot& slot) {

            if (slot.textureIndex < 0 ||
                slot.textureIndex >=
                    static_cast<int>(asset.textures.size())) {

                return "<none>";
            }
            const TextureAsset3D& texture =
                asset.textures[
                    static_cast<size_t>(slot.textureIndex)];
            const std::string& path =
                texture.resolvedPath.empty()
                    ? texture.sourcePath
                    : texture.resolvedPath;
            return path.empty() ? "<empty>" : path.c_str();
        }

        const char* ToStateText(ModelAsset::State state) {
            switch (state) {
            case ModelAsset::State::Unloaded:
                return "Unloaded";
            case ModelAsset::State::Loaded:
                return "Loaded";
            case ModelAsset::State::Failed:
                return "Failed";
            default:
                return "Unknown";
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
            if (oldVisible != visible_ ||
                oldShowSkeletonDebug != showSkeletonDebug_ ||
                oldSkeletonDebugXRay != skeletonDebugXRay_ ||
                oldCastShadow != castShadow_ ||
                oldReceiveShadow != receiveShadow_ ||
                oldRenderStatic != renderStatic_ ||
                oldDebugRenderMode != debugRenderMode_ ||
                oldWireColor != wireColor_ ||
                oldMaxWireLines != maxWireLines_ ||
                oldWirePerPrimitiveColor != wirePerPrimitiveColor_ ||
                oldPostGroupMask != postGroupMask_) {

                NotifyRenderStateDirty();
            }
        };

        if (ImGui::TreeNodeEx("Model Source")) {
            ImGui::Checkbox("Visible", &visible_);
            int postMask = static_cast<int>(postGroupMask_);
            if (ImGui::InputInt("Post Group Mask", &postMask)) {
                postGroupMask_ =
                    static_cast<uint32_t>(postMask < 0 ? 0 : postMask);
            }
            ImGui::Text("Asset Id: %s", assetId_.empty() ? "<none>" : assetId_.c_str());
            if (asset_ == nullptr) {
                ImGui::TextDisabled("A Procedural Mesh component may provide geometry instead.");
            }
            int debugMode = static_cast<int>(debugRenderMode_);
            const char* debugModes[] = { "Normal", "WireOverlay", "WireOnly", "BoundsOnly" };
            if (ImGui::Combo("Debug Render Mode", &debugMode, debugModes, 4)) {
                debugRenderMode_ = static_cast<ModelRenderDebugMode>((std::clamp)(debugMode, 0, 3));
            }
            ImGui::InputScalar("Wire Color RGBA", ImGuiDataType_U32, &wireColor_);
            int maxWireLines = static_cast<int>(maxWireLines_);
            if (ImGui::DragInt("Max Wire Lines", &maxWireLines, 100.0f, 0, 1000000)) {
                maxWireLines_ = static_cast<uint32_t>((std::max)(0, maxWireLines));
            }
            ImGui::Checkbox("Wire Per Primitive Color", &wirePerPrimitiveColor_);
            ImGui::TreePop();
        }


        if (ImGui::TreeNodeEx("Material Override", ImGuiTreeNodeFlags_DefaultOpen)) {
            const ModelMaterialOverrideSlot* slot0 = nullptr;
            for (const ModelMaterialOverrideSlot& slot : materialOverrides_) {
                if (slot.slotIndex == 0) {
                    slot0 = &slot;
                    break;
                }
            }
            ImGui::Text("Slot 0: %s",
                (slot0 && slot0->materialAssetGuid.IsValid()) ? ShortGuid(slot0->materialAssetGuid.value).c_str() : "<none>");
            ImGui::Text("Runtime Override: %s",
                runtimeMaterialOverride_ ? "Ready" : "Default model material");
            if (runtimeMaterialOverride_ && ImGui::TreeNode("Runtime Texture Debug")) {
                auto drawRuntimeSlot = [](const char* label, const RuntimeTextureSlot& slot) {
                    ImGui::Text("%s: handle=%d active=%s",
                        label,
                        slot.handle,
                        slot.IsActive() ? "true" : "false");
                    ImGui::TextDisabled("  resolved=%s", slot.resolvedPath.empty() ? "<none>" : slot.resolvedPath.c_str());
                };
                drawRuntimeSlot("BaseColor", runtimeMaterialOverride_->GetTextureSlot(MaterialTextureUsage::BaseColor));
                drawRuntimeSlot("Normal", runtimeMaterialOverride_->GetTextureSlot(MaterialTextureUsage::Normal));
                drawRuntimeSlot("MetallicRoughness", runtimeMaterialOverride_->GetTextureSlot(MaterialTextureUsage::MetallicRoughness));
                drawRuntimeSlot("Occlusion", runtimeMaterialOverride_->GetTextureSlot(MaterialTextureUsage::Occlusion));
                drawRuntimeSlot("Emissive", runtimeMaterialOverride_->GetTextureSlot(MaterialTextureUsage::Emissive));
                drawRuntimeSlot("Specular", runtimeMaterialOverride_->GetTextureSlot(MaterialTextureUsage::Specular));
                drawRuntimeSlot("SpecularColor", runtimeMaterialOverride_->GetTextureSlot(MaterialTextureUsage::SpecularColor));
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Shadow")) {
            ImGui::Checkbox("Cast Shadow", &castShadow_);
            ImGui::Checkbox("Receive Shadow", &receiveShadow_);
            ImGui::Checkbox("Render Static", &renderStatic_);
            ImGui::TreePop();
        }

        if (asset_ == nullptr) {
            ImGui::TextUnformatted("Asset: <none>");
            notifyIfRenderStateChanged();
            return;
        }

        const size_t matrixNodeCount = static_cast<size_t>(std::count_if(asset_->nodes.begin(), asset_->nodes.end(), [](const ModelNode& node) {
            return node.hasLocalMatrix;
        }));
        const size_t skinNodeCount = static_cast<size_t>(std::count_if(asset_->nodes.begin(), asset_->nodes.end(), [](const ModelNode& node) {
            return node.skinIndex >= 0;
        }));
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

        if (ImGui::TreeNodeEx("Model Basic")) {
            ImGui::Text("Asset: %s", asset_->GetName().c_str());
            ImGui::Text("Source: %s", asset_->GetSourcePath().c_str());
            ImGui::Text("State: %s", ToStateText(asset_->GetState()));
            ImGui::Text(
                "Has Legacy Runtime Mesh: %s",
                asset_->GetLegacyRuntimeMesh() ? "Yes" : "No");
            ImGui::Text(
                "Has Legacy Runtime Material: %s",
                asset_->GetLegacyRuntimeMaterial() ? "Yes" : "No");
            if (const Material* material =
                asset_->GetLegacyRuntimeMaterial()) {
                const MATH::Vec4& color = material->GetBaseColor();
                ImGui::Text("BaseColor: (%.2f, %.2f, %.2f, %.2f)", color.x, color.y, color.z, color.w);
                auto drawRuntimeSlot = [](const char* label, const RuntimeTextureSlot& slot) {
                    ImGui::Text("%s: handle=%d source=%s",
                        label,
                        slot.handle,
                        slot.sourcePath.empty() ? "<none>" : slot.sourcePath.c_str());
                    ImGui::Text("  resolved=%s", slot.resolvedPath.empty() ? "<none>" : slot.resolvedPath.c_str());
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

        if (ImGui::TreeNode("Model Structure")) {
            ImGui::Text("Nodes: %zu", asset_->nodes.size());
            ImGui::Text("Matrix Nodes: %zu", matrixNodeCount);
            ImGui::Text("Mesh Count: %zu", meshCount);
            ImGui::Text("Primitive Count: %zu", primitiveCount);
            ImGui::Text("Materials: %zu", asset_->materials.size());
            ImGui::Text("Textures: %zu", asset_->textures.size());
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Materials")) {
            for (size_t materialIndex = 0; materialIndex < asset_->materials.size(); ++materialIndex) {
                const MaterialAsset& material = asset_->materials[materialIndex];
                ImGui::PushID(static_cast<int>(materialIndex));
                const char* materialName = material.name.empty() ? "<unnamed>" : material.name.c_str();
                if (ImGui::TreeNode("Material", "Material[%zu] %s", materialIndex, materialName)) {
                    ImGui::Text("Base Color Factor: %.3f %.3f %.3f %.3f",
                        material.baseColorFactor.x,
                        material.baseColorFactor.y,
                        material.baseColorFactor.z,
                        material.baseColorFactor.w);
                    ImGui::Text("Base Color Texture: index=%d texCoord=%d source=%s",
                        material.baseColorTexture.textureIndex,
                        material.baseColorTexture.texCoord,
                        ResolveTexturePathDebug(*asset_, material.baseColorTexture));
                    ImGui::Text("  resolved=%s", ResolveTextureResolvedPathDebug(*asset_, material.baseColorTexture));
                    ImGui::Text("Metallic / Roughness: %.3f / %.3f", material.metallicFactor, material.roughnessFactor);
                    ImGui::Text("Metallic Roughness Texture: index=%d texCoord=%d source=%s",
                        material.metallicRoughnessTexture.textureIndex,
                        material.metallicRoughnessTexture.texCoord,
                        ResolveTexturePathDebug(*asset_, material.metallicRoughnessTexture));
                    ImGui::Text("  resolved=%s", ResolveTextureResolvedPathDebug(*asset_, material.metallicRoughnessTexture));
                    ImGui::Text("Normal Texture: index=%d texCoord=%d scale=%.3f source=%s",
                        material.normalTexture.textureIndex,
                        material.normalTexture.texCoord,
                        material.normalTexture.scale,
                        ResolveTexturePathDebug(*asset_, material.normalTexture));
                    ImGui::Text("  resolved=%s", ResolveTextureResolvedPathDebug(*asset_, material.normalTexture));
                    ImGui::Text("Occlusion Texture: index=%d texCoord=%d strength=%.3f source=%s",
                        material.occlusionTexture.textureIndex,
                        material.occlusionTexture.texCoord,
                        material.occlusionTexture.strength,
                        ResolveTexturePathDebug(*asset_, material.occlusionTexture));
                    ImGui::Text("  resolved=%s", ResolveTextureResolvedPathDebug(*asset_, material.occlusionTexture));
                    ImGui::Text("Emissive Factor: %.3f %.3f %.3f",
                        material.emissiveFactor.x,
                        material.emissiveFactor.y,
                        material.emissiveFactor.z);
                    ImGui::Text("Emissive Strength: %.3f", material.emissiveStrength);
                    ImGui::Text("Emissive Texture: index=%d texCoord=%d source=%s",
                        material.emissiveTexture.textureIndex,
                        material.emissiveTexture.texCoord,
                        ResolveTexturePathDebug(*asset_, material.emissiveTexture));
                    ImGui::Text("  resolved=%s", ResolveTextureResolvedPathDebug(*asset_, material.emissiveTexture));
                    ImGui::Text("Alpha Mode: %s", ToAlphaModeText(material.alphaMode));
                    ImGui::Text("Alpha Cutoff: %.3f", material.alphaCutoff);
                    ImGui::Text("Double Sided: %s", material.doubleSided ? "Yes" : "No");
                    ImGui::Text("Feature Bits: 0x%08X", material.featureBits);
                    ImGui::Text("Unlit: %s", (material.featureBits & MATERIAL_FEATURES::Unlit) != 0 ? "Yes" : "No");
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Skinning")) {
            ImGui::Text("Skin Nodes: %zu", skinNodeCount);
            ImGui::Checkbox("Show Skeleton Debug", &showSkeletonDebug_);
            ImGui::Checkbox("Skeleton Debug XRay", &skeletonDebugXRay_);
            ImGui::Text("Skins: %zu", asset_->GetSkinCount());
            ImGui::Text("Skinned Mesh: %s", asset_->HasSkinnedMesh() ? "Yes" : "No");
            ImGui::Text("Skinned Primitive Count: %zu", skinnedPrimitiveCount);
            ImGui::Text("Skinned Vertex Count: %zu", skinnedVertexCount);
            if (firstSkinnedVertex != nullptr) {
                ImGui::Text("First Joints: %u %u %u %u",
                    static_cast<unsigned>(firstSkinnedVertex->joints[0]),
                    static_cast<unsigned>(firstSkinnedVertex->joints[1]),
                    static_cast<unsigned>(firstSkinnedVertex->joints[2]),
                    static_cast<unsigned>(firstSkinnedVertex->joints[3]));
                ImGui::Text("First Weights: %.3f %.3f %.3f %.3f",
                    firstSkinnedVertex->weights[0],
                    firstSkinnedVertex->weights[1],
                    firstSkinnedVertex->weights[2],
                    firstSkinnedVertex->weights[3]);
            }
            for (size_t skinIndex = 0; skinIndex < asset_->skins.size(); ++skinIndex) {
                const SkeletonAsset& skin = asset_->skins[skinIndex];
                ImGui::PushID(static_cast<int>(skinIndex));
                if (ImGui::TreeNode("Skin", "Skin[%zu] %s", skinIndex, skin.name.c_str())) {
                    ImGui::Text("Skeleton Root Node: %d", skin.skeletonRootNode);
                    ImGui::Text("Joint Count: %zu", skin.joints.size());
                    const size_t maxDebugJoints = (std::min)(skin.joints.size(), static_cast<size_t>(32));
                    for (size_t jointIndex = 0; jointIndex < maxDebugJoints; ++jointIndex) {
                        const SkeletonJoint& joint = skin.joints[jointIndex];
                        ImGui::BulletText("[%zu] %s node=%d parentJoint=%d", jointIndex, joint.name.c_str(), joint.nodeIndex, joint.parentJoint);
                    }
                    if (skin.joints.size() > maxDebugJoints) {
                        ImGui::Text("... %zu more joints", skin.joints.size() - maxDebugJoints);
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

        if (ImGui::TreeNode("Runtime Skinning")) {
            ImGui::Text("GPU Driven Skinned Commands: %zu", meshRendererStats.gpuDrivenSkinnedCommandCount);
            ImGui::Text("GPU Driven Skinned Records: %zu / %zu / %zu",
                meshRendererStats.gpuDrivenSkinnedSourceRecordCount,
                meshRendererStats.gpuDrivenSkinnedSubmittedRecordCount,
                meshRendererStats.gpuDrivenSkinnedSkippedRecordCount);
            ImGui::Text("Skinned GPU Draws: %zu", meshRendererStats.skinnedGpuDrawCount);
            ImGui::Text("Skinned Fallbacks: %zu", meshRendererStats.skinnedFallbackCount);
            ImGui::Text("Uploaded Joints: %zu", meshRendererStats.uploadedJointCount);
            ImGui::Text("Max Joint Count: %zu", meshRendererStats.maxJointCount);
            ImGui::Text("Last Skinned Vertex Count: %zu", meshRendererStats.lastSkinnedVertexCount);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Performance Stats")) {
            ImGui::TextUnformatted("ModelRenderer:");
            ImGui::Text("Frame Kind: %s", MODELRENDERER::ToString(rendererStats.frameKind));
            ImGui::Text("RenderModel Cache Requests: %u", rendererCacheStats.renderModelCacheRequestCount);
            ImGui::Text("RenderModel Cache Hit / Miss: %u / %u",
                rendererCacheStats.renderModelCacheHitCount,
                rendererCacheStats.renderModelCacheMissCount);

            ImGui::Separator();
            ImGui::TextUnformatted("MeshRenderer:");
            ImGui::Text("Wire GPU Draws: %zu", meshRendererStats.wireGpuDrawCount);
            ImGui::Text("Primitive Mesh Cache Hit / Miss: %zu / %zu",
                meshRendererStats.primitiveMeshCacheHitCount,
                meshRendererStats.primitiveMeshCacheMissCount);
            ImGui::Text("Skinned Primitive Mesh Cache Hit / Miss: %zu / %zu",
                meshRendererStats.primitiveSkinnedMeshCacheHitCount,
                meshRendererStats.primitiveSkinnedMeshCacheMissCount);
            ImGui::Text("Texture Cache Hit / Miss: %zu / %zu",
                meshRendererStats.materialTextureCacheHitCount,
                meshRendererStats.materialTextureCacheMissCount);
            ImGui::Text("PSO Cache Hit / Miss: %zu / %zu",
                meshRendererStats.psoCacheHitCount,
                meshRendererStats.psoCacheMissCount);
            ImGui::Text("MaterialFx Profile Cache Hit / Miss / Fail: %zu / %zu / %zu",
                meshRendererStats.materialFxProfileCacheHitCount,
                meshRendererStats.materialFxProfileCacheMissCount,
                meshRendererStats.materialFxProfileCacheFailCount);
            ImGui::Text("NormalMapped Primitives: %zu", meshRendererStats.normalMappedPrimitiveCount);
            ImGui::Text("NormalMap Fallbacks: %zu", meshRendererStats.normalMapFallbackCount);
            ImGui::Text("NormalTexture Cache Hit / Miss: %zu / %zu",
                meshRendererStats.normalTextureCacheHitCount,
                meshRendererStats.normalTextureCacheMissCount);
            ImGui::Text("Emissive Mapped Primitives: %zu", meshRendererStats.emissiveMappedPrimitiveCount);
            ImGui::Text("EmissiveMap Fallbacks: %zu", meshRendererStats.emissiveMapFallbackCount);
            ImGui::Text("EmissiveTexture Cache Hit / Miss: %zu / %zu",
                meshRendererStats.emissiveTextureCacheHitCount,
                meshRendererStats.emissiveTextureCacheMissCount);
            const MESHWIREDEBUG::MeshWireDebugStats& wireStats = MESHWIREDEBUG::GetDebugStats();
            ImGui::Separator();
            ImGui::TextUnformatted("Wire Debug:");
            ImGui::Text("Submitted Models / Lines: %zu / %zu", wireStats.submittedModelCount, wireStats.submittedLineCount);
            ImGui::Text("Truncated Models: %zu", wireStats.truncatedModelCount);
            ImGui::Text("Cache Hit / Miss: %zu / %zu", wireStats.cacheHitCount, wireStats.cacheMissCount);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Animation Clips")) {
            ImGui::Text("Animation clips: %zu", asset_->animations.size());
            for (const AnimationClip& clip : asset_->animations) {
                ImGui::BulletText("%s (%.2fs)", clip.name.c_str(), clip.durationSec);
            }
            ImGui::TreePop();
        }
        notifyIfRenderStateChanged();
#endif
    }

} // namespace HIKARI