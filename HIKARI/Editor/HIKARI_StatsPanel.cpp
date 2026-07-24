#include "HIKARI_StatsPanel.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Render2D/HIKARI_DxTexture.h"
#include "HIKARI_Services.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Models/HIKARI_ModelManager.h"
#include "Render3D/Lighting/HIKARI_IblEnvironment.h"
#include "Render3D/Render/HIKARI_ModelRenderer.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"
#include "Scene/HIKARI_World.h"
#include "Vfx/Post/HIKARI_PostSystem.h"
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif
#include "Scene/HIKARI_GameObject.h"

namespace HIKARI {

    void StatsPanel::DrawContents(const char* sceneName, const World& world, const ModelManager& modelManager, const EditorSelection& selection, const Camera3D& camera) const {
#if defined(HIKARI_WITH_EDITOR)
        if (ImGui::TreeNodeEx("Scene", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Scene: %s", sceneName ? sceneName : "<none>");
            ImGui::Text("World Objects: %zu", world.GetObjects().size());
            ImGui::Text("Selected Object: %s", selection.selectedObject ? selection.selectedObject->GetName().c_str() : "<none>");
            ImGui::Text("Selected Asset: %s", selection.selectedAsset ? selection.selectedAsset->GetName().c_str() : "<none>");
            const MATH::Vec3 cameraPos = camera.GetPosition();
            ImGui::Text("Camera Pos: (%.2f, %.2f, %.2f)", cameraPos.x, cameraPos.y, cameraPos.z);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Assets", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Model Assets: %zu", modelManager.GetAssets().size());
            ImGui::Text("Loaded Models: %zu", modelManager.CountLoadedAssets());
            ImGui::Text("Failed Models: %zu", modelManager.CountFailedAssets());
            size_t texturedMaterialCount = 0;
            for (const auto& asset : modelManager.GetAssets()) {
                if (asset &&
                    asset->GetLegacyRuntimeMaterial() &&
                    asset->GetLegacyRuntimeMaterial()->HasBaseColorTexture()) {
                    ++texturedMaterialCount;
                }
            }
            ImGui::Text("Textured Materials: %zu", texturedMaterialCount);
            ImGui::TreePop();
        }

        const MODELRENDERER::ModelRendererDebugStats& modelStats = MODELRENDERER::GetDebugStats();
        const RENDER3D::RUNTIME::SceneRenderCache::Stats& sceneRenderCacheStats =
            RenderSubmissionSystem::GetSceneRenderCacheStats();
        const MESHRENDERER::MeshRendererDebugStats& meshStats = MESHRENDERER::GetDebugStats();
        if (ImGui::TreeNodeEx("Render Summary", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("ModelRenderer Frame: %s", MODELRENDERER::ToString(modelStats.frameKind));
            ImGui::Text("Objects Visible / Hidden / Dirty: %u / %u / %u",
                sceneRenderCacheStats.visibleObjectCount,
                sceneRenderCacheStats.hiddenObjectCount,
                sceneRenderCacheStats.dirtyObjectCount);
            ImGui::Text("Surfaces Total / Static / Dynamic / Skinned: %u / %u / %u / %u",
                sceneRenderCacheStats.surfaceInstanceCount,
                sceneRenderCacheStats.staticSurfaceInstanceCount,
                sceneRenderCacheStats.dynamicSurfaceInstanceCount,
                sceneRenderCacheStats.skinnedSurfaceInstanceCount);
            ImGui::Text("GPU Scene: %s",
                meshStats.surfaceGpuSceneSrvValid && meshStats.surfaceGpuSceneBufferReady ? "Ready" : "Missing");
            ImGui::Text("Texture Loads Deferred: %zu", meshStats.materialTextureLoadDeferredCount);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("GPU Resources", ImGuiTreeNodeFlags_DefaultOpen)) {
            const UINT used = DXTEX::DxTextureManager::GetUsedDescriptorCount();
            const UINT freeCount = DXTEX::DxTextureManager::GetFreeDescriptorCount();
            const UINT maxCount = DXTEX::DxTextureManager::GetMaxDescriptorCount();

            ImGui::Text("Texture Descriptors: %u / %u", used, maxCount);
            ImGui::Text("Free Texture Descriptors: %u", freeCount);
            ImGui::Text("SRV Heap Capacity: %u", GFX::DESCRIPTOR::kSrvHeapCapacity);
            ImGui::Text("System SRV Reserved: %u", GFX::DESCRIPTOR::kSystemSrvReservedCount);
            ImGui::Text(
                "Deferred Releases Pending: %zu",
                SERVICES::gCore.GetPendingDeferredReleaseCount());
            ImGui::Text("SceneColor Ready: %s", POST::PostSystem::IsSceneColorReady() ? "Yes" : "No");
            ImGui::Text("SceneColor Size: %d x %d",
                POST::PostSystem::GetSceneColorWidth(),
                POST::PostSystem::GetSceneColorHeight());
            const IBL::IblEnvironmentData& iblData = IBL::GetEnvironmentData();
            ImGui::Text("IBL Valid: %s", iblData.valid ? "Yes" : "No");
            ImGui::Text("IBL Irradiance / Prefiltered / BRDF: %s / %s / %s",
                iblData.hasIrradiance ? "Yes" : "No",
                iblData.hasPrefiltered ? "Yes" : "No",
                iblData.hasBrdfLut ? "Yes" : "No");
            ImGui::Text("IBL Prefiltered Mips: %u", iblData.prefilteredMipCount);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("PIX")) {
            ImGui::Text("Compiled In: %s", GFX::PIX::IsCompiledIn() ? "Yes" : "No");
            ImGui::Text("Capturer Loaded: %s", GFX::PIX::IsCapturerLoaded() ? "Yes" : "No");
            bool eventMarkersEnabled = GFX::PIX::AreEventMarkersEnabled();
            if (ImGui::Checkbox("Event Markers", &eventMarkersEnabled)) {
                GFX::PIX::SetEventMarkersEnabled(eventMarkersEnabled);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Capture file generation remains available. Enable this only when you need PIX timeline markers.");
            }
            ImGui::TextWrapped("Status: %s", GFX::PIX::GetLastStatusMessage().c_str());
            if (GFX::PIX::HasLastCapture()) {
                ImGui::TextWrapped("Last Capture: %s", GFX::PIX::GetLastCapturePath().generic_string().c_str());
            }
            if (ImGui::Button("Capture Next Frame")) {
                GFX::PIX::CaptureNextFrames(1, true);
            }
            ImGui::SameLine();
            if (ImGui::Button("Open Last") && GFX::PIX::HasLastCapture()) {
                GFX::PIX::OpenLastCaptureInPix();
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Animation / Skinning")) {
            ImGui::Text("Skinned GPU Draws: %zu", meshStats.skinnedGpuDrawCount);
            ImGui::Text("Uploaded Joints: %zu", meshStats.uploadedJointCount);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Lighting")) {
            ImGui::Text("Directional Enabled: %s", meshStats.directionalEnabled ? "Yes" : "No");
            ImGui::Text("Directional Intensity: %.3f", meshStats.directionalIntensity);
            ImGui::Text("Ambient Intensity: %.3f", meshStats.ambientIntensity);
            ImGui::Text("Point Lights Total / Uploaded / Clamped: %zu / %zu / %zu",
                meshStats.pointLightTotalCount,
                meshStats.pointLightUploadedCount,
                meshStats.pointLightClampedCount);
            ImGui::TreePop();
        }
#else
        (void)sceneName;
        (void)world;
        (void)modelManager;
        (void)selection;
        (void)camera;
#endif
    }

} // namespace HIKARI
