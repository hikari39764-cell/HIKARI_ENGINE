#pragma once
#include "RenderCore/Texture/HIKARI_DxTexture.h"

namespace HIKARI::DXTEX::DETAIL {
// 単体読込と HTEX バッチが同じ色空間・キャッシュ識別規則を使う。
void LogTextureLoad(const char* kind, const std::string& path, TextureColorSpace colorSpace, DXGI_FORMAT srvFormat,
                    int handle);
std::string MakeTextureCacheKey(const std::string& name, const std::string& path, TextureColorSpace colorSpace);
TextureColorSpace ResolveTextureLoadColorSpace(const std::string& name, const std::string& path,
                                               TextureColorSpace colorSpace);
DXGI_FORMAT ResolveSrvFormat(DXGI_FORMAT resourceFormat, TextureColorSpace colorSpace);
} // namespace HIKARI::DXTEX::DETAIL
