#include "Render2D/HIKARI_SpineTextureLoader.h"

#include <memory>

namespace HIKARI {

void SpineDxTextureLoader::load(spine::AtlasPage& page, const spine::String& path) {
    const std::string p = path.buffer();

    int handle = HIKARI::DXTEX::DxTextureManager::LoadTextureSrgb("spine:" + p, p);
    if (handle < 0) {
        return;
    }

    // Spine が必要とする実サイズを DxTextureManager から取得する。
    UINT w = 0, h = 0;
    HIKARI::DXTEX::DxTextureManager::GetTextureSize(handle, w, h);

    auto tex = std::make_unique<SpineTexture>();
    tex->handle = handle;
    tex->width = static_cast<int>(w);
    tex->height = static_cast<int>(h);

    page.width = tex->width;
    page.height = tex->height;
    // Spine の AtlasPage は void* でしか持てないので所有権を預け、unload で引き取って破棄する。
    page.texture = tex.release();
}

void SpineDxTextureLoader::unload(void* textureObject) {
    const std::unique_ptr<SpineTexture> owned(static_cast<SpineTexture*>(textureObject));
}

} // namespace HIKARI
