#include "Render2D/HIKARI_GameUiTextLayout.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "ThirdParty/imgui/imstb_truetype.h"

#include "Render2D/HIKARI_DxTexture.h"
#include "Render2D/HIKARI_GameUiSignedDistanceField.h"

namespace HIKARI::GAME_UI {
    namespace {
        constexpr float kAtlasPixelHeight = 64.0f;
        constexpr int kSdfPadding = 8;
        constexpr unsigned char kSdfEdge = 128u;
        constexpr float kSdfPixelDistanceScale = 16.0f;
        constexpr uint32_t kReplacementCodepoint = 0xFFFdu;
        constexpr size_t kMaximumGlyphDraws = 32768u;

        struct Glyph {
            float u0 = 0.0f;
            float v0 = 0.0f;
            float u1 = 0.0f;
            float v1 = 0.0f;
            float width = 0.0f;
            float height = 0.0f;
            float xOffset = 0.0f;
            float yOffset = 0.0f;
            float advance = 0.0f;
            bool drawable = false;
        };

        struct FontAtlas {
            std::vector<unsigned char> bytes{};
            stbtt_fontinfo font{};
            std::set<uint32_t> codepoints{};
            std::unordered_map<uint32_t, Glyph> glyphs{};
            float scale = 0.0f;
            float ascent = 0.0f;
            float descent = 0.0f;
            float lineGap = 0.0f;
            uint32_t width = 0u;
            uint32_t height = 0u;
            uint32_t generation = 0u;
            int textureHandle = -1;
            bool initialized = false;
        };

        struct RasterGlyph {
            uint32_t codepoint = 0u;
            std::vector<unsigned char> pixels{};
            int width = 0;
            int height = 0;
            int xOffset = 0;
            int yOffset = 0;
            int advance = 0;
            int packedX = 0;
            int packedY = 0;
        };

        std::unordered_map<std::string, FontAtlas> gFonts{};

        std::vector<uint32_t> DecodeUtf8(std::string_view text) {
            std::vector<uint32_t> result{};
            result.reserve(text.size());
            const auto* bytes = reinterpret_cast<const unsigned char*>(
                text.data());
            size_t index = 0u;
            while (index < text.size()) {
                const unsigned char lead = bytes[index];
                if (lead < 0x80u) {
                    result.push_back(lead);
                    ++index;
                    continue;
                }

                uint32_t codepoint = 0u;
                size_t length = 0u;
                uint32_t minimum = 0u;
                if ((lead & 0xE0u) == 0xC0u) {
                    codepoint = lead & 0x1Fu;
                    length = 2u;
                    minimum = 0x80u;
                }
                else if ((lead & 0xF0u) == 0xE0u) {
                    codepoint = lead & 0x0Fu;
                    length = 3u;
                    minimum = 0x800u;
                }
                else if ((lead & 0xF8u) == 0xF0u) {
                    codepoint = lead & 0x07u;
                    length = 4u;
                    minimum = 0x10000u;
                }
                else {
                    result.push_back(kReplacementCodepoint);
                    ++index;
                    continue;
                }
                if (index + length > text.size()) {
                    result.push_back(kReplacementCodepoint);
                    break;
                }
                bool valid = true;
                for (size_t byteIndex = 1u;
                     byteIndex < length;
                     ++byteIndex) {
                    const unsigned char continuation =
                        bytes[index + byteIndex];
                    if ((continuation & 0xC0u) != 0x80u) {
                        valid = false;
                        break;
                    }
                    codepoint = (codepoint << 6u) |
                        (continuation & 0x3Fu);
                }
                if (!valid || codepoint < minimum ||
                    codepoint > 0x10FFFFu ||
                    (codepoint >= 0xD800u && codepoint <= 0xDFFFu)) {
                    result.push_back(kReplacementCodepoint);
                    ++index;
                    continue;
                }
                result.push_back(codepoint);
                index += length;
            }
            return result;
        }

