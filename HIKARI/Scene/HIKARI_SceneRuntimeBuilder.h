#pragma once

#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "Core/Math/HIKARI_Math3D.h"
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Scene/HIKARI_SceneObjectId.h"

namespace HIKARI {

class ComponentRegistry;
class AssetRegistry;
class ModelManager;
class Material;
struct MaterialAssetDescriptor;
class ModelAsset;
class GameObject;
class IComponent;
class SkyManager;
class World;
class WorldMassSchemaRegistry;
struct SceneDocument;
struct SceneObjectData;
struct SceneObjectChanges;
namespace SPRITE {
class SpriteAssetStore;
}

struct SceneDependencySet {
    std::unordered_set<std::string> modelAssetIds{};
    std::unordered_set<std::string> skyAssetIds{};
    std::unordered_set<std::string> spriteAssetIds{};
    std::unordered_set<std::string> spriteMaterialAssetIds{};
    // 環境プロファイルの空。AddEnvironmentSkyDependencies が埋める。
    SkySettings sky{};
    std::unordered_set<std::string> textureAssetIds{};
    std::unordered_set<std::string> materialAssetIds{};
    std::unordered_set<std::string> modelMaterialSetAssetIds{};
    std::unordered_set<std::string> vfxAssetIds{};
    std::string lightingSceneHash{};
    std::unordered_map<std::string, std::string> lightingBakeAuthoringHashes{};
    RENDER3D::LIGHTING::SceneLightingSet lighting{};
};

// 解決済みの環境プロファイルの空（HDRI・全景・雲層の資産）を依存へ加える。
void AddEnvironmentSkyDependencies(const SkySettings& sky, SceneDependencySet& dependencies);

// Resolves and loads a registered model through the canonical runtime
// ModelManager path. Callers must not invoke importers directly.
ModelAsset* ResolveModelAssetNow(ModelManager& modelManager, const AssetRegistry& assetRegistry,
                                 const std::string& assetId);

// Builds the same runtime Material object used by ModelComponent's
// formal Material Set projection. The caller owns the returned value.
std::unique_ptr<Material> BuildRuntimeMaterialOverride(const AssetRegistry& assetRegistry,
                                                       const MaterialAssetDescriptor& descriptor,
                                                       bool inheritModelTextures);

class SceneRuntimeBuilder {
public:
    SceneDependencySet CollectDependencies(const SceneDocument& document,
                                           const ComponentRegistry* enabledComponents = nullptr) const;
    void CollectObjectDependencies(const SceneObjectData& object, const ComponentRegistry* enabledComponents,
                                   SceneDependencySet& dependencies) const;
    bool PreloadDependencies(const SceneDependencySet& dependencies, const AssetRegistry& assetRegistry,
                             ModelManager& modelManager, SkyManager& skyManager,
                             const std::filesystem::path& projectRoot, const std::string& sceneGuid,
                             bool loadLighting = true,
                             const SPRITE::SpriteAssetStore* spriteAssetStore = nullptr) const;

    bool BuildWorldFromDocument(const SceneDocument& document, World& world, const AssetRegistry& assetRegistry,
                                const ComponentRegistry& componentRegistry,
                                const WorldMassSchemaRegistry& massSchemaRegistry, ModelManager& modelManager,
                                SkyManager& skyManager) const;
    // 既存 World へ document のオブジェクトを追加する。親は追加分と既存オブジェクトの両方から解決する。
    // 失敗時は今回作ったオブジェクトだけを破棄して false を返す。
    bool AddObjectsFromDocument(std::span<const SceneObjectData* const> objects, World& world,
                                const AssetRegistry& assetRegistry, const ComponentRegistry& componentRegistry,
                                ModelManager& modelManager) const;
    bool InitializeRuntimeComponentAssets(GameObject& object, IComponent& component, const AssetRegistry& assetRegistry,
                                          ModelManager& modelManager, std::string& outDiagnostic) const;
    void FinalizeRuntimeObjectAssets(GameObject& object, const AssetRegistry& assetRegistry) const;
    // 依頼された差分を document と世界の今の状態で分け直す（取り消しの組などでは同じ物体が追加・変更・削除のどれにも
    // 現れうる）。両方にあれば changed、document だけなら added、世界だけなら removed。重複は一つにまとめる。
    SceneObjectChanges ClassifyObjectChanges(const SceneDocument& document, const SceneObjectChanges& requested,
                                             const World& world) const;
    // 世界の物体を document に合わせる。removed を壊し、added を document から作り、changed はその場で更新する
    // （親が追加された場合も解決できるよう更新は生成の後）。changes は ClassifyObjectChanges の結果。失敗したら false で、
    // 呼び手は世界を作り直す。
    bool ApplyObjectChanges(const SceneDocument& document, const SceneObjectChanges& changes, World& world,
                            const AssetRegistry& assetRegistry, const ComponentRegistry& componentRegistry,
                            ModelManager& modelManager) const;

private:
    // 物体を壊さずに document の内容へ合わせる（句柄と変わらない部品を保つ）。名前・局所変換・親を直し、document に無い
    // 部品を外し、足りない部品を足し、属性が変わった部品だけ読み直して資産を初期化し直す。部品の生成・読み込みに失敗したら false。
    bool UpdateObjectFromDocument(const SceneObjectData& object, GameObject& existing, World& world,
                                  const AssetRegistry& assetRegistry, const ComponentRegistry& componentRegistry,
                                  ModelManager& modelManager) const;
};

} // namespace HIKARI
