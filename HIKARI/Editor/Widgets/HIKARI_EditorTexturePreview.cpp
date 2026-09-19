#include "Editor/Widgets/HIKARI_EditorTexturePreview.h"

#include <string>
#include <unordered_map>

#include "Render2D/HIKARI_DxTexture.h"

namespace HIKARI::EDITOR {
    namespace {
#if defined(HIKARI_WITH_EDITOR)
        std::unordered_map<std::string, int> gTexturePreviewHandles{};

        DXTEX::TextureColorSpace ToRuntimeColorSpace(
            TextureAssetColorSpace colorSpace) {

            switch (colorSpace) {
            case TextureAssetColorSpace::Linear:
                return DXTEX::TextureColorSpace::Linear;
            case TextureAssetColorSpace::Srgb:
                return DXTEX::TextureColorSpace::Srgb;
            case TextureAssetColorSpace::Auto:
            default:
                return DXTEX::TextureColorSpace::Auto;
            }
        }
#endif
    }

    int ResolveEditorTexturePreviewHandle(
        const AssetGuid& guid,
        const TextureAssetDescriptor& descriptor) {
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
            "editor/texture_preview/" + guid.value,
            descriptor.sourcePath,
            ToRuntimeColorSpace(descriptor.colorSpace));
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

    void InvalidateEditorTexturePreviewByGuid(const AssetGuid& guid) {
#if defined(HIKARI_WITH_EDITOR)
        if (!guid.IsValid()) {
            return;
        }
        const std::string prefix = guid.value + "|";
        for (auto it = gTexturePreviewHandles.begin();
             it != gTexturePreviewHandles.end();) {
            if (it->first.rfind(prefix, 0) == 0) {
                it = gTexturePreviewHandles.erase(it);
            } else {
                ++it;
            }
        }
#else
        (void)guid;
#endif
    }

    void InvalidateEditorTexturePreviewByPath(const std::string& path) {
#if defined(HIKARI_WITH_EDITOR)
        if (path.empty()) {
            return;
        }
        for (auto it = gTexturePreviewHandles.begin();
             it != gTexturePreviewHandles.end();) {
            if (it->first.find("|" + path) != std::string::npos) {
                it = gTexturePreviewHandles.erase(it);
            } else {
                ++it;
            }
        }
#else
        (void)path;
#endif
    }

} // namespace HIKARI::EDITOR
