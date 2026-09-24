#pragma once

#include <cstdint>

#include "Render3D/Resources/HIKARI_RenderResourceHandle.h"

namespace HIKARI::TEXT_RENDERING::INTERNAL {

using SdfTextAtlasReleaseFunction = bool (*)(RENDER3D::TextureResourceHandle);

// Atlas の一世代を不変 resource として所有する。
// shared lease が残る間は旧 UV と texture handle の組を維持する。
class SdfTextAtlasGeneration final {
public:
    SdfTextAtlasGeneration(RENDER3D::TextureResourceHandle texture, uint32_t generation,
                           SdfTextAtlasReleaseFunction release) noexcept
        : texture_(texture), generation_(generation), release_(release) {}

    ~SdfTextAtlasGeneration() {
        if (texture_ && release_ != nullptr) {
            (void)release_(texture_);
        }
    }

    SdfTextAtlasGeneration(const SdfTextAtlasGeneration&) = delete;
    SdfTextAtlasGeneration& operator=(const SdfTextAtlasGeneration&) = delete;

    RENDER3D::TextureResourceHandle GetTexture() const noexcept { return texture_; }

    uint32_t GetGeneration() const noexcept { return generation_; }

private:
    RENDER3D::TextureResourceHandle texture_{};
    uint32_t generation_ = 0u;
    SdfTextAtlasReleaseFunction release_ = nullptr;
};

} // namespace HIKARI::TEXT_RENDERING::INTERNAL
