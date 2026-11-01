#include "Scene/HIKARI_RenderSubmissionSystem.h"
#include "Core/IO/HIKARI_PathNormalization.h"

#include "RenderContracts/Asset/HIKARI_AssetRegistry.h"
#include "RenderContracts/Asset/HIKARI_AssetTypes.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "HIKARI/Project/FrameContext.h"
#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "RenderContracts/Model/HIKARI_ModelBounds.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/GpuDriven/HIKARI_MassGpuSceneFrameBuffer.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "RenderContracts/Model/HIKARI_ModelAsset.h"
#include "Render3D/Debug/HIKARI_MeshWireDebugRenderer.h"
#include "Render3D/Debug/HIKARI_Renderer3D_Debug.h"
#include "Assets/Geometry/HIKARI_ProceduralModelFactory.h"
#include "Render3D/Render/HIKARI_ModelRenderer.h"
#include "Assets/Models/HIKARI_ModelManager.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"
#include "Render2D/HIKARI_GameUiEffectRenderer.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelRenderableAssetResolver.h"
#include "Scene/Components/Rendering/WorldSprite/HIKARI_WorldSpriteComponent.h"
#include "Scene/Components/Rendering/WorldText/HIKARI_WorldTextComponent.h"
#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_PresentationTransformService.h"
#include "Scene/Terrain/HIKARI_TerrainGrassModelFactory.h"
#include "Scene/Ui/HIKARI_GameUiPresentationService.h"
#include "Scene/HIKARI_World.h"
#include "Scene/HIKARI_SceneRuntimeBuilder.h"
#include "Scene/Vfx/HIKARI_ActionPostReactionService.h"
#include "Scene/Vfx/HIKARI_CombatAfterimageService.h"
#include "Scene/Vfx/HIKARI_VfxMeshOutput.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"
#include "Scene/Vfx/HIKARI_CombatPresentationService.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"
#include <Vfx/Common/HIKARI_FxTypes.h>

#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace HIKARI {

RenderSubmissionDebugStats RenderSubmissionSystem::sDebugStats_{};
const AssetRegistry* RenderSubmissionSystem::sAssetRegistry_ = nullptr;
ModelManager* RenderSubmissionSystem::sModelManager_ = nullptr;
std::filesystem::path RenderSubmissionSystem::sProjectRoot_{};
RENDER3D::RUNTIME::SceneRenderCache RenderSubmissionSystem::sSceneRenderCache_{};
VFX::VfxMeshInstancePublisher RenderSubmissionSystem::sVfxMeshPublisher_{};
RENDER3D::GPUDRIVEN::GpuSceneRegistry RenderSubmissionSystem::sGpuSceneRegistry_{};
RENDER3D::GPUDRIVEN::MassGpuSceneRegistry RenderSubmissionSystem::sMassGpuSceneRegistry_{};
RENDER3D::GPUDRIVEN::GpuDrivenSceneSource RenderSubmissionSystem::sCombinedGpuSceneSource_{};
RENDER3D::WORLD_SPRITES::WorldSpriteFrameSource RenderSubmissionSystem::sWorldSpriteFrameSource_{};
RENDER3D::WORLD_TEXT::WorldTextFrameSource RenderSubmissionSystem::sWorldTextFrameSource_{};
SceneRenderCacheSync RenderSubmissionSystem::sSceneRenderCacheSync_{};
bool RenderSubmissionSystem::sGpuSceneRegistryValid_ = false;
bool RenderSubmissionSystem::sGpuDrivenSceneSyncInitialized_ = false;
uint64_t RenderSubmissionSystem::sSubmissionWorldIdentity_ = 0u;
uint64_t RenderSubmissionSystem::sPendingMassPublicationGeneration_ = 0u;
uint64_t RenderSubmissionSystem::sPendingMassPublicationFrameIndex_ = 0u;
uint64_t RenderSubmissionSystem::sPendingMassSourceVersion_ = 0u;
RENDER3D::RenderViewPurpose RenderSubmissionSystem::sRenderViewPurpose_ = RENDER3D::RenderViewPurpose::Game;

const char* ToString(RenderSubmissionRouteMode mode) {
    switch (mode) {
    case RenderSubmissionRouteMode::GpuDrivenMainline: return "GPU Driven Mainline";
    default: return "Unknown";
    }
}

