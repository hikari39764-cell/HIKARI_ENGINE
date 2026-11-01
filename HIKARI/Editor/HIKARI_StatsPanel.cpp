#include "Core/HIKARI_TimeService.h"
#include "Core/Text/HIKARI_Localization.h"
#include "HIKARI_StatsPanel.h"
#include "Diagnostics/HIKARI_MetricRegistry.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/Performance/HIKARI_MetricText.h"
#include "Render2D/HIKARI_DxTexture.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_GfxContext.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneJointPaletteUpload.h"
#include "Assets/Models/HIKARI_ModelManager.h"
#include "Render3D/Lighting/HIKARI_IblEnvironment.h"
#include "Render3D/Render/HIKARI_ModelRenderer.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"
#include "Scene/HIKARI_World.h"
#include "Vfx/Post/HIKARI_PostSystem.h"
#include "imgui.h"
#include "Scene/HIKARI_GameObject.h"

namespace HIKARI {

void StatsPanel::DrawContents(const char* sceneName, const World& world, const ModelManager& modelManager,
                              const EditorSelection& selection, const Camera3D& camera) const {
    if (ImGui::TreeNodeEx(TEXT::LocalizeWindow("menu.scene", "Scene").c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text(TEXT::Localize("editor.ui.scene.s.3fdc78").c_str(), sceneName ? sceneName : "<none>");
        ImGui::Text(TEXT::Localize("editor.ui.world.objects.zu.b9705a").c_str(), world.GetObjects().size());
        ImGui::Text(TEXT::Localize("editor.ui.selected.object.s.9f0bad").c_str(),
                    selection.selectedObject ? selection.selectedObject->GetName().c_str() : "<none>");
        ImGui::Text(TEXT::Localize("editor.ui.selected.asset.s.2e3278").c_str(),
                    selection.selectedAsset ? selection.selectedAsset->GetName().c_str() : "<none>");
        const MATH::Vec3 cameraPos = camera.GetPosition();
        ImGui::Text(TEXT::Localize("editor.ui.camera.pos.2f.2f.2f.76f4ae").c_str(), cameraPos.x, cameraPos.y,
                    cameraPos.z);
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx(TEXT::LocalizeWindow("ui.vfx.text.Assets", "Assets").c_str(),
                          ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text(TEXT::Localize("editor.ui.model.assets.zu.f65174").c_str(), modelManager.GetAssets().size());
        ImGui::Text(TEXT::Localize("editor.ui.loaded.models.zu.999e68").c_str(), modelManager.CountLoadedAssets());
        ImGui::Text(TEXT::Localize("editor.ui.failed.models.zu.5cf3dd").c_str(), modelManager.CountFailedAssets());
        size_t texturedMaterialCount = 0;
        for (const auto& asset : modelManager.GetAssets()) {
            for (const MaterialAsset& material : asset ? asset->materials : std::vector<MaterialAsset>{}) {
                texturedMaterialCount += material.baseColorTexture.textureIndex >= 0 ? 1u : 0u;
            }
        }
        ImGui::Text(TEXT::Localize("editor.ui.textured.materials.zu.6250fa").c_str(), texturedMaterialCount);
        ImGui::TreePop();
    }

    const MODELRENDERER::ModelRendererDebugStats& modelStats = MODELRENDERER::GetDebugStats();
    const RENDER3D::RUNTIME::SceneRenderCache::Stats& sceneRenderCacheStats =
        RenderSubmissionSystem::GetSceneRenderCacheStats();
    const MESHRENDERER::MeshRendererDebugStats& meshStats = MESHRENDERER::GetDebugStats();
    const std::vector<METRIC::MetricReading> metrics = METRIC::Snapshot(TIME::GetFrameContext().frameIndex);
    if (ImGui::TreeNodeEx(TEXT::LocalizeWindow("editor.ui.render.summary.c7fd99", "Render Summary").c_str(),
                          ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text(TEXT::Localize("editor.ui.modelrenderer.frame.s.986990").c_str(),
                    MODELRENDERER::ToString(modelStats.frameKind));
        ImGui::Text(TEXT::Localize("editor.ui.objects.author.visible.hidden.dirty.u.u.u.8c7ece").c_str(),
                    sceneRenderCacheStats.authorVisibleObjectCount, sceneRenderCacheStats.hiddenObjectCount,
                    sceneRenderCacheStats.dirtyObjectCount);
        ImGui::Text(TEXT::Localize("editor.ui.surfaces.total.static.dynamic.skinned.u.u.u.u.f5e424").c_str(),
                    sceneRenderCacheStats.surfaceInstanceCount, sceneRenderCacheStats.staticSurfaceInstanceCount,
                    sceneRenderCacheStats.dynamicSurfaceInstanceCount,
                    sceneRenderCacheStats.skinnedSurfaceInstanceCount);
        ImGui::Text(TEXT::Localize("editor.ui.gpu.scene.s.73d1c0").c_str(),
                    meshStats.gpuSceneUpload.bufferStats.srv.ptr != 0 &&
                            meshStats.gpuSceneUpload.bufferStats.initialized
                        ? "Ready"
                        : "Missing");
        ImGui::Text(TEXT::Localize("editor.stats.textureLoadsDeferred").c_str(),
                    EDITOR::MetricText(METRIC::FindReading(metrics, "render.textureLoadsDeferred"), 0).c_str());
        ImGui::TreePop();
    }

    if (ImGui::TreeNodeEx(TEXT::LocalizeWindow("editor.ui.gpu.resources.ffa5ea", "GPU Resources").c_str(),
                          ImGuiTreeNodeFlags_DefaultOpen)) {
        const UINT used = DXTEX::DxTextureManager::GetUsedDescriptorCount();
        const UINT freeCount = DXTEX::DxTextureManager::GetFreeDescriptorCount();
        const UINT maxCount = DXTEX::DxTextureManager::GetMaxDescriptorCount();

        ImGui::Text(TEXT::Localize("editor.ui.texture.descriptors.u.u.ba7bc2").c_str(), used, maxCount);
        ImGui::Text(TEXT::Localize("editor.ui.free.texture.descriptors.u.be9291").c_str(), freeCount);
        ImGui::Text(TEXT::Localize("editor.ui.srv.heap.capacity.u.64e920").c_str(), GFX::DESCRIPTOR::kSrvHeapCapacity);
        ImGui::Text(TEXT::Localize("editor.ui.system.srv.reserved.u.bf5b5f").c_str(),
                    GFX::DESCRIPTOR::kSystemSrvReservedCount);
        ImGui::Text(TEXT::Localize("editor.ui.deferred.releases.pending.zu.6f9ce3").c_str(),
                    GFX::GetCurrentDeferredReleaseCount());
        const auto sceneColor = POST::PostSystem::GetSceneColorSnapshotView(POST::SceneColorSnapshotKind::Opaque);
        ImGui::Text(TEXT::Localize("editor.ui.scenecolor.ready.s.2356d2").c_str(),
                    sceneColor.IsSampleable() ? "Yes" : "No");
        ImGui::Text(TEXT::Localize("editor.ui.scenecolor.size.u.x.u.2b9027").c_str(), sceneColor.width,
                    sceneColor.height);
        const IBL::IblEnvironmentData& iblData = IBL::GetEnvironmentData();
        ImGui::Text(TEXT::Localize("editor.ui.ibl.valid.s.9f2c45").c_str(), iblData.valid ? "Yes" : "No");
        ImGui::Text(TEXT::Localize("editor.ui.ibl.irradiance.prefiltered.brdf.s.s.s.f149a1").c_str(),
                    iblData.hasIrradiance ? "Yes" : "No", iblData.hasPrefiltered ? "Yes" : "No",
                    iblData.hasBrdfLut ? "Yes" : "No");
        ImGui::Text(TEXT::Localize("editor.ui.ibl.prefiltered.mips.u.ea4118").c_str(), iblData.prefilteredMipCount);
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("PIX")) {
        ImGui::Text(TEXT::Localize("editor.ui.compiled.in.s.150d5b").c_str(), GFX::PIX::IsCompiledIn() ? "Yes" : "No");
        ImGui::Text(TEXT::Localize("editor.ui.capturer.loaded.s.708a29").c_str(),
                    GFX::PIX::IsCapturerLoaded() ? "Yes" : "No");
        bool eventMarkersEnabled = GFX::PIX::AreEventMarkersEnabled();
        if (ImGui::Checkbox(TEXT::LocalizeWindow("editor.ui.event.markers.d8fb71", "Event Markers").c_str(),
                            &eventMarkersEnabled)) {
            GFX::PIX::SetEventMarkersEnabled(eventMarkersEnabled);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                TEXT::Localize("editor.ui.capture.file.generation.remains.available.enable.this.only.when..deee62")
                    .c_str());
        }
        ImGui::TextWrapped(TEXT::Localize("editor.ui.status.s.6ab0dd").c_str(),
                           GFX::PIX::GetLastStatusMessage().c_str());
        if (GFX::PIX::HasLastCapture()) {
            ImGui::TextWrapped(TEXT::Localize("editor.ui.last.capture.s.b6b50d").c_str(),
                               GFX::PIX::GetLastCapturePath().generic_string().c_str());
        }
        if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.capture.next.frame.7f8738", "Capture Next Frame").c_str())) {
            GFX::PIX::CaptureNextFrames(1, true);
        }
        ImGui::SameLine();
        if (ImGui::Button(TEXT::LocalizeWindow("editor.ui.open.last.512ac9", "Open Last").c_str()) &&
            GFX::PIX::HasLastCapture()) {
            GFX::PIX::OpenLastCaptureInPix();
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode(TEXT::LocalizeWindow("editor.ui.animation.skinning.74b194", "Animation / Skinning").c_str())) {
        const RENDER3D::GPUDRIVEN::GpuSceneJointPaletteUploadStats jointUpload =
            RENDER3D::GPUDRIVEN::GetGpuSceneJointPaletteUploadStats(GFX::GetCurrentContext());
        ImGui::Text(TEXT::Localize("editor.stats.skinnedGpuDraws").c_str(),
                    EDITOR::MetricText(METRIC::FindReading(metrics, "render.skinnedGpuDraws"), 0).c_str());
        ImGui::Text(TEXT::Localize("editor.ui.uploaded.joints.zu.201370").c_str(),
                    static_cast<size_t>(jointUpload.matrixPayloadBytes / sizeof(MATH::Mat4)));
        ImGui::TreePop();
    }

    if (ImGui::TreeNode(TEXT::LocalizeWindow("ui.vfx.text.Lighting", "Lighting").c_str())) {
        ImGui::Text(TEXT::Localize("editor.ui.directional.enabled.s.e93ee0").c_str(),
                    meshStats.directionalEnabled ? "Yes" : "No");
        ImGui::Text(TEXT::Localize("editor.ui.directional.intensity.3f.428ab2").c_str(),
                    meshStats.directionalIntensity);
        ImGui::Text(TEXT::Localize("editor.ui.ambient.intensity.3f.115611").c_str(), meshStats.ambientIntensity);
        ImGui::Text(TEXT::Localize("editor.ui.local.lights.source.valid.gpu.rejected.zu.zu.zu.zu.654dcc").c_str(),
                    meshStats.localLightSourceCount, meshStats.localLightUploadableCount,
                    meshStats.localLightUploadedCount, meshStats.localLightClampedCount);
        ImGui::Text(TEXT::Localize("editor.ui.uploaded.point.spot.zu.zu.5d48d7").c_str(),
                    meshStats.pointLightUploadedCount, meshStats.spotLightUploadedCount);
        ImGui::Text(TEXT::Localize("editor.ui.light.clusters.indices.overflow.zu.zu.zu.6900e9").c_str(),
                    meshStats.localLightClusterCount, meshStats.localLightClusterIndexCount,
                    meshStats.localLightClusterOverflowCount);
        ImGui::TreePop();
    }
}

} // namespace HIKARI
