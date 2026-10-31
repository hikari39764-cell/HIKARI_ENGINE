#pragma once

#include <string>

#include "Core/HIKARI_AssetGuid.h"
#include "Assets/HIKARI_AssetTypes.h"

namespace HIKARI::EDITOR {

// 貼図アセットのプレビュー SRV を解決してキャッシュする唯一の所有者。
// マテリアルのテクスチャスロットと VFX のパーティクル貼図欄が同じ実装を使う。
// 見つからない場合は -1 を返す。
int ResolveEditorTexturePreviewHandle(const AssetGuid& guid, const TextureAssetDescriptor& descriptor);

void ClearEditorTexturePreviewCache();

} // namespace HIKARI::EDITOR