        bool ReadFontBytes(
            const std::string& path,
            std::vector<unsigned char>& outBytes) {
            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            if (!stream) {
                return false;
            }
            const std::streamoff size = stream.tellg();
            if (size <= 0 || size > 256ll * 1024ll * 1024ll) {
                return false;
            }
            outBytes.resize(static_cast<size_t>(size));
            stream.seekg(0, std::ios::beg);
            stream.read(
                reinterpret_cast<char*>(outBytes.data()),
                static_cast<std::streamsize>(outBytes.size()));
            return stream.good();
        }

        bool InitializeFont(const std::string& path, FontAtlas& atlas) {
            if (atlas.initialized) {
                return true;
            }
            if (!ReadFontBytes(path, atlas.bytes)) {
                return false;
            }
            const int offset = stbtt_GetFontOffsetForIndex(
                atlas.bytes.data(),
                0);
            if (offset < 0 || !stbtt_InitFont(
                    &atlas.font,
                    atlas.bytes.data(),
                    offset)) {
                atlas.bytes.clear();
                return false;
            }
            atlas.scale = stbtt_ScaleForPixelHeight(
                &atlas.font,
                kAtlasPixelHeight);
            int ascent = 0;
            int descent = 0;
            int lineGap = 0;
            stbtt_GetFontVMetrics(
                &atlas.font,
                &ascent,
                &descent,
                &lineGap);
            atlas.ascent = static_cast<float>(ascent) * atlas.scale;
            atlas.descent = static_cast<float>(descent) * atlas.scale;
            atlas.lineGap = static_cast<float>(lineGap) * atlas.scale;
            atlas.initialized = std::isfinite(atlas.scale) &&
                atlas.scale > 0.0f;
            return atlas.initialized;
        }

        bool PackRasterGlyphs(
            std::vector<RasterGlyph>& glyphs,
            int extent) {
            int x = 1;
            int y = 1;
            int shelfHeight = 0;
            for (RasterGlyph& glyph : glyphs) {
                if (glyph.width <= 0 || glyph.height <= 0) {
                    continue;
                }
                if (x + glyph.width + 1 > extent) {
                    x = 1;
                    y += shelfHeight + 1;
                    shelfHeight = 0;
                }
                if (y + glyph.height + 1 > extent) {
                    return false;
                }
                glyph.packedX = x;
                glyph.packedY = y;
                x += glyph.width + 1;
                shelfHeight = (std::max)(shelfHeight, glyph.height);
            }
            return true;
        }

