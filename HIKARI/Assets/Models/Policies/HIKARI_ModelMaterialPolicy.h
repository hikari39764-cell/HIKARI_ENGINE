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
        constexpr uint32_t SpecularGlossCompatibility = 1u << 4;
    }

    namespace MATERIAL_POLICY {

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

        inline bool HasThinTransparentCue(std::string_view text) {
            return
                ContainsLowerAscii(text, "glass") ||
                ContainsLowerAscii(text, "window") ||
                ContainsLowerAscii(text, "pane") ||
                ContainsLowerAscii(text, "fenetre") ||
                ContainsLowerAscii(text, "fenster") ||
                ContainsLowerAscii(text, "vitre") ||
                ContainsLowerAscii(text, "headlight") ||
                ContainsLowerAscii(text, "taillight") ||
                ContainsLowerAscii(text, "lightbulb") ||
                ContainsLowerAscii(text, "light_bulb") ||
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
