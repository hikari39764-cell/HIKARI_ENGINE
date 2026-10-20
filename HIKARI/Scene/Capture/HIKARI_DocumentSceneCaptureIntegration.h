#pragma once

#include <algorithm>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Core/HIKARI_TimeService.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render2D/HIKARI_GameUiEffectRenderer.h"
#include "Render3D/Render/HIKARI_ModelRenderer.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"
#include "Render3D/Vfx/HIKARI_ParticleRenderInput.h"
#include "Render3D/WorldSprite/HIKARI_WorldSpriteFrameSource.h"
#include "Render3D/WorldText/HIKARI_WorldTextFrameSource.h"
#include "Scene/Capture/HIKARI_SceneCaptureService.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/Document/Internal/HIKARI_DocumentSceneState.h"
#include "Scene/HIKARI_GameObject.h"

namespace HIKARI::SCENE_CAPTURE {

inline void ApplyMaterialBindingUvTransform(const SceneCaptureMaterialBinding& binding,
                                            RuntimeTextureSlot& textureSlot) noexcept {
    constexpr float pi = 3.14159265358979323846f;
    const GameProjectSceneCaptureSourceRect& rect = binding.sourceRect;
    const bool flipX = (binding.flags & GameProjectSceneCaptureMaterialBindingFlags(
                                            GameProjectSceneCaptureMaterialBindingFlag::FlipTextureX)) != 0u;
    const bool flipY = (binding.flags & GameProjectSceneCaptureMaterialBindingFlags(
                                            GameProjectSceneCaptureMaterialBindingFlag::FlipTextureY)) != 0u;
    const float signX = flipX ? -1.0f : 1.0f;
    const float signY = flipY ? -1.0f : 1.0f;
    const float biasX = flipX ? 1.0f : 0.0f;
    const float biasY = flipY ? 1.0f : 0.0f;

    switch (binding.quarterTurns % 4u) {
    case 1u:
        textureSlot.uvScale = {
            rect.height * signX,
            rect.width * signY,
        };
        textureSlot.uvOffset = {
            rect.x + rect.width * (1.0f - biasY),
            rect.y + rect.height * biasX,
        };
        textureSlot.uvRotation = pi * 0.5f;
        break;
    case 2u:
        textureSlot.uvScale = {
            rect.width * signX,
            rect.height * signY,
        };
        textureSlot.uvOffset = {
            rect.x + rect.width * (1.0f - biasX),
            rect.y + rect.height * (1.0f - biasY),
        };
        textureSlot.uvRotation = pi;
        break;
    case 3u:
        textureSlot.uvScale = {
            rect.height * signX,
            rect.width * signY,
        };
        textureSlot.uvOffset = {
            rect.x + rect.width * biasY,
            rect.y + rect.height * (1.0f - biasX),
        };
        textureSlot.uvRotation = pi * 1.5f;
        break;
    case 0u:
    default:
        textureSlot.uvScale = {
            rect.width * signX,
            rect.height * signY,
        };
        textureSlot.uvOffset = {
            rect.x + rect.width * biasX,
            rect.y + rect.height * biasY,
        };
        textureSlot.uvRotation = 0.0f;
        break;
    }
}

inline uint32_t ResolveMaterialIndex(const ModelAsset& model, const SceneCaptureMaterialBinding& binding) {
    if (!binding.materialSlotName.empty()) {
        const auto found =
            std::find_if(model.materials.begin(), model.materials.end(),
                         [&](const MaterialAsset& material) { return material.name == binding.materialSlotName; });
        if (found != model.materials.end()) {
            return static_cast<uint32_t>(std::distance(model.materials.begin(), found));
        }
        return kGameProjectInvalidMaterialIndex;
    }
    return binding.fallbackMaterialIndex < model.materials.size() ? binding.fallbackMaterialIndex
                                                                  : kGameProjectInvalidMaterialIndex;
}

inline void ClearMaterialBinding(World& world, const SceneCaptureMaterialBinding& binding) {
    if (!binding.applied || binding.resolvedMaterialIndex == kGameProjectInvalidMaterialIndex) {
        return;
    }
    GameObject* object = world.FindObject(binding.modelObject);
    ModelComponent* model = object != nullptr ? object->GetComponent<ModelComponent>() : nullptr;
    if (model != nullptr &&
        model->GetRuntimeMaterialOverride(binding.resolvedMaterialIndex) == binding.appliedMaterial) {
        model->ClearRuntimeMaterialOverride(binding.resolvedMaterialIndex);
    }
}

inline void ApplyPublishedMaterialBinding(World& world, SceneCaptureService& service) {
    for (const SceneCaptureMaterialBinding& retired : service.ConsumeRetiredBindings()) {
        ClearMaterialBinding(world, retired);
    }
    for (SceneCaptureMaterialBinding& binding : service.Bindings()) {
        const RENDER3D::TextureResourceHandle texture = service.OutputTexture(binding.capture);
        if (!texture || !RENDER3D::IsTextureResourceValid(texture)) {
            ClearMaterialBinding(world, binding);
            binding.resolvedMaterialIndex = kGameProjectInvalidMaterialIndex;
            binding.appliedTexture = {};
            binding.appliedMaterial = nullptr;
            binding.applied = false;
            continue;
        }
        GameObject* object = world.FindObject(binding.modelObject);
        ModelComponent* model = object != nullptr ? object->GetComponent<ModelComponent>() : nullptr;
        const ModelAsset* modelAsset = model != nullptr ? model->GetModelAsset() : nullptr;
        if (model == nullptr || modelAsset == nullptr) {
            continue;
        }
        const uint32_t materialIndex = ResolveMaterialIndex(*modelAsset, binding);
        if (materialIndex == kGameProjectInvalidMaterialIndex) {
            service.PublishFailure(binding.capture, GameProjectSceneCaptureFailure::MaterialBindingFailed);
            continue;
        }
        Material* appliedMaterial = model->GetRuntimeMaterialOverride(materialIndex);
        const bool ownsAppliedMaterial = binding.applied && binding.resolvedMaterialIndex == materialIndex &&
                                         appliedMaterial == binding.appliedMaterial;
        if (ownsAppliedMaterial && binding.appliedTexture == texture && !binding.refreshApplied) {
            continue;
        }
        if (binding.applied && !ownsAppliedMaterial &&
            model->GetRuntimeMaterialOverride(binding.resolvedMaterialIndex) == binding.appliedMaterial) {
            model->ClearRuntimeMaterialOverride(binding.resolvedMaterialIndex);
            appliedMaterial = nullptr;
        }
        RuntimeTextureSlot textureSlot{};
        textureSlot.sourcePath = "runtime://scene-capture";
        textureSlot.resolvedPath = textureSlot.sourcePath;
        textureSlot.resource = texture;
        textureSlot.handle = RENDER3D::GetTextureResourceBackendHandle(texture);
        textureSlot.enabled = textureSlot.handle >= 0;
        ApplyMaterialBindingUvTransform(binding, textureSlot);
        // AlphaBlend の結び付けはキャプチャの alpha で混ぜる半透明の板（背景を透かした風格の重ね）。
        // AlphaMask はキャプチャの alpha で切り抜く不透明面（深度と動きを書く）。
        const auto has = [&](GameProjectSceneCaptureMaterialBindingFlag flag) {
            return (binding.flags & GameProjectSceneCaptureMaterialBindingFlags(flag)) != 0u;
        };
        const auto alphaMode = has(GameProjectSceneCaptureMaterialBindingFlag::AlphaMask)    ? RuntimeMaterialAlphaMode::Mask
                               : has(GameProjectSceneCaptureMaterialBindingFlag::AlphaBlend) ? RuntimeMaterialAlphaMode::Blend
                                                                                              : RuntimeMaterialAlphaMode::Opaque;
        if (ownsAppliedMaterial && appliedMaterial != nullptr) {
            appliedMaterial->SetTextureSlot(MaterialTextureUsage::BaseColor, std::move(textureSlot));
            appliedMaterial->SetMaterialAlphaMode(alphaMode);
        } else {
            auto runtimeMaterial = std::make_unique<Material>();
            runtimeMaterial->SetShaderProfileId("PBR");
            runtimeMaterial->SetBaseColor({1.0f, 1.0f, 1.0f, 1.0f});
            runtimeMaterial->SetUnlit(true);
            runtimeMaterial->SetMetallicFactor(0.0f);
            runtimeMaterial->SetRoughnessFactor(1.0f);
            runtimeMaterial->SetInheritModelTextures(false);
            runtimeMaterial->SetTextureSlot(MaterialTextureUsage::BaseColor, std::move(textureSlot));
            runtimeMaterial->SetMaterialAlphaMode(alphaMode);
            appliedMaterial = runtimeMaterial.get();
            model->SetRuntimeMaterialOverride(materialIndex, std::move(runtimeMaterial));
        }
        binding.resolvedMaterialIndex = materialIndex;
        binding.appliedTexture = texture;
        binding.appliedMaterial = appliedMaterial;
        binding.applied = true;
        binding.refreshApplied = false;
    }
}

inline void RenderSecondaryCapture(DocumentSceneState& state, const GFX::Context& graphics,
                                   const SceneEnvironment& environment,
                                   const RENDER3D::LIGHTING::SceneLightingSet& lighting, const FrameContext& frame,
                                   const RENDER3D::PARTICLES::ParticleRenderFrameSource* particleFrameSource,
                                   const RENDER3D::WORLD_SPRITES::WorldSpriteFrameSource* worldSpriteFrameSource,
                                   const RENDER3D::WORLD_TEXT::WorldTextFrameSource* worldTextFrameSource) {
    SceneCaptureService& service = state.runtime.sceneCaptureService;
    for (const uint32_t retiredSlot : service.ConsumeRetiredCaptureTargets()) {
        if (retiredSlot < kGameProjectSceneCaptureMaximumActiveCount)
            MODELRENDERER::ResetSecondarySceneCaptureTarget(retiredSlot);
        else
            GAME_UI::ResetGameUiCanvasTarget(retiredSlot - kGameProjectSceneCaptureMaximumActiveCount);
    }
    const std::vector<SceneCaptureRequest> requests = service.Requests();
    if (requests.empty()) {
        MODELRENDERER::ResetSecondarySceneCaptureTarget();
        for (uint32_t i = 0; i < kGameProjectUiCanvasMaximumCount; ++i)
            GAME_UI::ResetGameUiCanvasTarget(i);
        return;
    }
    static const auto cpuTimer = CPU_PROFILE::RegisterDynamicPass("Render.SceneCaptures");
    const CPU_PROFILE::ScopedDynamicCpuTimer cpuScope(cpuTimer);
    const GFX::GPU_PROFILE::ScopedGpuTimerGroup gpuScope(graphics.cmdList, GFX::GPU_PROFILE::Pass::SceneCaptures);
    static_assert(kGameProjectSceneCaptureMaximumActiveCount == MESHRENDERER::kSecondarySceneCaptureMaximumCount);
    for (const SceneCaptureRequest& request : requests) {
        if (!request.IsEnabled()) {
            continue;
        }
        const uint32_t width =
            std::clamp(request.width, kGameProjectSceneCaptureMinimumExtent, kGameProjectSceneCaptureMaximumExtent);
        const uint32_t height =
            std::clamp(request.height, kGameProjectSceneCaptureMinimumExtent, kGameProjectSceneCaptureMaximumExtent);
        if (request.uiCanvas) {
            RENDER3D::TextureResourceHandle texture{};
            uint64_t revision{}, renderedFrame{};
            if (request.uiFrame &&
                GAME_UI::RenderGameUiCanvasTarget(request.renderSlot - kGameProjectSceneCaptureMaximumActiveCount,
                                                  state.runtime.world.GetRuntimeIdentity(), *request.uiFrame,
                                                  request.contentRevision, width, height, request.clearColor, texture,
                                                  revision, renderedFrame)) {
                service.PublishReady(request.handle, texture, width, height, renderedFrame, revision);
            } else if (request.contentRevision) {
                service.PublishFailure(request.handle, GameProjectSceneCaptureFailure::RenderFailed);
            }
            continue;
        }
        GameObject* cameraObject = state.runtime.world.FindObject(request.cameraObject);
        Camera3D camera{};
        if (cameraObject == nullptr || !state.camera.director.TryResolveCameraObject(
                                           state.runtime.world, cameraObject->GetDocumentId(),
                                           static_cast<float>(width) / static_cast<float>(height), camera)) {
            service.PublishFailure(request.handle, GameProjectSceneCaptureFailure::InvalidCamera);
            continue;
        }

        GameProjectSceneCaptureSnapshot previous{};
        (void)service.TryGetSnapshot(request.handle, previous);
        RENDER3D::ResolvedCameraFrame captureCamera{};
        captureCamera.camera = camera;
        captureCamera.sourceCameraObjectId = cameraObject->GetDocumentId().value;
        captureCamera.revision = previous.outputRevision + 1u;
        captureCamera.cameraCut = previous.outputRevision == 0u;
        captureCamera.projectionChanged = captureCamera.cameraCut;
        captureCamera.valid = true;
        RENDER3D::RenderViewContext view{};
        view.viewId = RENDER3D::RenderViewId{RENDER3D::kRuntimeSceneCaptureRenderViewId.value + request.renderSlot};
        // Secondary views never dispatch project observers or passes.
        view.purpose = RENDER3D::RenderViewPurpose::Game;
        view.cameraFrame = &captureCamera;
        view.assetRegistry = &state.assets.Registry();
        view.worldSpriteFrameSource = worldSpriteFrameSource;
        view.worldTextFrameSource = worldTextFrameSource;
        const uint32_t renderVfxFlag = GameProjectSceneCaptureFlags(GameProjectSceneCaptureFlag::RenderVfx);
        if ((request.flags & renderVfxFlag) != 0u) {
            view.particleFrameSource = particleFrameSource;
        }
        const uint32_t renderTransparentFlag =
            GameProjectSceneCaptureFlags(GameProjectSceneCaptureFlag::RenderTransparent);
        const bool renderTransparent = (request.flags & (renderTransparentFlag | renderVfxFlag)) != 0u;
        const uint32_t renderSkyFlag = GameProjectSceneCaptureFlags(GameProjectSceneCaptureFlag::RenderSky);
        MODELRENDERER::SecondarySceneCaptureOutput output{};
        if (!MODELRENDERER::RenderSecondarySceneCaptureTarget(
                graphics, view, environment, lighting, state.assets.Models(), state.lighting.sky,
                state.runtime.world.GetRuntimeIdentity(), request.renderSlot, width, height,
                (request.flags & renderSkyFlag) != 0u, renderTransparent, request.style, output) ||
            !output.ready) {
            service.PublishFailure(request.handle, GameProjectSceneCaptureFailure::RenderFailed);
            continue;
        }
        service.PublishReady(request.handle, output.texture, output.width, output.height, frame.frameIndex,
                             output.outputRevision);
    }
}

} // namespace HIKARI::SCENE_CAPTURE