        bool RebuildAtlas(const std::string& path, FontAtlas& atlas) {
            std::vector<RasterGlyph> rasterGlyphs{};
            rasterGlyphs.reserve(atlas.codepoints.size());
            for (uint32_t codepoint : atlas.codepoints) {
                RasterGlyph raster{};
                raster.codepoint = codepoint;
                int leftSideBearing = 0;
                stbtt_GetCodepointHMetrics(
                    &atlas.font,
                    static_cast<int>(codepoint),
                    &raster.advance,
                    &leftSideBearing);
                int coverageWidth = 0;
                int coverageHeight = 0;
                int coverageXOffset = 0;
                int coverageYOffset = 0;
                unsigned char* coverage = stbtt_GetCodepointBitmap(
                    &atlas.font,
                    0.0f,
                    atlas.scale,
                    static_cast<int>(codepoint),
                    &coverageWidth,
                    &coverageHeight,
                    &coverageXOffset,
                    &coverageYOffset);
                if (coverage != nullptr && coverageWidth > 0 &&
                    coverageHeight > 0 &&
                    BuildGameUiSignedDistanceField(
                        coverage,
                        coverageWidth,
                        coverageHeight,
                        kSdfPadding,
                        kSdfEdge,
                        kSdfPixelDistanceScale,
                        raster.pixels,
                        raster.width,
                        raster.height)) {
                    raster.xOffset = coverageXOffset - kSdfPadding;
                    raster.yOffset = coverageYOffset - kSdfPadding;
                }
                if (coverage != nullptr) {
                    stbtt_FreeBitmap(coverage, nullptr);
                }
                rasterGlyphs.push_back(std::move(raster));
            }

            int extent = 512;
            while (extent <= 4096 &&
                   !PackRasterGlyphs(rasterGlyphs, extent)) {
                extent *= 2;
            }
            if (extent > 4096) {
                return false;
            }

            std::vector<uint8_t> rgba(
                static_cast<size_t>(extent) *
                    static_cast<size_t>(extent) * 4u,
                0u);
            std::unordered_map<uint32_t, Glyph> glyphs{};
            glyphs.reserve(rasterGlyphs.size());
            for (const RasterGlyph& raster : rasterGlyphs) {
                Glyph glyph{};
                glyph.width = static_cast<float>(raster.width);
                glyph.height = static_cast<float>(raster.height);
                glyph.xOffset = static_cast<float>(raster.xOffset);
                glyph.yOffset = static_cast<float>(raster.yOffset);
                glyph.advance = static_cast<float>(raster.advance) *
                    atlas.scale;
                glyph.drawable = !raster.pixels.empty();
                if (glyph.drawable) {
                    glyph.u0 = static_cast<float>(raster.packedX) /
                        static_cast<float>(extent);
                    glyph.v0 = static_cast<float>(raster.packedY) /
                        static_cast<float>(extent);
                    glyph.u1 = static_cast<float>(
                        raster.packedX + raster.width) /
                        static_cast<float>(extent);
                    glyph.v1 = static_cast<float>(
                        raster.packedY + raster.height) /
                        static_cast<float>(extent);
                    for (int row = 0; row < raster.height; ++row) {
                        for (int column = 0;
                             column < raster.width;
                             ++column) {
                            const size_t sourceIndex =
                                static_cast<size_t>(row) *
                                    static_cast<size_t>(raster.width) +
                                static_cast<size_t>(column);
                            const size_t destinationIndex =
                                (static_cast<size_t>(raster.packedY + row) *
                                    static_cast<size_t>(extent) +
                                 static_cast<size_t>(raster.packedX + column)) *
                                4u;
                            rgba[destinationIndex + 0u] = 255u;
                            rgba[destinationIndex + 1u] = 255u;
                            rgba[destinationIndex + 2u] = 255u;
                            rgba[destinationIndex + 3u] =
                                raster.pixels[sourceIndex];
                        }
                    }
                }
                glyphs.emplace(raster.codepoint, glyph);
            }

            const uint32_t nextGeneration = atlas.generation + 1u;
            const std::string textureName =
                "GameUiFontAtlas:" +
                std::to_string(std::hash<std::string>{}(path)) + ":" +
                std::to_string(nextGeneration);
            const int newTexture = DXTEX::DxTextureManager::CreateRgbaTexture(
                textureName,
                static_cast<uint32_t>(extent),
                static_cast<uint32_t>(extent),
                rgba,
                DXTEX::TextureColorSpace::Linear);
            if (newTexture < 0) {
                return false;
            }
            const int oldTexture = atlas.textureHandle;
            atlas.textureHandle = newTexture;
            atlas.generation = nextGeneration;
            atlas.width = static_cast<uint32_t>(extent);
            atlas.height = static_cast<uint32_t>(extent);
            atlas.glyphs = std::move(glyphs);
            if (oldTexture >= 0 && oldTexture != newTexture) {
                DXTEX::DxTextureManager::ReleaseTextureDeferred(oldTexture);
            }
            return true;
        }

