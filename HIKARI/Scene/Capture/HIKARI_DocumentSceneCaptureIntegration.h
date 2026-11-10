#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include "RenderContracts/Model/HIKARI_ModelAsset.h"
#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Core/HIKARI_TimeService.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Render/HIKARI_ModelRenderer.h"
#include "RenderCore/Resources/HIKARI_TextureResourceSystem.h"
#include "Render3D/ScreenSpace/HIKARI_ScreenComposite.h"
#include "Render3D/Vfx/HIKARI_ParticleRenderInput.h"
#include "Render3D/WorldSprite/HIKARI_WorldSpriteFrameSource.h"
#include "Render3D/WorldText/HIKARI_WorldTextFrameSource.h"
#include "Scene/Capture/HIKARI_SceneCaptureReadability.h"
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
            // 描画先がまだ無い間（作りたて・大きさを変えて作り直し中の捕捉へ結び直した時）は、貼っている材質を外さず、
            // 古い描画先を参照しない黒い面にして待つ。外して貼り直すと材質が作り直しになり、GPU の場景が組み直しになる。
            GameObject* holder = binding.applied ? world.FindObject(binding.modelObject) : nullptr;
            ModelComponent* holderModel = holder != nullptr ? holder->GetComponent<ModelComponent>() : nullptr;
            Material* held = holderModel != nullptr && binding.resolvedMaterialIndex != kGameProjectInvalidMaterialIndex
                                 ? holderModel->GetRuntimeMaterialOverride(binding.resolvedMaterialIndex)
                                 : nullptr;
            if (held != nullptr && held == binding.appliedMaterial) {
                if (binding.appliedTexture) {
                    held->SetTextureSlot(MaterialTextureUsage::BaseColor, RuntimeTextureSlot{});
                    held->SetBaseColor({0.0f, 0.0f, 0.0f, 1.0f});
                    binding.appliedTexture = {};
                }
                continue;
            }
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
        const auto alphaMode =
            has(GameProjectSceneCaptureMaterialBindingFlag::AlphaMask)    ? RuntimeMaterialAlphaMode::Mask
            : has(GameProjectSceneCaptureMaterialBindingFlag::AlphaBlend) ? RuntimeMaterialAlphaMode::Blend
                                                                          : RuntimeMaterialAlphaMode::Opaque;
        if (ownsAppliedMaterial && appliedMaterial != nullptr) {
            appliedMaterial->SetTextureSlot(MaterialTextureUsage::BaseColor, std::move(textureSlot));
            appliedMaterial->SetMaterialAlphaMode(alphaMode);
            // 描画先を待つ間に黒くしていた面を戻す。
            appliedMaterial->SetBaseColor({1.0f, 1.0f, 1.0f, 1.0f});
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

// 面の (s, t)（bounds の二辺を 0..1 にした座標）から uv0 への一次式を頂点から最小二乗で求め、四隅の UV を出す。
// 平らな四角の画面なら正確。外れの大きい面（曲がった UV や分割した UV）は貼り直さない。
inline bool FitScreenSurfaceUv(const MeshPrimitive& primitive, const float low[3], const float extent[3], int u, int v,
                               std::array<MATH::Vec2, 4>& corners) {
    const auto axis = [](const MATH::Vec3& p, int index) { return index == 0 ? p.x : (index == 1 ? p.y : p.z); };
    double m[3][3]{};
    double bx[3]{}, by[3]{};
    for (const Vertex3D& vertex : primitive.staticVertices) {
        const double s = (axis(vertex.position, u) - low[u]) / extent[u];
        const double t = (axis(vertex.position, v) - low[v]) / extent[v];
        const double row[3]{s, t, 1.0};
        for (int i = 0; i < 3; ++i) {
            for (int j = 0; j < 3; ++j)
                m[i][j] += row[i] * row[j];
            bx[i] += row[i] * vertex.uv0.x;
            by[i] += row[i] * vertex.uv0.y;
        }
    }
    const double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
                       m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                       m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    if (primitive.staticVertices.size() < 3u || std::abs(det) < 1e-12)
        return false;
    const auto solve = [&](const double rhs[3], double out[3]) {
        for (int column = 0; column < 3; ++column) {
            double replaced[3][3]{};
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    replaced[i][j] = j == column ? rhs[i] : m[i][j];
            out[column] = (replaced[0][0] * (replaced[1][1] * replaced[2][2] - replaced[1][2] * replaced[2][1]) -
                           replaced[0][1] * (replaced[1][0] * replaced[2][2] - replaced[1][2] * replaced[2][0]) +
                           replaced[0][2] * (replaced[1][0] * replaced[2][1] - replaced[1][1] * replaced[2][0])) /
                          det;
        }
    };
    double fx[3]{}, fy[3]{};
    solve(bx, fx);
    solve(by, fy);
    for (const Vertex3D& vertex : primitive.staticVertices) {
        const double s = (axis(vertex.position, u) - low[u]) / extent[u];
        const double t = (axis(vertex.position, v) - low[v]) / extent[v];
        if (std::abs(fx[0] * s + fx[1] * t + fx[2] - vertex.uv0.x) > 0.01 ||
            std::abs(fy[0] * s + fy[1] * t + fy[2] - vertex.uv0.y) > 0.01)
            return false;
    }
    constexpr double st[4][2]{{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    for (int corner = 0; corner < 4; ++corner)
        corners[corner] = {static_cast<float>(fx[0] * st[corner][0] + fx[1] * st[corner][1] + fx[2]),
                           static_cast<float>(fy[0] * st[corner][0] + fy[1] * st[corner][1] + fy[2])};
    return true;
}

// 拡大の後に貼り直す画面を集める（ProtectReadability の不透明な結び付けの面）。四隅は CollectCaptureSurfaces と
// 同じ求め方で、各隅の UV には結び付けの UV 変換（切り出し・回転・反転）を材質と同じ式で掛ける。
inline RENDER3D::ScreenCompositeWorldFrame CollectScreenComposites(World& world, SceneCaptureService& captures) {
    RENDER3D::ScreenCompositeWorldFrame frame{};
    const auto* presentation = world.Services().Find<PresentationTransformService>();
    const auto* activity = world.Services().Find<RuntimeObjectActivityService>();
    for (const auto& binding : captures.Bindings()) {
        if (!binding.applied || !binding.appliedTexture.IsValid() || !binding.ShouldCompositeAfterTemporal())
            continue;
        auto* object = world.FindObject(binding.modelObject);
        auto* model = object ? object->GetComponent<ModelComponent>() : nullptr;
        const auto* asset = model ? model->GetModelAsset() : nullptr;
        if (!model || !model->IsVisible() || !asset ||
            (activity && activity->Get(object->GetRuntimeHandle()) == GameProjectRuntimeActivity::SuspendedHidden))
            continue;
        Transform3D transform{};
        if (!world.TryGetWorldTransform(object->GetRuntimeHandle(), transform))
            continue;
        auto worldMatrix = transform.GetWorldMatrix();
        if (presentation)
            (void)presentation->TryGetWorldMatrix(object->GetRuntimeHandle(), worldMatrix);
        RuntimeTextureSlot slot{};
        ApplyMaterialBindingUvTransform(binding, slot);
        const float sine = std::sin(slot.uvRotation), cosine = std::cos(slot.uvRotation);
        const MATH::Vec4 baseColor =
            binding.appliedMaterial ? binding.appliedMaterial->GetBaseColor() : MATH::Vec4{1.0f, 1.0f, 1.0f, 1.0f};
        const auto visibility = model->ResolveRuntimeNodeVisibilityMask(*asset);
        const auto nodeGlobals = BOUNDS::BuildModelNodeGlobals(*asset);
        for (size_t nodeIndex = 0; nodeIndex < asset->nodes.size(); ++nodeIndex) {
            const auto& node = asset->nodes[nodeIndex];
            if (node.meshIndex < 0 || static_cast<size_t>(node.meshIndex) >= asset->meshes.size() ||
                node.skinIndex >= 0 || (!visibility.empty() && !visibility[nodeIndex]))
                continue;
            for (const auto& primitive : asset->meshes[node.meshIndex].primitives) {
                if (primitive.materialIndex != binding.resolvedMaterialIndex ||
                    primitive.layout != VertexLayoutKind::StaticPNTT)
                    continue;
                if (frame.count == RENDER3D::kScreenCompositeCandidateCount)
                    return frame;
                const auto& bounds = primitive.bounds;
                const float low[]{bounds.min.x, bounds.min.y, bounds.min.z};
                const float high[]{bounds.max.x, bounds.max.y, bounds.max.z};
                const float extent[]{high[0] - low[0], high[1] - low[1], high[2] - low[2]};
                const int thin = static_cast<int>(std::min_element(extent, extent + 3) - extent);
                const int u = (thin + 1) % 3, v = (thin + 2) % 3;
                if (extent[u] < .00001f || extent[v] < .00001f ||
                    extent[thin] > (std::min)(extent[u], extent[v]) * .02f)
                    continue;
                std::array<MATH::Vec2, 4> meshUv{};
                if (!FitScreenSurfaceUv(primitive, low, extent, u, v, meshUv))
                    continue;
                RENDER3D::ScreenCompositeWorldRegion& region = frame.regions[frame.count];
                const auto matrix = worldMatrix * nodeGlobals[nodeIndex];
                constexpr float signs[4][2]{{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
                for (uint32_t corner = 0; corner < 4; ++corner) {
                    float p[]{(low[0] + high[0]) * .5f, (low[1] + high[1]) * .5f, (low[2] + high[2]) * .5f};
                    p[u] += extent[u] * .5f * signs[corner][0];
                    p[v] += extent[v] * .5f * signs[corner][1];
                    const auto point = matrix.TransformPoint({p[0], p[1], p[2], 1});
                    region.corners[corner] = {point.x, point.y, point.z};
                    // 材質の UV 変換（HikariApplyMaterialUvTransform と同じ：拡大縮小 → 回転 → ずらし）。
                    const float x = meshUv[corner].x * slot.uvScale.x, y = meshUv[corner].y * slot.uvScale.y;
                    region.uv[corner] = {x * cosine - y * sine + slot.uvOffset.x,
                                         x * sine + y * cosine + slot.uvOffset.y};
                }
                region.texture = binding.appliedTexture;
                region.baseColor = baseColor;
                ++frame.count;
            }
        }
    }
    return frame;
}

inline void RenderSecondaryCapture(DocumentSceneState& state, const GFX::Context& graphics,
                                   const SceneEnvironment& environment,
                                   const RENDER3D::LIGHTING::SceneLightingSet& lighting, const FrameContext& frame,
                                   const RENDER3D::PARTICLES::ParticleRenderFrameSource* particleFrameSource,
                                   const RENDER3D::WORLD_SPRITES::WorldSpriteFrameSource* worldSpriteFrameSource,
                                   const RENDER3D::WORLD_TEXT::WorldTextFrameSource* worldTextFrameSource) {
    SceneCaptureService& service = state.runtime.sceneCaptureService;
    for (const uint32_t retiredSlot : service.ConsumeRetiredCaptureTargets()) {
        MODELRENDERER::ResetSecondarySceneCaptureTarget(retiredSlot);
    }
    const std::vector<SceneCaptureRequest> requests = service.Requests();
    if (requests.empty()) {
        MODELRENDERER::ResetSecondarySceneCaptureTarget();
        return;
    }
    static const auto cpuTimer = CPU_PROFILE::RegisterDynamicPass("Render.SceneCaptures");
    const CPU_PROFILE::ScopedDynamicCpuTimer cpuScope(cpuTimer);
    const GFX::GPU_PROFILE::ScopedGpuTimerGroup gpuScope(graphics.cmdList, GFX::GPU_PROFILE::Pass::SceneCaptures);
    static_assert(kGameProjectSceneCaptureMaximumActiveCount == MESHRENDERER::kSecondarySceneCaptureMaximumCount);
    // 描き方を受けない面（Unstyled の結び付け）。どのキャプチャに写っても同じなので、フレームに一度だけ集める。
    const RENDER3D::ScreenReadabilityWorldFrame unstyledSurfaces = CollectCaptureSurfaces(
        state.runtime.world, service,
        GameProjectSceneCaptureMaterialBindingFlags(GameProjectSceneCaptureMaterialBindingFlag::Unstyled));
    for (const SceneCaptureRequest& request : requests) {
        if (!request.IsEnabled()) {
            continue;
        }
        const uint32_t width =
            std::clamp(request.width, kGameProjectSceneCaptureMinimumExtent, kGameProjectSceneCaptureMaximumExtent);
        const uint32_t height =
            std::clamp(request.height, kGameProjectSceneCaptureMinimumExtent, kGameProjectSceneCaptureMaximumExtent);
        GameObject* cameraObject = state.runtime.world.FindObject(request.cameraObject);
        Camera3D camera{};
        // 並べた視点（SetViews）：視点ごとのカメラで描く。カメラ物体は作成と更新の判定にだけ使う。はみ出す視点は描かない。
        std::vector<MODELRENDERER::SecondarySceneCaptureSubView> subViews{};
        if (!request.views.empty()) {
            subViews.reserve(request.views.size());
            for (const GameProjectSceneCaptureViewDesc& desc : request.views) {
                if (uint64_t(desc.x) + desc.width > width || uint64_t(desc.y) + desc.height > height) {
                    continue;
                }
                MODELRENDERER::SecondarySceneCaptureSubView sub{};
                const MATH::Vec3 eye{desc.eye.x, desc.eye.y, desc.eye.z};
                sub.camera.SetLookAt(eye, {eye.x + desc.forward.x, eye.y + desc.forward.y, eye.z + desc.forward.z},
                                     {desc.up.x, desc.up.y, desc.up.z});
                sub.camera.SetOffCenter(desc.left, desc.right, desc.bottom, desc.top, desc.nearZ, desc.farZ,
                                        desc.orthographic != 0u);
                sub.x = desc.x;
                sub.y = desc.y;
                sub.width = desc.width;
                sub.height = desc.height;
                sub.keepPrevious =
                    (desc.flags & GameProjectSceneCaptureViewFlags(GameProjectSceneCaptureViewFlag::KeepPrevious)) !=
                    0u;
                subViews.push_back(sub);
            }
        }
        if (!subViews.empty()) {
            camera = subViews.front().camera;
        } else if (cameraObject == nullptr || !state.camera.director.TryResolveCameraObject(
                                                  state.runtime.world, cameraObject->GetDocumentId(),
                                                  static_cast<float>(width) / static_cast<float>(height), camera)) {
            service.PublishFailure(request.handle, GameProjectSceneCaptureFailure::InvalidCamera);
            continue;
        }

        GameProjectSceneCaptureSnapshot previous{};
        (void)service.TryGetSnapshot(request.handle, previous);
        RENDER3D::ResolvedCameraFrame captureCamera{};
        captureCamera.camera = camera;
        captureCamera.sourceCameraObjectId = cameraObject != nullptr ? cameraObject->GetDocumentId().value : 0u;
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
        view.surfaceExtensions = state.runtime.world.GetRenderSurfaceExtensions();
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
                graphics, view, environment, lighting, state.lighting.sky, state.runtime.world.GetRuntimeIdentity(),
                request.renderSlot, width, height, (request.flags & renderSkyFlag) != 0u, renderTransparent,
                request.style, output, request.views.empty() ? nullptr : &subViews,
                unstyledSurfaces.count > 0u ? &unstyledSurfaces : nullptr) ||
            !output.ready) {
            service.PublishFailure(request.handle, GameProjectSceneCaptureFailure::RenderFailed);
            continue;
        }
        service.PublishReady(request.handle, output.texture, output.width, output.height, frame.frameIndex,
                             output.outputRevision);
    }
}

} // namespace HIKARI::SCENE_CAPTURE
