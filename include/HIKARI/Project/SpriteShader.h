#pragma once

#include <cstdint>

namespace HIKARI {

inline constexpr const char* kGameProjectSpriteShaderEngineInterfaceId = "HIKARI.Project.SpriteShader";
inline constexpr uint32_t kGameProjectSpriteShaderEngineInterfaceVersion = 1u;
// SHA-256 of the canonical EngineSdk/Project/Sprite.hlsli source.
inline constexpr const char* kGameProjectSpriteShaderEngineInterfaceHash =
    "e235912b8dc0e15663f4cc6408002d7bad29f10ee1182df4d8852c6e9e7e6a2d";
inline constexpr const char* kGameProjectSpriteShaderRootLayoutId = "HIKARI.RenderGeometry.WorldSpriteDraw.Root";
inline constexpr uint32_t kGameProjectSpriteShaderRootLayoutVersion = 5u;
inline constexpr const char* kGameProjectSpriteShaderRootLayoutHash =
    "28b3180ebac7fb772905401be74bd0fed4dc99633fe26ce1b89d8f2e18801f8b";
inline constexpr const char* kGameProjectSpriteColorPixelEntryId = "SpriteColorPS";
inline constexpr const char* kGameProjectSpriteDepthOfFieldPixelEntryId = "SpriteDepthOfFieldPS";
inline constexpr uint32_t kGameProjectSpriteShaderParameterSlotCount = 16u;

} // namespace HIKARI
