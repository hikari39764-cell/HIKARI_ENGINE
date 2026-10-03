#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/HIKARI_AssetTypes.h"

namespace HIKARI::SPRITE {

struct SpritePage {
    std::string id;
    std::string textureAssetId;
    std::string artifactPath;
    std::string normalTextureAssetId;
    std::string normalArtifactPath;
    TextureAssetAlphaClass colorAlphaClass = TextureAssetAlphaClass::Unknown;
    uint32_t width = 0u;
    uint32_t height = 0u;
};

struct SpriteFrame {
    std::string id;
    uint32_t pageIndex = 0u;
    uint32_t x = 0u;
    uint32_t y = 0u;
    uint32_t width = 0u;
    uint32_t height = 0u;
    uint32_t sourceWidth = 0u;
    uint32_t sourceHeight = 0u;
    uint32_t trimX = 0u;
    uint32_t trimY = 0u;
    float pivotX = 0.5f;
    float pivotY = 0.5f;
    bool rotated = false;
    bool empty = false;
};

struct SpriteClipFrame {
    uint32_t frameIndex = 0u;
    uint32_t durationMs = 0u;
    double endTimeMs = 0.0;
};

struct SpriteClip {
    std::string id;
    bool loop = false;
    std::vector<SpriteClipFrame> frames;
};

struct SpriteAssetData {
    std::string name;
    float pixelsPerUnit = 64.0f;
    std::string defaultClip;
    std::vector<SpritePage> pages;
    std::vector<SpriteFrame> frames;
    std::vector<SpriteClip> clips;
};

std::string MakeSpritePageTextureAssetId(std::string_view spriteGuid, std::string_view pageId);
std::string MakeSpritePageNormalTextureAssetId(std::string_view spriteGuid, std::string_view pageId);

} // namespace HIKARI::SPRITE
