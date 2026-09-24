#include "Editor/Widgets/HIKARI_EditorTexturePreview.h"

#include <string>
#include <unordered_map>

#include "Render2D/HIKARI_DxTexture.h"

namespace HIKARI::EDITOR {
namespace {
#if defined(HIKARI_WITH_EDITOR)
std::unordered_map<std::string, int> gTexturePreviewHandles{};

DXTEX::TextureColorSpace ToRuntimeColorSpace(TextureAssetColorSpace colorSpace) {
    switch (colorSpace) {
    case TextureAssetColorSpace::Linear: return DXTEX::TextureColorSpace::Linear;
    case TextureAssetColorSpace::Srgb: return DXTEX::TextureColorSpace::Srgb;
    case TextureAssetColorSpace::Auto:
    default: return DXTEX::TextureColorSpace::Auto;
    }
}
#endif
} // namespace

int ResolveEditorTexturePreviewHandle(const AssetGuid& guid, const TextureAssetDescriptor& descriptor) {
#if defined(HIKARI_WITH_EDITOR)
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
#else
    (void)guid;
    (void)descriptor;
    return -1;
#endif
}

void ClearEditorTexturePreviewCache() {
#if defined(HIKARI_WITH_EDITOR)
    gTexturePreviewHandles.clear();
#endif
}

} // namespace HIKARI::EDITOR