namespace {
std::filesystem::path NormalizeProjectRoot(std::filesystem::path projectRoot) {
    return projectRoot.empty() ? std::filesystem::path{} : projectRoot.lexically_normal();
}

void TickPresentationServices(World& world, const FrameContext& frame) noexcept {
    try {
        // Playback is a world-owned service, not a component-owned
        // slot system. Tick it here so public VfxView users still
        // advance/render Owner or World effects without a
        // VfxPlayerComponent. VfxPlayerSystem may also call Update
        // earlier in this frame; the service de-duplicates the frame
        // token and preserves one deterministic step.
        if (auto* playback = world.Services().Find<VFX::VfxPlaybackService>()) {
            playback->Update(frame.frameIndex, frame.gameDt);
        }
        if (auto* combat = world.Services().Find<VFX::CombatPresentationService>()) {
            combat->Tick(frame.frameIndex, frame.unscaledDt);
        }
        if (auto* afterimage = world.Services().Find<VFX::CombatAfterimageService>()) {
            afterimage->Tick(frame.frameIndex, frame.unscaledDt);
        }
        if (auto* reaction = world.Services().Find<VFX::ActionPostReactionService>()) {
            reaction->Tick(frame.unscaledDt, frame.frameIndex);
        }
        if (auto* gameUi = world.Services().Find<GAME_UI::GameUiPresentationService>()) {
            gameUi->Tick(frame.frameIndex);
            GAME_UI::SetGameUiFrameSource(&gameUi->GetFrameSource());
        }
    } catch (...) {
    }
}

void SubmitDebugOverlays(World& world, RenderSubmissionDebugStats& stats) {
    MESHWIREDEBUG::BeginFrame();
    world.ForEachObjectWith<ModelComponent>([&](GameObject& object, ModelComponent& model) {
        if (!model.IsVisible()) {
            return;
        }

        const ModelRenderDebugMode debugMode = model.GetRenderDebugMode();
        if (debugMode == ModelRenderDebugMode::Normal) {
            return;
        }

        const ModelAsset* asset = ResolveRenderableModelAsset(object, model);
        if (asset == nullptr) {
            return;
        }

        const Transform3D& worldTransform = object.GetTransform();
        if (debugMode == ModelRenderDebugMode::WireOverlay || debugMode == ModelRenderDebugMode::WireOnly) {
            const std::vector<uint8_t> nodeVisibilityMask = model.ResolveRuntimeNodeVisibilityMask(*asset);
            MESHWIREDEBUG::SubmitModelWire(*asset, worldTransform, model.GetWireColor(), model.GetMaxWireLines(),
                                           model.GetWirePerPrimitiveColor(), nodeVisibilityMask);
            ++stats.fallbackWireCount;
        } else if (debugMode == ModelRenderDebugMode::BoundsOnly) {
            MESHWIREDEBUG::SubmitModelBounds(*asset, worldTransform, model.GetWireColor());
            ++stats.fallbackWireCount;
        }
    });
}

Bounds ResolveWorldSpriteBounds(const RENDER3D::WORLD_SPRITES::WorldSpriteInstance& instance,
                                const MATH::Mat4& worldMatrix) noexcept {
    const float width = instance.size.x;
    const float height = instance.size.y;
    if (instance.facing == RENDER3D::WORLD_SPRITES::WorldSpriteFacingMode::Fixed) {
        const float left = -instance.pivot.x * width;
        const float right = (1.0f - instance.pivot.x) * width;
        const float bottom = -instance.pivot.y * height;
        const float top = (1.0f - instance.pivot.y) * height;
        const std::array corners{
            MATH::Vec3{left, bottom, 0.0f},
            MATH::Vec3{right, bottom, 0.0f},
            MATH::Vec3{left, top, 0.0f},
            MATH::Vec3{right, top, 0.0f},
        };
        Bounds bounds{{(std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)(),
                       (std::numeric_limits<float>::max)()},
                      {(std::numeric_limits<float>::lowest)(), (std::numeric_limits<float>::lowest)(),
                       (std::numeric_limits<float>::lowest)()}};
        for (const MATH::Vec3& corner : corners) {
            const MATH::Vec3 point = MATH::TransformPoint(worldMatrix, corner);
            bounds.min.x = (std::min)(bounds.min.x, point.x);
            bounds.min.y = (std::min)(bounds.min.y, point.y);
            bounds.min.z = (std::min)(bounds.min.z, point.z);
            bounds.max.x = (std::max)(bounds.max.x, point.x);
            bounds.max.y = (std::max)(bounds.max.y, point.y);
            bounds.max.z = (std::max)(bounds.max.z, point.z);
        }
        constexpr float depthEpsilon = 0.01f;
        bounds.min.x -= depthEpsilon;
        bounds.min.y -= depthEpsilon;
        bounds.min.z -= depthEpsilon;
        bounds.max.x += depthEpsilon;
        bounds.max.y += depthEpsilon;
        bounds.max.z += depthEpsilon;
        return bounds;
    }

    const float scaledWidth = width * (std::max)(std::abs(instance.worldScale.x), 0.001f);
    const float scaledHeight = height * (std::max)(std::abs(instance.worldScale.y), 0.001f);
    const float pivotExtentX = (std::max)(instance.pivot.x, 1.0f - instance.pivot.x) * scaledWidth;
    const float pivotExtentY = (std::max)(instance.pivot.y, 1.0f - instance.pivot.y) * scaledHeight;
    const float radius = std::sqrt(pivotExtentX * pivotExtentX + pivotExtentY * pivotExtentY) + 0.01f;
    return {instance.worldPosition - MATH::Vec3{radius, radius, radius},
            instance.worldPosition + MATH::Vec3{radius, radius, radius}};
}

Bounds ResolveWorldTextBounds(const RENDER3D::WORLD_TEXT::WorldTextInstance& instance,
                              const MATH::Mat4& worldMatrix) noexcept {
    const float width = instance.layoutSize.x;
    const float height = instance.layoutSize.y;
    if (instance.facing == RENDER3D::WORLD_TEXT::WorldTextFacingMode::Fixed) {
        const float left = -instance.pivot.x * width;
        const float right = (1.0f - instance.pivot.x) * width;
        const float bottom = -instance.pivot.y * height;
        const float top = (1.0f - instance.pivot.y) * height;
        const std::array corners{
            MATH::Vec3{left, bottom, 0.0f},
            MATH::Vec3{right, bottom, 0.0f},
            MATH::Vec3{left, top, 0.0f},
            MATH::Vec3{right, top, 0.0f},
        };
        Bounds bounds{{(std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)(),
                       (std::numeric_limits<float>::max)()},
                      {(std::numeric_limits<float>::lowest)(), (std::numeric_limits<float>::lowest)(),
                       (std::numeric_limits<float>::lowest)()}};
        for (const MATH::Vec3& corner : corners) {
            const MATH::Vec3 point = MATH::TransformPoint(worldMatrix, corner);
            bounds.min.x = (std::min)(bounds.min.x, point.x);
            bounds.min.y = (std::min)(bounds.min.y, point.y);
            bounds.min.z = (std::min)(bounds.min.z, point.z);
            bounds.max.x = (std::max)(bounds.max.x, point.x);
            bounds.max.y = (std::max)(bounds.max.y, point.y);
            bounds.max.z = (std::max)(bounds.max.z, point.z);
        }
        constexpr float depthEpsilon = 0.01f;
        bounds.min = bounds.min - MATH::Vec3{depthEpsilon, depthEpsilon, depthEpsilon};
        bounds.max = bounds.max + MATH::Vec3{depthEpsilon, depthEpsilon, depthEpsilon};
        return bounds;
    }

    const float scaledWidth = width * (std::max)(std::abs(instance.worldScale.x), 0.001f);
    const float scaledHeight = height * (std::max)(std::abs(instance.worldScale.y), 0.001f);
    const float pivotExtentX = (std::max)(instance.pivot.x, 1.0f - instance.pivot.x) * scaledWidth;
    const float pivotExtentY = (std::max)(instance.pivot.y, 1.0f - instance.pivot.y) * scaledHeight;
    const float radius = std::sqrt(pivotExtentX * pivotExtentX + pivotExtentY * pivotExtentY) + 0.01f;
    return {instance.worldPosition - MATH::Vec3{radius, radius, radius},
            instance.worldPosition + MATH::Vec3{radius, radius, radius}};
}

bool ResolveWorldTextFontPath(const std::filesystem::path& projectRoot, std::string_view fontAssetPath,
                              std::filesystem::path& outPath) {
    outPath.clear();
    if (projectRoot.empty() || fontAssetPath.empty()) {
        return false;
    }
    try {
        const std::filesystem::path relative = TEXT::PathFromUtf8(fontAssetPath);
        if (relative.is_absolute() || relative.has_root_path() || PATHS::HasParentTraversal(relative)) {
            return false;
        }
        const std::array roots{projectRoot / "Assets", projectRoot / "RuntimePayload" / "Assets"};
        for (const auto& requestedRoot : roots) {
            std::error_code error{};
            std::filesystem::path root = std::filesystem::weakly_canonical(requestedRoot, error);
            if (error) {
                root = requestedRoot.lexically_normal();
            }
            error.clear();
            const std::filesystem::path candidate = std::filesystem::weakly_canonical(root / relative, error);
            if (error || !PATHS::IsLexicallyWithin(root, candidate)) {
                continue;
            }
            error.clear();
            if (!std::filesystem::is_regular_file(candidate, error) || error) {
                continue;
            }
            outPath = candidate;
            return true;
        }
    } catch (...) {
        outPath.clear();
    }
    return false;
}

void BuildWorldSpriteFrameSource(World& world, const FrameContext& frame, const AssetRegistry* assetRegistry,
                                 RENDER3D::WORLD_SPRITES::WorldSpriteFrameSource& outSource) {
    outSource.Clear();
    outSource.assetRegistry = assetRegistry;
    outSource.worldIdentity = world.GetRuntimeIdentity();
    outSource.frameIndex = frame.frameIndex;
    const auto* presentation = world.Services().Find<PresentationTransformService>();
    uint32_t stableOrder = 0u;
    world.ForEachObjectWith<WorldSpriteComponent>([&](GameObject& object, WorldSpriteComponent& sprite) {
        if (!sprite.IsVisible() || sprite.IsCanvasSourceOnly()) {
            return;
        }
        RENDER3D::WORLD_SPRITES::WorldSpriteInstance instance{};
        instance.objectId = object.GetRenderStableId();
        instance.stableOrder = stableOrder++;
        instance.tint = sprite.GetTint();
        instance.opacity = sprite.GetOpacity();
        instance.alphaCutoff = sprite.GetAlphaCutoff();
        instance.transparentSortPriority = sprite.GetTransparentSortPriority();
        instance.facing = sprite.GetFacingMode();
        instance.alphaMode = sprite.GetAlphaMode();
        instance.lit = sprite.IsLit();
        instance.roughness = sprite.GetRoughness();
        instance.specular = sprite.GetSpecular();
        instance.castShadow = sprite.CastsShadow();
        instance.receiveShadow = sprite.ReceivesShadow();

        auto worldMatrix = object.GetTransform().GetWorldMatrix();
        if (presentation != nullptr)
            (void)presentation->TryGetWorldMatrix(object.GetRuntimeHandle(), worldMatrix);
        if (!MATH::DecomposeTRS(worldMatrix, instance.worldPosition, instance.worldRotation, instance.worldScale)) {
            instance.worldPosition = MATH::TransformPoint(worldMatrix, {});
            instance.worldRotation = MATH::Quat::Identity();
            instance.worldScale = {1.0f, 1.0f, 1.0f};
        }
        WorldSpriteRenderProjection projection{};
        if (!sprite.ResolveRenderProjection(instance.worldScale, projection) || projection.empty) {
            return;
        }
        instance.size = projection.size;
        instance.pivot = projection.pivot;
        instance.textureAssetId = std::move(projection.textureAssetId);
        instance.normalTextureAssetId = std::move(projection.normalTextureAssetId);
        instance.preserveAspect = projection.preserveAspect;
        instance.uvRect = projection.uvRect;
        instance.sourceCanvasRect = projection.sourceCanvasRect;
        instance.rotated = projection.rotated;
        instance.flipX = projection.flipX;
        instance.pointSampling = projection.pointSampling;
        if (sprite.IsMaterialEnabled()) {
            const auto& binding = sprite.GetSpriteMaterialBinding();
            if (binding != nullptr && binding->shaderArtifact != nullptr) {
                instance.spriteShaderArtifact = binding->shaderArtifact;
                const auto& parameterSlots = sprite.GetEffectiveMaterialParameterSlots();
                for (size_t slotIndex = 0u; slotIndex < parameterSlots.size(); ++slotIndex) {
                    const auto& slot = parameterSlots[slotIndex];
                    instance.spriteMaterialParameters[slotIndex] = {slot[0], slot[1], slot[2], slot[3]};
                }
            }
        }
        auto boundsInstance = instance;
        if (instance.preserveAspect && assetRegistry != nullptr && !instance.textureAssetId.value.empty()) {
            const auto* texture = assetRegistry->FindAs<TextureAssetDescriptor>(instance.textureAssetId);
            if (texture != nullptr && texture->width != 0u && texture->height != 0u) {
                const float scaleX = (std::max)(std::abs(instance.worldScale.x), 0.001f);
                const float scaleY = (std::max)(std::abs(instance.worldScale.y), 0.001f);
                boundsInstance.size.x = instance.size.y * scaleY / scaleX * static_cast<float>(texture->width) /
                                        static_cast<float>(texture->height) * instance.uvRect.z / instance.uvRect.w;
            }
        }
        instance.worldBounds = ResolveWorldSpriteBounds(boundsInstance, worldMatrix);
        outSource.instances.push_back(std::move(instance));
    });
}

void BuildWorldTextFrameSource(World& world, const FrameContext& frame, const std::filesystem::path& projectRoot,
                               RENDER3D::WORLD_TEXT::WorldTextFrameSource& outSource) {
    static const auto timer = CPU_PROFILE::RegisterDynamicPass("Scene.WorldTextSource");
    const CPU_PROFILE::ScopedDynamicCpuTimer scope(timer);
    constexpr size_t kMaximumWorldTextCount = 4096u;
    outSource.Clear();
    outSource.worldIdentity = world.GetRuntimeIdentity();
    outSource.frameIndex = frame.frameIndex;
    std::unordered_map<std::string, std::filesystem::path> resolvedFonts{};
    uint32_t stableOrder = 0u;
    world.ForEachObjectWith<WorldTextComponent>([&](GameObject& object, WorldTextComponent& text) {
        if (!text.IsVisible() || text.IsCanvasSourceOnly() || text.GetText().empty() ||
            outSource.instances.size() >= kMaximumWorldTextCount) {
            return;
        }
        const auto* store = world.Services().Find<TextAssetStore>();
        const auto appearance = text.ResolveAppearance(store);
        const std::string fontAssetPath =
            appearance.font.IsValid() && store ? store->ResolveFont(appearance.font) : text.GetFontAssetPath();

        auto found = resolvedFonts.find(fontAssetPath);
        if (found == resolvedFonts.end()) {
            std::filesystem::path resolved{};
            if (!ResolveWorldTextFontPath(projectRoot, fontAssetPath, resolved)) {
                return;
            }
            found = resolvedFonts.emplace(fontAssetPath, std::move(resolved)).first;
        }

        RENDER3D::WORLD_TEXT::WorldTextInstance instance{};
        instance.objectId = object.GetRenderStableId();
        instance.stableOrder = stableOrder++;
        instance.absoluteFontAssetPath = TEXT::PathToUtf8(found->second);
        instance.fontRevision = store ? store->FontRevision(found->second) : 0;
        instance.utf8Text = text.GetText();
        instance.layoutSize = text.GetLayoutSize();
        instance.pivot = text.GetPivot();
        instance.color = {appearance.color.r, appearance.color.g, appearance.color.b};
        instance.outlineColor = {appearance.effects.outline.r, appearance.effects.outline.g,
                                 appearance.effects.outline.b};
        instance.fontHeight = text.GetFontHeight();
        instance.lineHeightMultiplier = appearance.effects.lineHeight;
        instance.letterSpacing = appearance.effects.letterSpacing / appearance.pixelHeight * text.GetFontHeight();
        instance.outlineWidth = appearance.effects.outlineWidth / appearance.pixelHeight;
        instance.opacity = appearance.color.a * text.GetOpacity();
        instance.revealAmount = text.GetRevealAmount();
        instance.transparentSortPriority = text.GetTransparentSortPriority();
        instance.horizontalAlign = text.GetHorizontalAlign();
        instance.verticalAlign = text.GetVerticalAlign();
        instance.wrap = text.GetWrapMode();
        instance.facing = text.GetFacingMode();

        const MATH::Mat4 worldMatrix = object.GetTransform().GetWorldMatrix();
        if (!MATH::DecomposeTRS(worldMatrix, instance.worldPosition, instance.worldRotation, instance.worldScale)) {
            instance.worldPosition = MATH::TransformPoint(worldMatrix, {});
            instance.worldRotation = MATH::Quat::Identity();
            instance.worldScale = {1.0f, 1.0f, 1.0f};
        }
        instance.worldBounds = ResolveWorldTextBounds(instance, worldMatrix);
        outSource.instances.push_back(std::move(instance));
    });
}

struct ResolvedMassRenderModel {
    const ModelAsset* model = nullptr;
    std::string clusteredGeometryPath{};
};

bool ResolveMassRenderModel(const GameProjectWorldMassRenderProxyDesc& proxy, const AssetRegistry* assetRegistry,
                            ModelManager* modelManager, const std::filesystem::path& projectRoot,
                            ResolvedMassRenderModel& outResolved) {
    outResolved = {};
    ProceduralMeshSettings proceduralSettings{};
    bool procedural = false;
    switch (proxy.geometry) {
    case GameProjectWorldMassRenderGeometry::BuiltInBox:
        proceduralSettings.kind = ProceduralMeshKind::Box;
        procedural = true;
        break;
    case GameProjectWorldMassRenderGeometry::BuiltInSphere:
        proceduralSettings.kind = ProceduralMeshKind::Sphere;
        proceduralSettings.sphereSlices = 12u;
        proceduralSettings.sphereStacks = 8u;
        procedural = true;
        break;
    case GameProjectWorldMassRenderGeometry::ModelAsset: break;
    default: return false;
    }
    if (procedural) {
        if (PROCEDURAL::TryGetWarmedClusteredGeometry(proceduralSettings, projectRoot, outResolved.model,
                                                      outResolved.clusteredGeometryPath)) {
            return outResolved.model != nullptr && !outResolved.clusteredGeometryPath.empty();
        }
        PROCEDURAL::RequestClusteredGeometryWarmUp(proceduralSettings, projectRoot);
        return false;
    }
    if (assetRegistry == nullptr || modelManager == nullptr || proxy.modelAssetGuid[0] == '\0') {
        return false;
    }
    const std::string assetGuid(proxy.modelAssetGuid);
    const auto* descriptor = assetRegistry->FindAs<ModelAssetDescriptor>(AssetGuid{assetGuid});
    if (descriptor == nullptr) {
        return false;
    }
    outResolved.model = ResolveModelAssetNow(*modelManager, *assetRegistry, descriptor->id.value);
    if (outResolved.model == nullptr || outResolved.model->GetState() != ModelAsset::State::Loaded ||
        descriptor->clusteredGeometryPath.empty()) {
        outResolved = {};
        return false;
    }
    std::filesystem::path clusterPath = descriptor->clusteredGeometryPath;
    if (!clusterPath.is_absolute() && !projectRoot.empty()) {
        clusterPath = projectRoot / clusterPath;
    }
    outResolved.clusteredGeometryPath = clusterPath.lexically_normal().generic_string();
    return !outResolved.clusteredGeometryPath.empty();
}

struct MassRenderSyncContext {
    RENDER3D::GPUDRIVEN::MassGpuSceneRegistry* registry = nullptr;
    const AssetRegistry* assetRegistry = nullptr;
    ModelManager* modelManager = nullptr;
    RENDER3D::RUNTIME::RenderModelCache* renderModelCache = nullptr;
    const std::filesystem::path* projectRoot = nullptr;
    uint64_t worldIdentity = 0u;
    uint64_t frameIndex = 0u;
};

bool SyncMassRenderStores(void* rawContext, const WorldMassRenderPublicationReceipt& receipt,
                          std::span<const WorldMassRenderStoreView> stores) noexcept {
    try {
        auto& context = *static_cast<MassRenderSyncContext*>(rawContext);
        if (context.registry == nullptr || context.renderModelCache == nullptr || context.worldIdentity == 0u ||
            receipt.worldIdentity != context.worldIdentity || receipt.frameIndex != context.frameIndex ||
            receipt.generation == 0u) {
            return false;
        }
        context.registry->BeginSync(context.worldIdentity, context.frameIndex, receipt.generation);
        for (const WorldMassRenderStoreView& store : stores) {
            if (store.proxy == nullptr) {
                context.registry->Clear();
                return false;
            }
            ResolvedMassRenderModel resolved{};
            if (context.projectRoot == nullptr ||
                !ResolveMassRenderModel(*store.proxy, context.assetRegistry, context.modelManager, *context.projectRoot,
                                        resolved) ||
                !context.registry->AddStore(RENDER3D::GPUDRIVEN::MassGpuSceneStoreInput{
                    {store.typeId.value, resolved.model, store.proxy->meshIndex, store.proxy->primitiveIndex,
                     store.proxy->materialIndex,
                     (store.proxy->flags & static_cast<uint32_t>(GameProjectWorldMassRenderFlags::ReceiveShadow)) != 0u,
                     resolved.clusteredGeometryPath},
                    store.instances,
                    store.dirtyRanges,
                    store.layoutVersion,
                    store.sourceVersion,
                    store.dirtyBaseSourceVersion})) {
                context.registry->Clear();
                return false;
            }
        }
        return context.registry->EndSync(*context.renderModelCache);
    } catch (...) {
        auto& context = *static_cast<MassRenderSyncContext*>(rawContext);
        if (context.registry != nullptr) {
            context.registry->Clear();
        }
        return false;
    }
}
} // namespace

