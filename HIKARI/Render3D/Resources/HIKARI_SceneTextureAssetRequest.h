#pragma once

#include <optional>
#include <string>

#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI::RENDER3D {

// シーンの部品が GUID で直接参照する 2D テクスチャ資産（ワールドスプライト、スポットライトのクッキー）の読み込み要求。
// 事前読み込みと描画時の解決が同じ名前・色空間で共有キャッシュを引く。2D でない・未登録の資産は空。
inline std::optional<TextureResourceLoadRequest> FindSceneTextureAssetRequest(const AssetRegistry& registry,
                                                                              const std::string& assetId) {
    const auto* descriptor = registry.FindAs<TextureAssetDescriptor>(AssetId{assetId});
    if (descriptor == nullptr || descriptor->dimension != TextureAssetDimension::Texture2D ||
        descriptor->sourcePath.empty()) {
        return std::nullopt;
    }
    return TextureResourceLoadRequest{"runtime_texture/" + assetId, descriptor->sourcePath,
                                      descriptor->colorSpace == TextureAssetColorSpace::Linear
                                          ? TextureResourceColorSpace::Linear
                                          : TextureResourceColorSpace::Srgb};
}

} // namespace HIKARI::RENDER3D