        bool EnsureGlyphs(
            const std::string& path,
            const std::set<uint32_t>& requested,
            FontAtlas*& outAtlas) {
            FontAtlas& atlas = gFonts[path];
            if (!InitializeFont(path, atlas)) {
                return false;
            }
            const size_t previousCount = atlas.codepoints.size();
            atlas.codepoints.insert(static_cast<uint32_t>('?'));
            atlas.codepoints.insert(kReplacementCodepoint);
            atlas.codepoints.insert(requested.begin(), requested.end());
            const bool textureInvalid = atlas.textureHandle < 0 ||
                !DXTEX::DxTextureManager::IsTextureHandleValid(
                    atlas.textureHandle);
            if ((atlas.codepoints.size() != previousCount ||
                 textureInvalid) &&
                !RebuildAtlas(path, atlas)) {
                return false;
            }
            outAtlas = &atlas;
            return true;
        }

        const Glyph* FindGlyph(
            const FontAtlas& atlas,
            uint32_t codepoint) {
            auto found = atlas.glyphs.find(codepoint);
            if (found != atlas.glyphs.end()) {
                return &found->second;
            }
            found = atlas.glyphs.find(kReplacementCodepoint);
            if (found != atlas.glyphs.end()) {
                return &found->second;
            }
            found = atlas.glyphs.find(static_cast<uint32_t>('?'));
            return found != atlas.glyphs.end() ? &found->second : nullptr;
        }

        float AdvanceFor(
            const FontAtlas& atlas,
            uint32_t codepoint,
            uint32_t nextCodepoint,
            float scale,
            float letterSpacing) {
            const Glyph* glyph = FindGlyph(atlas, codepoint);
            if (glyph == nullptr) {
                return 0.0f;
            }
            const int kern = nextCodepoint == 0u
                ? 0
                : stbtt_GetCodepointKernAdvance(
                    &atlas.font,
                    static_cast<int>(codepoint),
                    static_cast<int>(nextCodepoint));
            return glyph->advance * scale +
                static_cast<float>(kern) * atlas.scale * scale +
                letterSpacing;
        }

        struct TextLine {
            std::vector<uint32_t> codepoints{};
            float width = 0.0f;
        };

        std::vector<TextLine> BuildLines(
            const FontAtlas& atlas,
            const std::vector<uint32_t>& codepoints,
            const GameProjectUiTextRequest& request,
            float glyphScale) {
            std::vector<TextLine> lines{};
            TextLine current{};
            auto flush = [&]() {
                lines.push_back(std::move(current));
                current = {};
            };
            for (size_t index = 0u; index < codepoints.size(); ++index) {
                const uint32_t codepoint = codepoints[index];
                if (codepoint == '\r') {
                    continue;
                }
                if (codepoint == '\n') {
                    flush();
                    continue;
                }
                const uint32_t next = index + 1u < codepoints.size()
                    ? codepoints[index + 1u]
                    : 0u;
                const float advance = AdvanceFor(
                    atlas,
                    codepoint,
                    next,
                    glyphScale,
                    request.letterSpacing);
                if (request.wrap == GameProjectUiTextWrap::Character &&
                    !current.codepoints.empty() &&
                    current.width + advance > request.maxWidth) {
                    flush();
                }
                current.codepoints.push_back(codepoint);
                current.width += advance;
            }
            if (!current.codepoints.empty() || lines.empty()) {
                flush();
            }
            return lines;
        }

        float HorizontalOffset(
            GameProjectUiTextAlign align,
            float availableWidth,
            float lineWidth) {
            switch (align) {
            case GameProjectUiTextAlign::Center:
                return (availableWidth - lineWidth) * 0.5f;
            case GameProjectUiTextAlign::Right:
                return availableWidth - lineWidth;
            case GameProjectUiTextAlign::Left:
            default:
                return 0.0f;
            }
        }

        float VerticalOffset(
            GameProjectUiVerticalAlign align,
            float availableHeight,
            float textHeight) {
            switch (align) {
            case GameProjectUiVerticalAlign::Center:
                return (availableHeight - textHeight) * 0.5f;
            case GameProjectUiVerticalAlign::Bottom:
                return availableHeight - textHeight;
            case GameProjectUiVerticalAlign::Top:
            default:
                return 0.0f;
            }
        }

