#pragma once

#include <cstdint>
#include <string>

#include "RenderContracts/Model/HIKARI_ModelAssetTypes.h"

namespace HIKARI {
class ModelAsset;
}

namespace HIKARI::ASSETS::MODELS {

void GenerateModelPrimitiveNormals(MeshPrimitive& primitive);
void GenerateModelPrimitiveTangents(MeshPrimitive& primitive);

// 蒙皮網格の表面の節点の global を頂点と逆バインドへ焼き込み、節点の global を単位にする。描画は「表面の節点の global ×
// 関節の global × 逆バインド」で、Assimp の骨の offset は網格の節点の bind 時の global を含み、glTF は網格の節点の変換を
// 無視する決まりなので、どちらも網格の節点が模型の原点に無いと網格の変換が二重に掛かる（人物が浮く・ずれる）。
// 関節・動画の対象・共有の網格は焼かない。
void BakeSkinnedMeshNodes(ModelAsset& asset);

// 図元の顶点流と索引が揃っているか：顶点布局が使う流（蒙皮なら skinnedVertices）に全ての索引が収まり、蒙皮なら静的流と
// 同じ数、静的なら蒙皮流が空。揃っていない模型は書き出さない（簇几何の LOD 簡略化や描画が範囲外を読み書きする）。
// 失敗した時は outMessage に網格・図元と理由を書く。
bool ValidateModelPrimitiveStreams(const ModelAsset& model, std::string& outMessage);

} // namespace HIKARI::ASSETS::MODELS