void RenderSubmissionSystem::SetAssetContext(const AssetRegistry* assetRegistry, std::filesystem::path projectRoot,
                                             ModelManager* modelManager) {
    std::filesystem::path normalizedProjectRoot = NormalizeProjectRoot(std::move(projectRoot));
    // 初回描画時のパイプライン初期化とプロシージャル既定プリミティブの
    // ウォームアップを、ユーザー操作（プリミティブ作成）から切り離す。
    if (!normalizedProjectRoot.empty()) {
        (void)MESHRENDERER::EnsureReadyForFirstFrame(GFX::GetCurrentContext());
        PROCEDURAL::WarmUpDefaultPrimitiveClusters(normalizedProjectRoot);
    }
    const bool assetContextChanged = sAssetRegistry_ != assetRegistry;
    const bool modelManagerChanged = sModelManager_ != modelManager;
    const bool projectRootChanged = sProjectRoot_ != normalizedProjectRoot;
    if (!assetContextChanged && !modelManagerChanged && !projectRootChanged) {
        return;
    }

    sAssetRegistry_ = assetRegistry;
    sModelManager_ = modelManager;
    sProjectRoot_ = std::move(normalizedProjectRoot);
    InvalidateSceneResources(projectRootChanged);
}

void RenderSubmissionSystem::SetRenderViewPurpose(RENDER3D::RenderViewPurpose purpose) {
    if (sRenderViewPurpose_ == purpose) {
        return;
    }
    sRenderViewPurpose_ = purpose;
    InvalidateSubmissionRouting();
}