        bool AppendTextDraws(
            const GameUiTextDraw& text,
            FontAtlas& atlas,
            std::vector<GameUiTextGlyphDraw>& outDraws) {
            const std::vector<uint32_t> codepoints = DecodeUtf8(
                text.utf8Text);
            const float glyphScale = text.request.pixelHeight /
                kAtlasPixelHeight;
            std::vector<TextLine> lines = BuildLines(
                atlas,
                codepoints,
                text.request,
                glyphScale);
            const float lineHeight = text.request.pixelHeight *
                text.request.lineHeightMultiplier;
            const float textHeight = lines.empty()
                ? 0.0f
                : text.request.pixelHeight +
                    static_cast<float>(lines.size() - 1u) * lineHeight;
            const float verticalOffset = VerticalOffset(
                text.request.verticalAlign,
                text.request.maxHeight,
                textHeight);
            const size_t revealableCount = [&]() {
                size_t count = 0u;
                for (const TextLine& line : lines) {
                    count += line.codepoints.size();
                }
                return count;
            }();
            const float revealed = static_cast<float>(revealableCount) *
                text.request.revealAmount;
            size_t revealIndex = 0u;
            uint32_t glyphSequence = 0u;
            for (size_t lineIndex = 0u;
                 lineIndex < lines.size();
                 ++lineIndex) {
                const TextLine& line = lines[lineIndex];
                float cursorX = text.request.x + HorizontalOffset(
                    text.request.horizontalAlign,
                    text.request.maxWidth,
                    line.width);
                const float baselineY = text.request.y + verticalOffset +
                    static_cast<float>(lineIndex) * lineHeight +
                    atlas.ascent * glyphScale;
                for (size_t index = 0u;
                     index < line.codepoints.size();
                     ++index, ++revealIndex) {
                    const uint32_t codepoint = line.codepoints[index];
                    const uint32_t next = index + 1u < line.codepoints.size()
                        ? line.codepoints[index + 1u]
                        : 0u;
                    const Glyph* glyph = FindGlyph(atlas, codepoint);
                    const float advance = AdvanceFor(
                        atlas,
                        codepoint,
                        next,
                        glyphScale,
                        text.request.letterSpacing);
                    const float revealAlpha = std::clamp(
                        revealed - static_cast<float>(revealIndex),
                        0.0f,
                        1.0f);
                    if (glyph != nullptr && glyph->drawable &&
                        revealAlpha > 0.0f &&
                        outDraws.size() < kMaximumGlyphDraws) {
                        GameUiTextGlyphDraw draw{};
                        draw.textureHandle = atlas.textureHandle;
                        draw.u0 = glyph->u0;
                        draw.v0 = glyph->v0;
                        draw.u1 = glyph->u1;
                        draw.v1 = glyph->v1;
                        draw.submissionSequence = text.submissionSequence;
                        draw.request.sourceId = text.request.sourceId;
                        draw.request.order = text.request.order;
                        draw.request.flags = text.request.flags;
                        draw.request.referenceWidth =
                            text.request.referenceWidth;
                        draw.request.referenceHeight =
                            text.request.referenceHeight;
                        draw.request.x = cursorX +
                            glyph->xOffset * glyphScale;
                        draw.request.y = baselineY +
                            glyph->yOffset * glyphScale;
                        draw.request.width = glyph->width * glyphScale;
                        draw.request.height = glyph->height * glyphScale;
                        const float pivotCanvasX = text.request.x +
                            text.request.maxWidth * text.request.pivotX;
                        const float pivotCanvasY = text.request.y +
                            text.request.maxHeight * text.request.pivotY;
                        draw.request.pivotX =
                            (pivotCanvasX - draw.request.x) /
                            (std::max)(draw.request.width, 1.0e-4f);
                        draw.request.pivotY =
                            (pivotCanvasY - draw.request.y) /
                            (std::max)(draw.request.height, 1.0e-4f);
                        draw.request.rotationRadians =
                            text.request.rotationRadians;
                        draw.request.fillAmount = 1.0f;
                        draw.request.effectProgress =
                            text.request.effectProgress;
                        draw.request.effectIntensity =
                            text.request.outlineWidth;
                        draw.request.timeSeconds =
                            text.request.timeSeconds;
                        draw.request.directionX =
                            text.request.shadowOffsetX;
                        draw.request.directionY =
                            text.request.shadowOffsetY;
                        if (text.style.glowIntensity > 0.0f &&
                            text.style.glowWidth > 0.0f &&
                            text.style.glowColor.a > 0.0f &&
                            outDraws.size() < kMaximumGlyphDraws) {
                            GameUiTextGlyphDraw glow = draw;
                            glow.glyphSequence = glyphSequence++;
                            glow.request.effectProgress = 1.0f;
                            glow.request.effectIntensity =
                                text.style.glowWidth;
                            glow.request.timeSeconds =
                                text.style.glowIntensity;
                            glow.request.tint = text.style.glowColor;
                            glow.request.tint.a *= revealAlpha;
                            glow.request.effectColor = {};
                            glow.request.highlightColor = {};
                            glow.request.effect =
                                GameProjectUiEffect::TextSdfGlow;
                            outDraws.push_back(std::move(glow));
                        }
                        if (outDraws.size() >= kMaximumGlyphDraws) {
                            cursorX += advance;
                            continue;
                        }
                        draw.glyphSequence = glyphSequence++;
                        draw.request.tint = text.request.textColor;
                        draw.request.tint.a *= revealAlpha;
                        draw.request.effectColor =
                            text.request.outlineColor;
                        draw.request.effectColor.a *= revealAlpha;
                        draw.request.highlightColor =
                            text.request.shadowColor;
                        draw.request.highlightColor.a *= revealAlpha;
                        draw.request.effect = GameProjectUiEffect::TextSdf;
                        outDraws.push_back(std::move(draw));
                    }
                    cursorX += advance;
                }
            }
            return true;
        }
    }

