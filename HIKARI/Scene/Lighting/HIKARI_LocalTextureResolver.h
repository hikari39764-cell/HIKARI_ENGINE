#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "Assets/HIKARI_AssetFileCache.h"
#include "Assets/Lighting/HIKARI_IesProfileAsset.h"
#include "Render3D/Lighting/HIKARI_LightingProviderContract.h"
#include "Render3D/Resources/HIKARI_RenderResourcePool.h"

namespace HIKARI {

class AssetRegistry;

// 局所の記録が引くテクスチャ（光源の配光とクッキー、デカールの色と法線）を描画用のテクスチャへ解決し、記述子番号を
// 書く。配光は全光束を保つよう光度を上げる。配光の表は資産の内容が変わった時だけ作り直し、解決できない資産は資産の
// 内容版が進むまで再試行しない。
class LocalTextureResolver {
public:
    void Resolve(const AssetDatabase& database, const AssetRegistry& registry,
                 RENDER3D::LIGHTING::SceneLightingSet& lighting);

private:
    struct ProfileTexture {
        std::shared_ptr<const ASSETS::LIGHTING::IesProfileAsset> source{};
        RENDER3D::TextureResourceHandle texture{};
        float peakToMean = 1.0f;
    };

    const ProfileTexture* ResolveProfile(const std::string& guid);
    // Texture 資産（クッキー、デカール）。
    uint32_t ResolveTexture(const AssetRegistry& registry, const std::string& guid);
    void Report(const std::string& guid, const std::string& message);

    using ProfileCache = AssetFileCache<ASSETS::LIGHTING::IesProfileAsset, AssetType::LightProfile,
                                        &ASSETS::LIGHTING::LoadIesProfileAsset>;
    ProfileCache profiles_;
    const AssetDatabase* database_ = nullptr;
    uint64_t revision_ = 0u;
    std::unordered_map<std::string, ProfileTexture> profileTextures_;
    std::unordered_map<std::string, RENDER3D::TextureResourceHandle> textures_;
    std::unordered_set<std::string> failed_;
};

} // namespace HIKARI
