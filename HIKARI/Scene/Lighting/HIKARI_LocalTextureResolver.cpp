#include "Scene/Lighting/HIKARI_LocalTextureResolver.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include "Assets/HIKARI_AssetRegistry.h"
#include "Core/HIKARI_Logger.h"
#include "Core/Hashing/HIKARI_FnvHash.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
#include "Render3D/Lighting/HIKARI_LocalLightGpuData.h"
#include "Render3D/Resources/HIKARI_SceneTextureAssetRequest.h"
#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"

namespace HIKARI {
namespace {

using RENDER3D::LIGHTING::SceneLightProvider;
using RENDER3D::LIGHTING::SceneLightType;

static_assert(GFX::DESCRIPTOR::kUserSrvCount < RENDER3D::LIGHTING::kLocalLightTextureIndexStride);

// 描画が引く記述子番号 + 1。材質 texture pool の外は無し。
uint32_t EncodeTextureIndex(RENDER3D::TextureResourceHandle texture) {
    if (!RENDER3D::IsTextureResourceValid(texture)) {
        return 0u;
    }
    const UINT index = RENDER3D::GetTextureResourceSrvDescriptorIndex(texture);
    return index < GFX::DESCRIPTOR::kUserSrvCount ? index + 1u : 0u;
}

// 表の値を平方根で 8 bit に詰める（暗い裾の階調を残す）。シェーダーが R を二乗して戻す。
std::vector<uint8_t> EncodeProfileTable(const std::vector<float>& values) {
    std::vector<uint8_t> rgba(values.size() * 4u);
    for (size_t index = 0u; index < values.size(); ++index) {
        const auto encoded =
            static_cast<uint8_t>(std::lround(std::sqrt(std::clamp(values[index], 0.0f, 1.0f)) * 255.0f));
        rgba[index * 4u + 0u] = encoded;
        rgba[index * 4u + 1u] = encoded;
        rgba[index * 4u + 2u] = encoded;
        rgba[index * 4u + 3u] = 255u;
    }
    return rgba;
}

} // namespace

void LocalTextureResolver::Resolve(const AssetDatabase& database, const AssetRegistry& registry,
                                   RENDER3D::LIGHTING::SceneLightingSet& lighting) {
    if (database_ != &database) {
        database_ = &database;
        profiles_.Configure(&database);
        revision_ = 0u;
    }
    if (revision_ != database.GetContentRevision()) {
        revision_ = database.GetContentRevision();
        textures_.clear();
        failed_.clear();
    }
    for (SceneLightProvider& light : lighting.lights) {
        if (light.type != SceneLightType::Point && light.type != SceneLightType::Spot) {
            continue;
        }
        if (!light.iesProfile.empty()) {
            if (const ProfileTexture* profile = ResolveProfile(light.iesProfile)) {
                light.iesProfileTexture = EncodeTextureIndex(profile->texture);
                light.intensity *= profile->peakToMean;
            }
        }
        if (light.type == SceneLightType::Spot && !light.cookie.empty()) {
            light.cookieTexture = ResolveTexture(registry, light.cookie);
        }
    }
    for (RENDER3D::DECAL::SceneDecalProvider& decal : lighting.decals) {
        if (!decal.settings.baseColorTexture.empty()) {
            decal.baseColorTextureIndex = ResolveTexture(registry, decal.settings.baseColorTexture);
        }
        if (!decal.settings.normalTexture.empty()) {
            decal.normalTextureIndex = ResolveTexture(registry, decal.settings.normalTexture);
        }
    }
}

const LocalTextureResolver::ProfileTexture* LocalTextureResolver::ResolveProfile(const std::string& guid) {
    if (failed_.contains(guid)) {
        return nullptr;
    }
    std::string error;
    std::shared_ptr<const ASSETS::LIGHTING::IesProfileAsset> source = profiles_.Load(AssetGuid{guid}, error);
    if (!source) {
        Report(guid, error);
        return nullptr;
    }
    ProfileTexture& entry = profileTextures_[guid];
    if (entry.source == source && RENDER3D::IsTextureResourceValid(entry.texture)) {
        return &entry;
    }
    const ASSETS::LIGHTING::IesProfileTable table = ASSETS::LIGHTING::SampleIesProfile(*source);
    const std::vector<uint8_t> rgba = EncodeProfileTable(table.values);
    // 同じ内容なら同じ名前で、テクスチャの共有キャッシュから同じ資源が返る。
    const std::string name = "light_ies/" + guid + "/" +
                             std::to_string(HASH::Fnv1aBytes(HASH::kFnv1a64OffsetBasis, rgba.data(), rgba.size()));
    const RENDER3D::TextureResourceHandle texture = RENDER3D::CreateRgbaTextureResource(
        name, ASSETS::LIGHTING::kIesProfileVerticalSamples, ASSETS::LIGHTING::kIesProfileHorizontalSamples, rgba,
        RENDER3D::TextureResourceColorSpace::Linear);
    if (entry.texture && entry.texture != texture) {
        (void)RENDER3D::ReleaseTextureResource(entry.texture);
    }
    entry = ProfileTexture{std::move(source), texture, table.peakToMean};
    if (!RENDER3D::IsTextureResourceValid(texture)) {
        Report(guid, "the IES profile texture could not be created");
        return nullptr;
    }
    return &entry;
}

uint32_t LocalTextureResolver::ResolveTexture(const AssetRegistry& registry, const std::string& guid) {
    if (const auto found = textures_.find(guid);
        found != textures_.end() && RENDER3D::IsTextureResourceValid(found->second)) {
        return EncodeTextureIndex(found->second);
    }
    if (failed_.contains(guid)) {
        return 0u;
    }
    const std::optional<RENDER3D::TextureResourceLoadRequest> request =
        RENDER3D::FindSceneTextureAssetRequest(registry, guid);
    const RENDER3D::TextureResourceHandle texture =
        request ? RENDER3D::LoadTextureResourceWithColorSpace(request->name, request->path, request->colorSpace)
                : RENDER3D::TextureResourceHandle{};
    if (!RENDER3D::IsTextureResourceValid(texture)) {
        Report(guid, "the texture is not a loadable 2D texture asset");
        return 0u;
    }
    textures_[guid] = texture;
    return EncodeTextureIndex(texture);
}

void LocalTextureResolver::Report(const std::string& guid, const std::string& message) {
    if (failed_.insert(guid).second) {
        HIKARI_LOG_WARN("[LocalTextures] " + guid + ": " + message);
    }
}

} // namespace HIKARI
