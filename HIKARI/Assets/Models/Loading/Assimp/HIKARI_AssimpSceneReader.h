#pragma once

#include <string>
#include <unordered_map>
#include <vector>

struct aiScene;

namespace HIKARI {
class ModelAsset;
}

namespace HIKARI::ASSETS::MODELS::ASSIMP {

int ReadSceneHierarchy(const aiScene& scene, ModelAsset& asset, std::unordered_map<std::string, int>& nodeNameToIndex);

void FinalizeSceneMeshBindings(const std::vector<int>& meshToSkin, const std::vector<int>& assimpMeshToModelMesh,
                               ModelAsset& asset);

void ReadAnimations(const aiScene& scene, const std::unordered_map<std::string, int>& nodeNameToIndex,
                    ModelAsset& asset);

// Assimp の骨の mOffsetMatrix は網格の節点の bind 時の global を含む（網格空間 → 骨空間）。描画は表面の節点の global に
// 関節の行列を重ねるので、網格の節点が模型の原点に無いと網格の変換が二重に掛かる（枢軸のずれた FBX の人物が浮く）。
// 蒙皮網格の節点の global を頂点と逆バインドへ焼き込み、節点の global を単位にする。関節・動画の対象・共有の網格は焼かない。
void BakeSkinnedMeshNodes(ModelAsset& asset);

} // namespace HIKARI::ASSETS::MODELS::ASSIMP
