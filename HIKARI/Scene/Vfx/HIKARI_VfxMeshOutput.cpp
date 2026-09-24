#include "Scene/Vfx/HIKARI_VfxMeshOutput.h"

#include <algorithm>
#include <vector>

#include "Assets/HIKARI_AssetTypes.h"
#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Core/HIKARI_Logger.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Models/HIKARI_ModelManager.h"
#include "Render3D/Runtime/HIKARI_RenderModelCache.h"
#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"
#include "Scene/HIKARI_SceneRuntimeBuilder.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"

namespace HIKARI::VFX {
namespace {
using RENDER3D::RUNTIME::RenderModelAsset;
using RENDER3D::RUNTIME::SceneRenderCache;
using RENDER3D::RUNTIME::SceneRenderObjectDesc;
using RENDER3D::RUNTIME::SceneRenderObjectId;

// 再生ハンドルとレイヤー添字から安定 ID を作る。同じレイヤーは毎フレーム
// 同じ ID を上書きし、停止したらその ID を削除する。
uint64_t MakeStableId(const VfxLayerMeshSample& sample) {
    const uint64_t slot = sample.handle.slot;
    const uint64_t generation = sample.handle.generation;
    const uint64_t mixed = (sample.playbackNamespace * 0x27d4eb2f165667c5ull) ^ (slot * 0x9e3779b97f4a7c15ull) ^
                           (generation * 0xc2b2ae3d27d4eb4full) ^
                           (uint64_t{sample.emitterIndex} * 0x165667b19e3779f9ull);
    // 0 は無効 ID なので避ける。
    return 0xB000000000000000ull | (mixed & 0x0FFFFFFFFFFFFFFFull);
}

} // namespace

void VfxMeshInstancePublisher::Reset() noexcept {
    publishedIds_.clear();
    materials_.clear();
}

void VfxMeshInstancePublisher::ClearPublished(SceneRenderCache& sceneCache) {
    for (const auto id : publishedIds_)
        sceneCache.Remove(SceneRenderObjectId{id});
    if (!publishedIds_.empty())
        sceneCache.PreRenderSync();
    Reset();
}

void VfxMeshInstancePublisher::Publish(const VfxPlaybackService& playback, SceneRenderCache& sceneCache,
                                       RENDER3D::RUNTIME::RenderModelCache& renderModelCache,
                                       const AssetRegistry& assetRegistry, ModelManager& modelManager,
                                       const std::filesystem::path& projectRoot) {
    std::vector<VfxLayerMeshSample> samples{};
    playback.CollectMeshLayers(samples);

    // 同じ scene 同期に追記する。BeginPatchSync は world 側の dirty
    // surface を消すため、publisher が新しい同期を開始してはならない。
    std::unordered_set<uint64_t> liveIds{};
    liveIds.reserve(samples.size());
    for (const VfxLayerMeshSample& sample : samples) {
        const uint64_t id = MakeStableId(sample);
        // 他の publisher/world の ID を上書きしてはならない。
        if (liveIds.contains(id) ||
            (sceneCache.Find(SceneRenderObjectId{id}) != nullptr && !publishedIds_.contains(id)))
            continue;
        const auto* descriptor = assetRegistry.FindAs<ModelAssetDescriptor>(AssetId{sample.meshAssetGuid});
        if (descriptor == nullptr) {
            static bool warned = false;
            if (!warned) {
                warned = true;
                HIKARI_LOG_WARN("[VfxMeshOutput] mesh asset is not registered: " + sample.meshAssetGuid);
            }
            continue;
        }
        ModelAsset* model = HIKARI::ResolveModelAssetNow(modelManager, assetRegistry, descriptor->id.value);
        if (model == nullptr || model->GetState() != ModelAsset::State::Loaded) {
            continue;
        }
        const RenderModelAsset* renderModel = renderModelCache.GetOrCreate(*model);
        if (renderModel == nullptr || !renderModel->valid) {
            continue;
        }
        SceneRenderObjectDesc desc{};
        desc.id = SceneRenderObjectId{id};
        desc.model = model;
        desc.renderModel = renderModel;
        HIKARI::Transform3D worldTransform{};
        worldTransform.position = sample.worldTransform.position;
        worldTransform.rotation = sample.worldTransform.rotation;
        worldTransform.scale = sample.worldTransform.scale;
        desc.worldTransform = worldTransform;
        desc.localBounds = renderModel->localBounds;
        desc.worldBounds = BOUNDS::TransformBounds(desc.localBounds, desc.worldTransform.GetWorldMatrix());
        desc.visible = true;
        desc.isStatic = false;
        desc.castShadow = (sample.flags & 1u) != 0u;
        desc.receiveShadow = (sample.flags & 2u) != 0u;
        desc.allowStaticCachedForward = false;
        if (!sample.materialAssetGuid.empty()) {
            auto& binding = materials_[id];
            if (binding.assetGuid != sample.materialAssetGuid || binding.registryId != assetRegistry.GetInstanceId() ||
                binding.registryRevision != assetRegistry.GetRevision()) {
                binding = {sample.materialAssetGuid, assetRegistry.GetInstanceId(), assetRegistry.GetRevision(), {}};
                if (const auto* material =
                        assetRegistry.FindAs<MaterialAssetDescriptor>(AssetId{sample.materialAssetGuid}))
                    binding.material = BuildRuntimeMaterialOverride(assetRegistry, *material, false);
            }
            if (!binding.material)
                continue;
            for (uint32_t index = 0; index < model->materials.size(); ++index)
                desc.materialOverrides.push_back({index, binding.material.get(), binding.material->GetRevision()});
        } else {
            materials_.erase(id);
        }
        if (!descriptor->clusteredGeometryPath.empty()) {
            std::filesystem::path clusterPath = descriptor->clusteredGeometryPath;
            if (!clusterPath.is_absolute() && !projectRoot.empty()) {
                clusterPath = projectRoot / clusterPath;
            }
            desc.clusteredGeometryPath = clusterPath.lexically_normal().generic_string();
        }
        sceneCache.Upsert(desc);
        liveIds.insert(id);
    }
    // 前フレームまで出していて、もう出ていないものを片付ける。
    for (const uint64_t id : publishedIds_) {
        if (liveIds.find(id) == liveIds.end()) {
            sceneCache.Remove(SceneRenderObjectId{id});
            materials_.erase(id);
        }
    }
    // 新規 surface と削除も GPU registry へ渡す前に確定する。
    sceneCache.PreRenderSync();
    publishedIds_ = std::move(liveIds);
}

} // namespace HIKARI::VFX
