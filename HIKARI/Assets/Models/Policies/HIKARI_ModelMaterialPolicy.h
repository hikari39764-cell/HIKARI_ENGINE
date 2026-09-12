#pragma once

#include <string_view>

#include "Assets/Models/HIKARI_ModelAssetTypes.h"
#include "Core/Text/HIKARI_AsciiCase.h"

namespace HIKARI {

    namespace MATERIAL_FEATURES {
        constexpr uint32_t Unlit = 1u << 0;
        constexpr uint32_t AlphaMask = 1u << 1;
        constexpr uint32_t Emissive = 1u << 2;
        constexpr uint32_t ThinTransparentSurface = 1u << 3;
        // v37 以前の生成物のみが使用する旧反射応答。新規インポートには付与しない。
        constexpr uint32_t SpecularGlossCompatibility = 1u << 4;
    }

    namespace MATERIAL_POLICY {

        inline bool UsesTextureCoordinateSet(const MaterialAsset& material, int uvSet) noexcept {
            for (const TextureSlot* slot : {&material.baseColorTexture, &material.normalTexture,
                &material.metallicRoughnessTexture, &material.occlusionTexture, &material.emissiveTexture,
                &material.specularTexture, &material.specularColorTexture}) {
                if (slot->textureIndex >= 0 && slot->texCoord == uvSet) {
                    return true;
                }
            }
            return false;
        }

        constexpr float kOpticalTransparencyFallbackOpacity =
            0.35f;

        inline bool ContainsLowerAscii(std::string_view text, std::string_view needle) {
            if (needle.empty() || text.size() < needle.size()) {
                return false;
            }
            for (size_t begin = 0; begin + needle.size() <= text.size(); ++begin) {
                bool matched = true;
                for (size_t i = 0; i < needle.size(); ++i) {
                    const char c = TEXT::ToLowerAscii(text[begin + i]);
                    if (c != needle[i]) {
                        matched = false;
                        break;
                    }
                }
                if (matched) {
                    return true;
                }
            }
            return false;
        }

        inline bool ContainsLowerAsciiToken(
            std::string_view text,
            std::string_view token) {
            if (token.empty() || text.size() < token.size()) {
                return false;
            }
            const auto isTokenCharacter = [](char value) {
                const char c = TEXT::ToLowerAscii(value);
                return
                    (c >= 'a' && c <= 'z') ||
                    (c >= '0' && c <= '9');
            };
            for (size_t begin = 0u;
                begin + token.size() <= text.size();
                ++begin) {
                if (begin > 0u &&
                    isTokenCharacter(text[begin - 1u])) {
                    continue;
                }
                if (begin + token.size() < text.size() &&
                    isTokenCharacter(
                        text[begin + token.size()])) {
                    continue;
                }
                bool matched = true;
                for (size_t i = 0u; i < token.size(); ++i) {
                    if (TEXT::ToLowerAscii(text[begin + i]) !=
                        token[i]) {
                        matched = false;
                        break;
                    }
                }
                if (matched) {
                    return true;
                }
            }
            return false;
        }

        inline bool HasOpticalTransparencyCue(
            std::string_view text);

        inline bool HasThinTransparentCue(std::string_view text) {
            return
                HasOpticalTransparencyCue(text) ||
                ContainsLowerAscii(text, "foliage") ||
                ContainsLowerAscii(text, "leaf") ||
                ContainsLowerAscii(text, "leaves") ||
                ContainsLowerAscii(text, "ivy") ||
                ContainsLowerAscii(text, "hedge") ||
                ContainsLowerAscii(text, "grass") ||
                ContainsLowerAscii(text, "flower") ||
                ContainsLowerAscii(text, "curtain") ||
                ContainsLowerAscii(text, "cloth") ||
                ContainsLowerAscii(text, "fabric") ||
                ContainsLowerAscii(text, "transparent") ||
                ContainsLowerAscii(text, "translucent");
        }

        inline bool HasOpticalTransparencyCue(
            std::string_view text) {
            return
                ContainsLowerAsciiToken(text, "glass") ||
                ContainsLowerAsciiToken(text, "window") ||
                ContainsLowerAsciiToken(text, "pane") ||
                ContainsLowerAsciiToken(text, "fenetre") ||
                ContainsLowerAsciiToken(text, "fenster") ||
                ContainsLowerAsciiToken(text, "vitre") ||
                ContainsLowerAsciiToken(text, "windshield") ||
                ContainsLowerAsciiToken(text, "windscreen") ||
                ContainsLowerAsciiToken(text, "lens") ||
                ContainsLowerAsciiToken(text, "visor") ||
                ContainsLowerAsciiToken(text, "headlight") ||
                ContainsLowerAsciiToken(text, "taillight") ||
                ContainsLowerAsciiToken(text, "lightbulb") ||
                ContainsLowerAscii(text, "light_bulb") ||
                ContainsLowerAsciiToken(text, "transparent") ||
                ContainsLowerAsciiToken(text, "translucent");
        }

        inline bool HasExplicitAlphaSurface(const MaterialAsset& material) {
            return
                material.alphaMode != AlphaMode::Opaque ||
                material.baseColorFactor.w < 0.999f ||
                (material.featureBits & MATERIAL_FEATURES::AlphaMask) != 0u;
        }

        inline bool HasThinTransparentSurfaceHint(const MaterialAsset& material) {
            return
                (material.featureBits & MATERIAL_FEATURES::ThinTransparentSurface) != 0u ||
                HasThinTransparentCue(material.name);
        }

        inline bool HasAlphaMaskedSurface(const MaterialAsset& material) {
            return
                material.alphaMode == AlphaMode::Mask ||
                (material.featureBits & MATERIAL_FEATURES::AlphaMask) != 0u;
        }

        inline bool HasBlendedSurface(const MaterialAsset& material) {
            return material.alphaMode == AlphaMode::Blend;
        }

        inline bool HasExplicitThinTransparentSurface(const MaterialAsset& material) {
            return (material.featureBits & MATERIAL_FEATURES::ThinTransparentSurface) != 0u;
        }

        inline bool ShouldRenderDoubleSided(const MaterialAsset& material) {
            if (!material.doubleSided) {
                return false;
            }

            if (HasBlendedSurface(material)) {
                return true;
            }

            if (HasAlphaMaskedSurface(material)) {
                return HasThinTransparentSurfaceHint(material);
            }

            return HasExplicitThinTransparentSurface(material);
        }

    } // namespace MATERIAL_POLICY

} // namespace HIKARI
