#pragma once

#include <cstdint>
#include <filesystem>
#include <unordered_set>
#include <unordered_map>
#include <memory>
#include <string>

namespace HIKARI {
class AssetRegistry;
class ModelManager;
class Material;
} // namespace HIKARI

namespace HIKARI::RENDER3D::RUNTIME {
class RenderModelCache;
class SceneRenderCache;
} // namespace HIKARI::RENDER3D::RUNTIME

namespace HIKARI::VFX {

class VfxPlaybackService;

// Mesh 出力レイヤーを scene render cache の一時オブジェクトとして公開する。
// 所有権はこの publisher が持ち、停止したレイヤーは同じフレームで消す。
// モデル資源の解決は既存の runtime loader / render model cache に任せ、
// ここでは描画要求を組み立てるだけにする。
class VfxMeshInstancePublisher final {
public:
    void Reset() noexcept;
    void ClearPublished(RENDER3D::RUNTIME::SceneRenderCache& sceneCache);

    // 毎フレーム 1 回。world 由来の scene cache 同期が終わった直後、
    // GPU scene registry へ流す前に呼ぶ。
    void Publish(const VfxPlaybackService& playback, RENDER3D::RUNTIME::SceneRenderCache& sceneCache,
                 RENDER3D::RUNTIME::RenderModelCache& renderModelCache, const AssetRegistry& assetRegistry,
                 ModelManager& modelManager, const std::filesystem::path& projectRoot);

private:
    std::unordered_set<uint64_t> publishedIds_{};
    struct MaterialBinding {
        std::string assetGuid;
        uint64_t registryId = 0u;
        uint64_t registryRevision = 0u;
        std::shared_ptr<Material> material;
    };
    std::unordered_map<uint64_t, MaterialBinding> materials_{};
};

} // namespace HIKARI::VFX