    bool BuildGameUiTextGlyphDraws(
        const std::vector<GameUiTextDraw>& texts,
        std::vector<GameUiTextGlyphDraw>& outDraws) noexcept {
        try {
            outDraws.clear();
            std::unordered_map<std::string, std::set<uint32_t>> requested{};
            for (const GameUiTextDraw& text : texts) {
                std::set<uint32_t>& codepoints = requested[
                    text.absoluteFontAssetPath];
                const std::vector<uint32_t> decoded = DecodeUtf8(
                    text.utf8Text);
                codepoints.insert(decoded.begin(), decoded.end());
            }
            for (const auto& [path, codepoints] : requested) {
                FontAtlas* atlas = nullptr;
                if (!EnsureGlyphs(path, codepoints, atlas)) {
                    return false;
                }
            }
            for (const GameUiTextDraw& text : texts) {
                const auto found = gFonts.find(text.absoluteFontAssetPath);
                if (found == gFonts.end() ||
                    !AppendTextDraws(text, found->second, outDraws)) {
                    return false;
                }
            }
            return true;
        }
        catch (...) {
            outDraws.clear();
            return false;
        }
    }

    void ShutdownGameUiTextLayout() noexcept {
        for (auto& [_, atlas] : gFonts) {
            if (atlas.textureHandle >= 0 &&
                DXTEX::DxTextureManager::IsTextureHandleValid(
                    atlas.textureHandle)) {
                DXTEX::DxTextureManager::ReleaseTextureDeferred(
                    atlas.textureHandle);
            }
        }
        gFonts.clear();
    }

} // namespace HIKARI::GAME_UI