RENDER3D::RenderViewPurpose RenderSubmissionSystem::GetRenderViewPurpose() { return sRenderViewPurpose_; }

void RenderSubmissionSystem::InvalidateSubmissionRouting() {
    MESHRENDERER::SetGpuDrivenSceneSource(nullptr);
    SHADOW::SetGpuDrivenSceneSource(nullptr);
    sSceneRenderCache_.Clear();
    sVfxMeshPublisher_.Reset();
    sSceneRenderCacheSync_.Clear();
    sGpuSceneRegistry_.Clear();
    sMassGpuSceneRegistry_.Clear();
    sCombinedGpuSceneSource_.Reset();
    sWorldSpriteFrameSource_.Clear();
    sWorldTextFrameSource_.Clear();
    sGpuSceneRegistryValid_ = false;
    sGpuDrivenSceneSyncInitialized_ = false;
    sSubmissionWorldIdentity_ = 0u;
    sPendingMassPublicationGeneration_ = 0u;
    sPendingMassPublicationFrameIndex_ = 0u;
    sPendingMassSourceVersion_ = 0u;
}

void RenderSubmissionSystem::InvalidateSurfaceResourceBindings() { InvalidateSubmissionRouting(); }

void RenderSubmissionSystem::InvalidateSceneResources(bool clearProceduralCache) {
    MODELRENDERER::GetRenderModelCache().Clear();
    InvalidateSubmissionRouting();
    MESHRENDERER::InvalidateMaterialFxPipelineCache();
    SHADOW::InvalidateSceneCache();
    if (clearProceduralCache) {
        PROCEDURAL::ClearCache();
        TERRAIN::ClearTerrainGrassModelCache();
    }
}

