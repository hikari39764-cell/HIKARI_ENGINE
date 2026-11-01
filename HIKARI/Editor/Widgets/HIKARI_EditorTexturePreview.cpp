#include "Editor/Widgets/HIKARI_EditorTexturePreview.h"

#include <string>
#include <unordered_map>

#include "RenderCore/Texture/HIKARI_DxTexture.h"

namespace HIKARI::EDITOR {
namespace {
std::unordered_map<std::string, int> gTexturePreviewHandles{};

DXTEX::TextureColorSpace ToRuntimeColorSpace(TextureAssetColorSpace colorSpace) {
    switch (colorSpace) {
    case TextureAssetColorSpace::Linear: return DXTEX::TextureColorSpace::Linear;
    case TextureAssetColorSpace::Srgb: return DXTEX::TextureColorSpace::Srgb;
    case TextureAssetColorSpace::Auto:
    default: return DXTEX::TextureColorSpace::Auto;
    }
}
} // namespace

int ResolveEditorTexturePreviewHandle(const AssetGuid& guid, const TextureAssetDescriptor& descriptor) {
    if (!guid.IsValid() || descriptor.sourcePath.empty()) {
        return -1;
    }
    // 登録側が解決した HTEX/DDS パスをそのままプレビューに使う。
    const std::string cacheKey = guid.value + "|" + descriptor.sourcePath;
    const auto found = gTexturePreviewHandles.find(cacheKey);
    if (found != gTexturePreviewHandles.end()) {
        return found->second;
    }
    const int handle = DXTEX::DxTextureManager::LoadTextureWithColorSpace(
        "editor/texture_preview/" + guid.value, descriptor.sourcePath, ToRuntimeColorSpace(descriptor.colorSpace));
    gTexturePreviewHandles.emplace(cacheKey, handle);
    return handle;
}

void ClearEditorTexturePreviewCache() { gTexturePreviewHandles.clear(); }

} // namespace HIKARI::EDITOR