const RenderSubmissionDebugStats& RenderSubmissionSystem::GetDebugStats() { return sDebugStats_; }

const RENDER3D::RUNTIME::SceneRenderCache& RenderSubmissionSystem::GetSceneRenderCache() { return sSceneRenderCache_; }

const RENDER3D::RUNTIME::SceneRenderCache::Stats& RenderSubmissionSystem::GetSceneRenderCacheStats() {
    return sSceneRenderCache_.GetStats();
}

const RENDER3D::GPUDRIVEN::GpuSceneRegistryStats& RenderSubmissionSystem::GetGpuSceneRegistryStats() {
    return sGpuSceneRegistry_.GetStats();
}

const RENDER3D::WORLD_SPRITES::WorldSpriteFrameSource& RenderSubmissionSystem::GetWorldSpriteFrameSource() {
    return sWorldSpriteFrameSource_;
}

const RENDER3D::WORLD_TEXT::WorldTextFrameSource& RenderSubmissionSystem::GetWorldTextFrameSource() {
    return sWorldTextFrameSource_;
}

void RenderSubmissionSystem::PreRender(World& world, const FrameContext& frame) {
    CPU_PROFILE::ScopedCpuTimer cpuTimer(CPU_PROFILE::Pass::RenderSubmission);
    sDebugStats_.submittedModelCount = 0;
    sDebugStats_.scannedModelCount = 0;
    sDebugStats_.hiddenModelCount = 0;
    sDebugStats_.culledModelCount = 0;
    sDebugStats_.missingBoundsCount = 0;
    sDebugStats_.skinnedCullSkippedCount = 0;
    sDebugStats_.fallbackWireCount = 0;
    sDebugStats_.gpuDrivenForwardBypassCount = 0;
    sDebugStats_.gpuDrivenShadowBypassCount = 0;
    sDebugStats_.runtimeSpecialModelCount = 0;
    sDebugStats_.runtimeSpecialForwardModelCount = 0;
    sDebugStats_.runtimeSpecialShadowModelCount = 0;
    sDebugStats_.massRenderStoreCount = 0u;
    sDebugStats_.massRenderInstanceCount = 0u;
    sDebugStats_.massRenderDirtyRangeCount = 0u;
    sDebugStats_.massRenderMissingTemplateCount = 0u;
    sDebugStats_.massRenderUnsupportedTemplateCount = 0u;
    sDebugStats_.routeMode = RenderSubmissionRouteMode::GpuDrivenMainline;
    sDebugStats_.gpuDrivenMainRouteActive = true;

    if (sSubmissionWorldIdentity_ != world.GetRuntimeIdentity()) {
        InvalidateSubmissionRouting();
        sSubmissionWorldIdentity_ = world.GetRuntimeIdentity();
    }

    {
        static const auto timer = CPU_PROFILE::RegisterDynamicPass("Scene.PresentationSources");
        const CPU_PROFILE::ScopedDynamicCpuTimer scope(timer);
        TickPresentationServices(world, frame);
        BuildWorldSpriteFrameSource(world, frame, sAssetRegistry_, sWorldSpriteFrameSource_);
        BuildWorldTextFrameSource(world, frame, sProjectRoot_, sWorldTextFrameSource_);
    }

    {
        static const auto timer = CPU_PROFILE::RegisterDynamicPass("Scene.RenderCacheSync");
        const CPU_PROFILE::ScopedDynamicCpuTimer scope(timer);
        if (!sGpuDrivenSceneSyncInitialized_) {
            sSceneRenderCacheSync_.Sync(world, MODELRENDERER::GetRenderModelCache(), sSceneRenderCache_,
                                        frame.frameIndex, sAssetRegistry_, sProjectRoot_, sRenderViewPurpose_,
                                        sModelManager_);
            sGpuDrivenSceneSyncInitialized_ = true;
        } else {
            sSceneRenderCacheSync_.SyncDirty(world, MODELRENDERER::GetRenderModelCache(), sSceneRenderCache_,
                                             frame.frameIndex, sAssetRegistry_, sProjectRoot_, sRenderViewPurpose_,
                                             sModelManager_);
        }
    }

    {
        static const auto timer = CPU_PROFILE::RegisterDynamicPass("Vfx.MeshOutput");
        const CPU_PROFILE::ScopedDynamicCpuTimer scope(timer);
        // VFX の Mesh 出力は world 由来の同期が終わった直後に差し込み、
        // 同じフレームの GPU scene へ載せる。world 側の所有物は触らない。
        if (const auto* vfxPlayback = world.Services().Find<VFX::VfxPlaybackService>()) {
            if (sAssetRegistry_ != nullptr && sModelManager_ != nullptr) {
                sVfxMeshPublisher_.Publish(*vfxPlayback, sSceneRenderCache_, MODELRENDERER::GetRenderModelCache(),
                                           *sAssetRegistry_, *sModelManager_, sProjectRoot_);
            } else {
                sVfxMeshPublisher_.ClearPublished(sSceneRenderCache_);
            }
        } else {
            sVfxMeshPublisher_.ClearPublished(sSceneRenderCache_);
        }
    }

    if (auto* afterimages = world.Services().Find<VFX::CombatAfterimageService>()) {
        afterimages->Publish(world, sSceneRenderCache_, frame.frameIndex);
    }

    {
        static const auto timer = CPU_PROFILE::RegisterDynamicPass("Render.GpuSceneSync");
        const CPU_PROFILE::ScopedDynamicCpuTimer scope(timer);
        RENDER3D::GPUDRIVEN::GpuSceneRegistrySyncInput gpuSceneInput{};
        gpuSceneInput.sceneCache = &sSceneRenderCache_;
        sGpuSceneRegistry_.SyncForwardFromSceneCache(gpuSceneInput);
        sGpuSceneRegistryValid_ = true;
    }

    {
        static const auto timer = CPU_PROFILE::RegisterDynamicPass("Scene.MassPublication");
        const CPU_PROFILE::ScopedDynamicCpuTimer scope(timer);
        MassRenderSyncContext massContext{&sMassGpuSceneRegistry_, sAssetRegistry_,
                                          sModelManager_,          &MODELRENDERER::GetRenderModelCache(),
                                          &sProjectRoot_,          world.GetRuntimeIdentity(),
                                          frame.frameIndex};
        WorldMassRenderPublicationReceipt massReceipt{};
        const bool massPublished =
            world.PublishMassRenderProxies(frame.frameIndex, &massContext, &SyncMassRenderStores, massReceipt);
        if (massPublished) {
            sPendingMassPublicationGeneration_ = massReceipt.generation;
            sPendingMassPublicationFrameIndex_ = massReceipt.frameIndex;
            sPendingMassSourceVersion_ = sMassGpuSceneRegistry_.GetSource().sourceVersion;
        } else {
            sMassGpuSceneRegistry_.Clear();
            sPendingMassPublicationGeneration_ = 0u;
            sPendingMassPublicationFrameIndex_ = 0u;
            sPendingMassSourceVersion_ = 0u;
        }
        const auto& massStats = sMassGpuSceneRegistry_.GetStats();
        sDebugStats_.massRenderStoreCount = massStats.storeCount;
        sDebugStats_.massRenderInstanceCount = massStats.sourceInstanceCount;
        sDebugStats_.massRenderDirtyRangeCount = massStats.dirtyRangeCount;
        sDebugStats_.massRenderMissingTemplateCount = massStats.missingTemplateCount;
        sDebugStats_.massRenderUnsupportedTemplateCount = massStats.unsupportedTemplateCount;
    }

    sCombinedGpuSceneSource_ = sGpuSceneRegistry_.GetSceneSource();
    sCombinedGpuSceneSource_.mass = &sMassGpuSceneRegistry_.GetSource();

    if (sGpuSceneRegistryValid_) {
        MESHRENDERER::SetGpuDrivenSceneSource(&sCombinedGpuSceneSource_);
    } else {
        MESHRENDERER::SetGpuDrivenSceneSource(nullptr);
    }
    if (sGpuSceneRegistryValid_ && sGpuSceneRegistry_.HasShadowPassSource()) {
        SHADOW::SetGpuDrivenSceneSource(&sGpuSceneRegistry_.GetSceneSource());
    } else {
        SHADOW::SetGpuDrivenSceneSource(nullptr);
    }

    const bool lightingBakeCapture = sRenderViewPurpose_ == RENDER3D::RenderViewPurpose::ReflectionProbeCapture ||
                                     sRenderViewPurpose_ == RENDER3D::RenderViewPurpose::LightProbeCapture;
    if (!lightingBakeCapture) {
        static const auto timer = CPU_PROFILE::RegisterDynamicPass("Scene.DebugOverlays");
        const CPU_PROFILE::ScopedDynamicCpuTimer scope(timer);
        SubmitDebugOverlays(world, sDebugStats_);
    }
}

void RenderSubmissionSystem::AcknowledgeMassRenderFrame(World& world, const FrameContext& frame) {
    if (sPendingMassPublicationGeneration_ == 0u) {
        return;
    }
    const auto& stats = MESHRENDERER::GetMassGpuSceneFrameBufferStats();
    if (stats.sceneResident && stats.worldIdentity == world.GetRuntimeIdentity() &&
        stats.sourceVersion == sPendingMassSourceVersion_ &&
        stats.publicationFrameIndex == sPendingMassPublicationFrameIndex_ &&
        stats.publicationFrameIndex == frame.frameIndex &&
        stats.publicationGeneration == sPendingMassPublicationGeneration_ &&
        world.AcknowledgeMassRenderProxies(WorldMassRenderPublicationReceipt{
            world.GetRuntimeIdentity(), sPendingMassPublicationGeneration_, sPendingMassPublicationFrameIndex_})) {
        sPendingMassPublicationGeneration_ = 0u;
        sPendingMassPublicationFrameIndex_ = 0u;
        sPendingMassSourceVersion_ = 0u;
    }
}

} // namespace HIKARI
