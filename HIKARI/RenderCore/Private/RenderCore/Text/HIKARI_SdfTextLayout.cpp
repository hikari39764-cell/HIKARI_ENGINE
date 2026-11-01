#include "RenderCore/Text/HIKARI_SdfTextLayout.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Diagnostics/HIKARI_CpuFrameProfiler.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "imstb_truetype.h"

#include "Core/Text/HIKARI_UtfPath.h"
#include "RenderCore/Resources/HIKARI_TextureResourceSystem.h"
#include "RenderCore/Text/Internal/HIKARI_SdfTextAtlasGeneration.h"
#include "RenderCore/Text/HIKARI_SignedDistanceField.h"

namespace HIKARI::TEXT_RENDERING {
namespace {
constexpr int kSdfPadding = 8;
constexpr uint32_t kReplacementCodepoint = 0xFFFdu;

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
    std::shared_ptr<INTERNAL::SdfTextAtlasGeneration> textureGeneration{};
    bool initialized = false;
    uint64_t sourceRevision = 0;
};

struct SdfTextLayoutLeaseStorage {
    std::vector<std::shared_ptr<const INTERNAL::SdfTextAtlasGeneration>> generations{};
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

struct TextLine {
    std::vector<size_t> codepointIndices{};
    size_t startIndex = 0;
    float width = 0.0f;
};

std::unordered_map<std::string, FontAtlas> gFonts{};

using MATH::IsFinite;

bool TryAddFinite(float lhs, float rhs, float& out) noexcept {
    if (!IsFinite(lhs) || !IsFinite(rhs))
        return false;
    out = lhs + rhs;
    return IsFinite(out);
}

bool TrySubtractFinite(float lhs, float rhs, float& out) noexcept {
    if (!IsFinite(lhs) || !IsFinite(rhs))
        return false;
    out = lhs - rhs;
    return IsFinite(out);
}

bool TryMultiplyFinite(float lhs, float rhs, float& out) noexcept {
    if (!IsFinite(lhs) || !IsFinite(rhs))
        return false;
    out = lhs * rhs;
    return IsFinite(out);
}

bool TryDivideFinite(float lhs, float rhs, float& out) noexcept {
    if (!IsFinite(lhs) || !IsFinite(rhs) || rhs == 0.0f) {
        return false;
    }
    out = lhs / rhs;
    return IsFinite(out);
}

bool IsValidRequest(const SdfTextLayoutRequest& request) noexcept {
    if (request.absoluteFontAssetPath.empty() || request.utf8Text.size() > kSdfTextMaximumUtf8Bytes ||
        !IsFinite(request.x) || !IsFinite(request.y) || !IsFinite(request.fontHeight) || !IsFinite(request.maxWidth) ||
        !IsFinite(request.maxHeight) || !IsFinite(request.lineHeightMultiplier) || !IsFinite(request.letterSpacing) ||
        !IsFinite(request.revealAmount) || request.fontHeight <= 0.0f || request.maxWidth <= 0.0f ||
        request.maxHeight <= 0.0f || request.lineHeightMultiplier <= 0.0f ||
        static_cast<uint32_t>(request.horizontalAlign) > static_cast<uint32_t>(SdfTextHorizontalAlign::Right) ||
        static_cast<uint32_t>(request.verticalAlign) > static_cast<uint32_t>(SdfTextVerticalAlign::Bottom) ||
        static_cast<uint32_t>(request.wrap) > static_cast<uint32_t>(SdfTextWrap::Character)) {
        return false;
    }

    // These products are consumed for every request, including a
    // one-glyph, single-line request. Reject an unrepresentable
    // layout scale/line height before touching the atlas cache.
    float glyphScale = 0.0f;
    float lineHeight = 0.0f;
    return TryDivideFinite(request.fontHeight, kSdfTextAtlasPixelHeight, glyphScale) &&
           TryMultiplyFinite(request.fontHeight, request.lineHeightMultiplier, lineHeight);
}

std::vector<uint32_t> DecodeUtf8(std::string_view text, std::vector<size_t>* offsets = nullptr) {
    std::vector<uint32_t> result{};
    result.reserve(text.size());
    const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
    size_t index = 0u;
    while (index < text.size()) {
        if (offsets)
            offsets->push_back(index);
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
        } else if ((lead & 0xF0u) == 0xE0u) {
            codepoint = lead & 0x0Fu;
            length = 3u;
            minimum = 0x800u;
        } else if ((lead & 0xF8u) == 0xF0u) {
            codepoint = lead & 0x07u;
            length = 4u;
            minimum = 0x10000u;
        } else {
            result.push_back(kReplacementCodepoint);
            ++index;
            continue;
        }
        if (index + length > text.size()) {
            result.push_back(kReplacementCodepoint);
            break;
        }
        bool valid = true;
        for (size_t byteIndex = 1u; byteIndex < length; ++byteIndex) {
            const unsigned char continuation = bytes[index + byteIndex];
            if ((continuation & 0xC0u) != 0x80u) {
                valid = false;
                break;
            }
            codepoint = (codepoint << 6u) | (continuation & 0x3Fu);
        }
        if (!valid || codepoint < minimum || codepoint > 0x10FFFFu || (codepoint >= 0xD800u && codepoint <= 0xDFFFu)) {
            result.push_back(kReplacementCodepoint);
            ++index;
            continue;
        }
        result.push_back(codepoint);
        index += length;
    }
    if (offsets)
        offsets->push_back(text.size());
    return result;
}

bool ReadFontBytes(const std::string& path, std::vector<unsigned char>& outBytes) {
    outBytes.clear();
    const std::filesystem::path nativePath = TEXT::PathFromUtf8(path);
    std::ifstream stream(nativePath, std::ios::binary | std::ios::ate);
    if (!stream)
        return false;
    const std::streamoff size = stream.tellg();
    if (size < 12 || size > 256ll * 1024ll * 1024ll)
        return false;
    std::vector<unsigned char> bytes(static_cast<size_t>(size));
    stream.seekg(0, std::ios::beg);
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!stream.good())
        return false;
    outBytes = std::move(bytes);
    return true;
}

bool InitializeFont(const std::string& path, FontAtlas& atlas) {
    if (atlas.initialized)
        return true;
    if (!ReadFontBytes(path, atlas.bytes))
        return false;
    const int offset = stbtt_GetFontOffsetForIndex(atlas.bytes.data(), 0);
    if (offset < 0 || !stbtt_InitFont(&atlas.font, atlas.bytes.data(), offset)) {
        atlas.bytes.clear();
        return false;
    }
    atlas.scale = stbtt_ScaleForPixelHeight(&atlas.font, kSdfTextAtlasPixelHeight);
    int ascent = 0;
    int descent = 0;
    int lineGap = 0;
    stbtt_GetFontVMetrics(&atlas.font, &ascent, &descent, &lineGap);
    atlas.ascent = static_cast<float>(ascent) * atlas.scale;
    atlas.descent = static_cast<float>(descent) * atlas.scale;
    atlas.lineGap = static_cast<float>(lineGap) * atlas.scale;
    atlas.initialized = IsFinite(atlas.scale) && atlas.scale > 0.0f;
    return atlas.initialized;
}

bool PackRasterGlyphs(std::vector<RasterGlyph>& glyphs, int extent) {
    int x = 1;
    int y = 1;
    int shelfHeight = 0;
    for (RasterGlyph& glyph : glyphs) {
        if (glyph.width <= 0 || glyph.height <= 0)
            continue;
        if (x + glyph.width + 1 > extent) {
            x = 1;
            y += shelfHeight + 1;
            shelfHeight = 0;
        }
        if (y + glyph.height + 1 > extent)
            return false;
        glyph.packedX = x;
        glyph.packedY = y;
        x += glyph.width + 1;
        shelfHeight = (std::max)(shelfHeight, glyph.height);
    }
    return true;
}

bool RebuildAtlas(const std::string& path, FontAtlas& atlas, SdfTextLayoutStats& stats) {
    std::vector<RasterGlyph> rasterGlyphs{};
    rasterGlyphs.reserve(atlas.codepoints.size());
    for (uint32_t codepoint : atlas.codepoints) {
        RasterGlyph raster{};
        raster.codepoint = codepoint;
        int leftSideBearing = 0;
        stbtt_GetCodepointHMetrics(&atlas.font, static_cast<int>(codepoint), &raster.advance, &leftSideBearing);
        int coverageWidth = 0;
        int coverageHeight = 0;
        int coverageXOffset = 0;
        int coverageYOffset = 0;
        unsigned char* coverage =
            stbtt_GetCodepointBitmap(&atlas.font, 0.0f, atlas.scale, static_cast<int>(codepoint), &coverageWidth,
                                     &coverageHeight, &coverageXOffset, &coverageYOffset);
        if (coverage != nullptr && coverageWidth > 0 && coverageHeight > 0 &&
            BuildSignedDistanceField(coverage, coverageWidth, coverageHeight, kSdfPadding, kSdfTextOnEdgeValue,
                                     kSdfTextPixelDistanceScale, raster.pixels, raster.width, raster.height)) {
            raster.xOffset = coverageXOffset - kSdfPadding;
            raster.yOffset = coverageYOffset - kSdfPadding;
        }
        if (coverage != nullptr)
            stbtt_FreeBitmap(coverage, nullptr);
        rasterGlyphs.push_back(std::move(raster));
    }

    int extent = 512;
    while (extent <= 4096 && !PackRasterGlyphs(rasterGlyphs, extent)) {
        extent *= 2;
    }
    if (extent > 4096)
        return false;

    std::vector<uint8_t> rgba(static_cast<size_t>(extent) * static_cast<size_t>(extent) * 4u, 0u);
    std::unordered_map<uint32_t, Glyph> glyphs{};
    glyphs.reserve(rasterGlyphs.size());
    for (const RasterGlyph& raster : rasterGlyphs) {
        Glyph glyph{};
        glyph.width = static_cast<float>(raster.width);
        glyph.height = static_cast<float>(raster.height);
        glyph.xOffset = static_cast<float>(raster.xOffset);
        glyph.yOffset = static_cast<float>(raster.yOffset);
        glyph.advance = static_cast<float>(raster.advance) * atlas.scale;
        glyph.drawable = !raster.pixels.empty();
        if (glyph.drawable) {
            glyph.u0 = static_cast<float>(raster.packedX) / static_cast<float>(extent);
            glyph.v0 = static_cast<float>(raster.packedY) / static_cast<float>(extent);
            glyph.u1 = static_cast<float>(raster.packedX + raster.width) / static_cast<float>(extent);
            glyph.v1 = static_cast<float>(raster.packedY + raster.height) / static_cast<float>(extent);
            for (int row = 0; row < raster.height; ++row) {
                for (int column = 0; column < raster.width; ++column) {
                    const size_t sourceIndex =
                        static_cast<size_t>(row) * static_cast<size_t>(raster.width) + static_cast<size_t>(column);
                    const size_t destinationIndex =
                        (static_cast<size_t>(raster.packedY + row) * static_cast<size_t>(extent) +
                         static_cast<size_t>(raster.packedX + column)) *
                        4u;
                    rgba[destinationIndex + 0u] = 255u;
                    rgba[destinationIndex + 1u] = 255u;
                    rgba[destinationIndex + 2u] = 255u;
                    rgba[destinationIndex + 3u] = raster.pixels[sourceIndex];
                }
            }
        }
        glyphs.emplace(raster.codepoint, glyph);
    }

    const uint32_t nextGeneration = atlas.generation + 1u;
    const std::string textureName =
        "SdfFontAtlas:" + std::to_string(std::hash<std::string>{}(path)) + ":" + std::to_string(nextGeneration);
    RENDER3D::TextureResourceHandle newTexture =
        RENDER3D::CreateRgbaTextureResource(textureName, static_cast<uint32_t>(extent), static_cast<uint32_t>(extent),
                                            rgba, RENDER3D::TextureResourceColorSpace::Linear);
    if (!newTexture)
        return false;

    std::shared_ptr<INTERNAL::SdfTextAtlasGeneration> newGeneration{};
    try {
        newGeneration = std::make_shared<INTERNAL::SdfTextAtlasGeneration>(newTexture, nextGeneration,
                                                                           &RENDER3D::ReleaseTextureResource);
    } catch (...) {
        (void)RENDER3D::ReleaseTextureResource(newTexture);
        return false;
    }
    atlas.textureGeneration = std::move(newGeneration);
    atlas.generation = nextGeneration;
    atlas.width = static_cast<uint32_t>(extent);
    atlas.height = static_cast<uint32_t>(extent);
    atlas.glyphs = std::move(glyphs);
    ++stats.atlasRebuildCount;
    return true;
}

bool EnsureGlyphs(const std::string& path, const std::set<uint32_t>& requested, uint64_t sourceRevision,
                  FontAtlas*& outAtlas, SdfTextLayoutStats& stats) {
    FontAtlas& published = gFonts[path];
    FontAtlas replacement;
    const bool reload = sourceRevision && sourceRevision != published.sourceRevision;
    // 再インポートでも世代を継続し、旧 lease と同じ texture cache key を再利用しない。
    replacement.generation = published.generation;
    FontAtlas& atlas = reload ? replacement : published;
    if (!InitializeFont(path, atlas))
        return false;
    const std::set<uint32_t> previousCodepoints = atlas.codepoints;
    atlas.codepoints.insert(static_cast<uint32_t>('?'));
    atlas.codepoints.insert(kReplacementCodepoint);
    atlas.codepoints.insert(requested.begin(), requested.end());
    const RENDER3D::TextureResourceHandle atlasTexture =
        atlas.textureGeneration ? atlas.textureGeneration->GetTexture() : RENDER3D::TextureResourceHandle{};
    const bool textureInvalid = !atlasTexture || !RENDER3D::IsTextureResourceValid(atlasTexture);
    if ((atlas.codepoints != previousCodepoints || textureInvalid) && !RebuildAtlas(path, atlas, stats)) {
        // Atlas 更新は transactional に扱う。容量超過などで失敗した
        // codepoint 集合を cache に残すと、次回が誤って ready になる。
        atlas.codepoints = previousCodepoints;
        return false;
    }
    if (reload) {
        replacement.sourceRevision = sourceRevision;
        published = std::move(replacement);
    }
    outAtlas = &published;
    return true;
}

const Glyph* FindGlyph(const FontAtlas& atlas, uint32_t codepoint) {
    auto found = atlas.glyphs.find(codepoint);
    if (found != atlas.glyphs.end())
        return &found->second;
    found = atlas.glyphs.find(kReplacementCodepoint);
    if (found != atlas.glyphs.end())
        return &found->second;
    found = atlas.glyphs.find(static_cast<uint32_t>('?'));
    return found != atlas.glyphs.end() ? &found->second : nullptr;
}

bool ComputeAdvance(const FontAtlas& atlas, uint32_t codepoint, uint32_t nextCodepoint, float scale,
                    float letterSpacing, float& outAdvance) noexcept {
    const Glyph* glyph = FindGlyph(atlas, codepoint);
    outAdvance = 0.0f;
    if (glyph == nullptr)
        return true;
    if (!IsFinite(glyph->advance) || !IsFinite(atlas.scale)) {
        return false;
    }
    const int kern = nextCodepoint == 0u ? 0
                                         : stbtt_GetCodepointKernAdvance(&atlas.font, static_cast<int>(codepoint),
                                                                         static_cast<int>(nextCodepoint));
    float glyphAdvance = 0.0f;
    float kernAdvance = 0.0f;
    float kernScaled = 0.0f;
    if (!TryMultiplyFinite(glyph->advance, scale, glyphAdvance) ||
        !TryMultiplyFinite(static_cast<float>(kern), atlas.scale, kernAdvance) ||
        !TryMultiplyFinite(kernAdvance, scale, kernScaled) || !TryAddFinite(glyphAdvance, kernScaled, outAdvance) ||
        !TryAddFinite(outAdvance, letterSpacing, outAdvance)) {
        outAdvance = 0.0f;
        return false;
    }
    return true;
}

bool BuildLines(const FontAtlas& atlas, const std::vector<uint32_t>& codepoints, const SdfTextLayoutRequest& request,
                float glyphScale, std::vector<TextLine>& outLines) {
    outLines.clear();
    TextLine current{};
    auto flush = [&]() {
        outLines.push_back(std::move(current));
        current = {};
    };
    for (size_t index = 0u; index < codepoints.size(); ++index) {
        const uint32_t codepoint = codepoints[index];
        if (codepoint == '\r')
            continue;
        if (codepoint == '\n') {
            flush();
            current.startIndex = index + 1;
            continue;
        }
        const uint32_t next = index + 1u < codepoints.size() ? codepoints[index + 1u] : 0u;
        float advance = 0.0f;
        if (!ComputeAdvance(atlas, codepoint, next, glyphScale, request.letterSpacing, advance)) {
            return false;
        }
        float widthWithAdvance = 0.0f;
        if (!TryAddFinite(current.width, advance, widthWithAdvance)) {
            return false;
        }
        if (request.wrap == SdfTextWrap::Character && !current.codepointIndices.empty() &&
            widthWithAdvance > request.maxWidth) {
            flush();
            current.startIndex = index;
            if (!TryAddFinite(current.width, advance, widthWithAdvance)) {
                return false;
            }
        }
        current.codepointIndices.push_back(index);
        current.width = widthWithAdvance;
    }
    if (!current.codepointIndices.empty() || outLines.empty() || current.startIndex == codepoints.size())
        flush();
    return true;
}

bool ComputeHorizontalOffset(SdfTextHorizontalAlign align, float availableWidth, float lineWidth,
                             float& outOffset) noexcept {
    outOffset = 0.0f;
    switch (align) {
    case SdfTextHorizontalAlign::Center: {
        float difference = 0.0f;
        return TrySubtractFinite(availableWidth, lineWidth, difference) &&
               TryMultiplyFinite(difference, 0.5f, outOffset);
    }
    case SdfTextHorizontalAlign::Right: return TrySubtractFinite(availableWidth, lineWidth, outOffset);
    case SdfTextHorizontalAlign::Left:
    default: return true;
    }
}

bool ComputeVerticalOffset(SdfTextVerticalAlign align, float availableHeight, float textHeight,
                           float& outOffset) noexcept {
    outOffset = 0.0f;
    switch (align) {
    case SdfTextVerticalAlign::Center: {
        float difference = 0.0f;
        return TrySubtractFinite(availableHeight, textHeight, difference) &&
               TryMultiplyFinite(difference, 0.5f, outOffset);
    }
    case SdfTextVerticalAlign::Bottom: return TrySubtractFinite(availableHeight, textHeight, outOffset);
    case SdfTextVerticalAlign::Top:
    default: return true;
    }
}

bool AppendTextDraws(const SdfTextLayoutRequest& text, uint32_t requestIndex, FontAtlas& atlas,
                     uint32_t maximumGlyphCount, std::vector<SdfTextGlyphDraw>& outDraws, SdfTextLayoutStats& stats,
                     std::vector<SdfTextCaretStop>* stops) {
    std::vector<size_t> offsets;
    const std::vector<uint32_t> codepoints = DecodeUtf8(text.utf8Text, stops ? &offsets : nullptr);
    float glyphScale = 0.0f;
    if (!TryDivideFinite(text.fontHeight, kSdfTextAtlasPixelHeight, glyphScale)) {
        return false;
    }
    std::vector<TextLine> lines{};
    if (!BuildLines(atlas, codepoints, text, glyphScale, lines)) {
        return false;
    }
    float lineHeight = 0.0f;
    if (!TryMultiplyFinite(text.fontHeight, text.lineHeightMultiplier, lineHeight)) {
        return false;
    }
    float textHeight = 0.0f;
    if (!lines.empty()) {
        float additionalHeight = 0.0f;
        if (!TryMultiplyFinite(static_cast<float>(lines.size() - 1u), lineHeight, additionalHeight) ||
            !TryAddFinite(text.fontHeight, additionalHeight, textHeight)) {
            return false;
        }
    }
    float verticalOffset = 0.0f;
    if (!ComputeVerticalOffset(text.verticalAlign, text.maxHeight, textHeight, verticalOffset)) {
        return false;
    }
    const size_t revealableCount = [&]() {
        size_t count = 0u;
        for (const TextLine& line : lines) {
            count += line.codepointIndices.size();
        }
        return count;
    }();
    float revealed = 0.0f;
    if (!TryMultiplyFinite(static_cast<float>(revealableCount), std::clamp(text.revealAmount, 0.0f, 1.0f), revealed)) {
        return false;
    }
    size_t revealIndex = 0u;
    uint32_t glyphSequence = 0u;
    for (size_t lineIndex = 0u; lineIndex < lines.size(); ++lineIndex) {
        const TextLine& line = lines[lineIndex];
        float horizontalOffset = 0.0f;
        if (!ComputeHorizontalOffset(text.horizontalAlign, text.maxWidth, line.width, horizontalOffset)) {
            return false;
        }
        float cursorX = 0.0f;
        if (!TryAddFinite(text.x, horizontalOffset, cursorX)) {
            return false;
        }
        float lineOffset = 0.0f;
        float ascentOffset = 0.0f;
        float baselineY = 0.0f;
        if (!TryMultiplyFinite(static_cast<float>(lineIndex), lineHeight, lineOffset) ||
            !TryMultiplyFinite(atlas.ascent, glyphScale, ascentOffset) ||
            !TryAddFinite(text.y, verticalOffset, baselineY) || !TryAddFinite(baselineY, lineOffset, baselineY) ||
            !TryAddFinite(baselineY, ascentOffset, baselineY)) {
            return false;
        }
        const float caretY = baselineY - ascentOffset;
        if (stops)
            stops->push_back({requestIndex,
                              offsets[line.codepointIndices.empty() ? line.startIndex : line.codepointIndices.front()],
                              cursorX, caretY, text.fontHeight});
        for (size_t index = 0u; index < line.codepointIndices.size(); ++index, ++revealIndex) {
            const size_t sourceIndex = line.codepointIndices[index];
            const uint32_t codepoint = codepoints[sourceIndex];
            const uint32_t next =
                index + 1u < line.codepointIndices.size() ? codepoints[line.codepointIndices[index + 1u]] : 0u;
            const Glyph* glyph = FindGlyph(atlas, codepoint);
            float advance = 0.0f;
            if (!ComputeAdvance(atlas, codepoint, next, glyphScale, text.letterSpacing, advance)) {
                return false;
            }
            float revealRemaining = 0.0f;
            if (!TrySubtractFinite(revealed, static_cast<float>(revealIndex), revealRemaining)) {
                return false;
            }
            const float revealAlpha = std::clamp(revealRemaining, 0.0f, 1.0f);
            if (glyph != nullptr && glyph->drawable && revealAlpha > 0.0f) {
                float glyphXOffset = 0.0f;
                float glyphYOffset = 0.0f;
                float glyphWidth = 0.0f;
                float glyphHeight = 0.0f;
                if (!IsFinite(glyph->xOffset) || !IsFinite(glyph->yOffset) || !IsFinite(glyph->width) ||
                    !IsFinite(glyph->height) || !IsFinite(glyph->u0) || !IsFinite(glyph->v0) || !IsFinite(glyph->u1) ||
                    !IsFinite(glyph->v1) || !TryMultiplyFinite(glyph->xOffset, glyphScale, glyphXOffset) ||
                    !TryMultiplyFinite(glyph->yOffset, glyphScale, glyphYOffset) ||
                    !TryMultiplyFinite(glyph->width, glyphScale, glyphWidth) ||
                    !TryMultiplyFinite(glyph->height, glyphScale, glyphHeight)) {
                    return false;
                }
                if (outDraws.size() < maximumGlyphCount) {
                    SdfTextGlyphDraw draw{};
                    draw.atlasTexture = atlas.textureGeneration->GetTexture();
                    if (!TryAddFinite(cursorX, glyphXOffset, draw.x) ||
                        !TryAddFinite(baselineY, glyphYOffset, draw.y)) {
                        return false;
                    }
                    draw.width = glyphWidth;
                    draw.height = glyphHeight;
                    draw.u0 = glyph->u0;
                    draw.v0 = glyph->v0;
                    draw.u1 = glyph->u1;
                    draw.v1 = glyph->v1;
                    draw.revealAlpha = revealAlpha;
                    draw.submissionSequence = text.submissionSequence;
                    draw.requestIndex = requestIndex;
                    draw.glyphSequence = glyphSequence++;
                    outDraws.push_back(std::move(draw));
                } else {
                    ++stats.droppedGlyphCount;
                }
            }
            if (!TryAddFinite(cursorX, advance, cursorX)) {
                return false;
            }
            if (stops)
                stops->push_back({requestIndex, offsets[sourceIndex + 1], cursorX, caretY, text.fontHeight});
        }
    }
    return true;
}
} // namespace

bool BuildSdfTextGlyphDraws(std::span<const SdfTextLayoutRequest> requests, std::vector<SdfTextGlyphDraw>& outDraws,
                            SdfTextLayoutStats* outStats, uint32_t maximumGlyphCount, SdfTextLayoutLease* outLease,
                            std::vector<SdfTextCaretStop>* outCaretStops) noexcept {
    static const auto timer = CPU_PROFILE::RegisterDynamicPass("Text.SdfLayout");
    const CPU_PROFILE::ScopedDynamicCpuTimer scope(timer);
    SdfTextLayoutStats stats{};
    stats.requestCount =
        static_cast<uint32_t>((std::min)(requests.size(), static_cast<size_t>((std::numeric_limits<uint32_t>::max)())));
    try {
        outDraws.clear();
        if (outCaretStops)
            outCaretStops->clear();
        if (outLease != nullptr)
            outLease->Reset();
        std::vector<bool> valid(requests.size(), false);
        std::unordered_map<std::string, std::set<uint32_t>> requested{};
        std::unordered_map<std::string, uint64_t> revisions;
        for (size_t index = 0u; index < requests.size(); ++index) {
            const SdfTextLayoutRequest& text = requests[index];
            if (!IsValidRequest(text)) {
                ++stats.failedRequestCount;
                continue;
            }
            auto [rev, inserted] = revisions.try_emplace(text.absoluteFontAssetPath, text.fontRevision);
            if (!inserted && text.fontRevision && rev->second && rev->second != text.fontRevision) {
                ++stats.failedRequestCount;
                continue;
            }
            if (text.fontRevision)
                rev->second = text.fontRevision;
            valid[index] = true;
            std::set<uint32_t>& codepoints = requested[text.absoluteFontAssetPath];
            const std::vector<uint32_t> decoded = DecodeUtf8(text.utf8Text);
            codepoints.insert(decoded.begin(), decoded.end());
        }

        std::unordered_map<std::string, bool> readyFonts{};
        readyFonts.reserve(requested.size());
        auto leaseStorage = std::make_shared<SdfTextLayoutLeaseStorage>();
        leaseStorage->generations.reserve(requested.size());
        for (const auto& [path, codepoints] : requested) {
            FontAtlas* atlas = nullptr;
            const bool ready = EnsureGlyphs(path, codepoints, revisions.at(path), atlas, stats);
            readyFonts.emplace(path, ready);
            if (ready && atlas != nullptr && atlas->textureGeneration) {
                leaseStorage->generations.push_back(atlas->textureGeneration);
            }
        }
        outDraws.reserve((std::min)(static_cast<size_t>(maximumGlyphCount), requests.size() * 16u));
        for (size_t index = 0u; index < requests.size(); ++index) {
            if (!valid[index])
                continue;
            const SdfTextLayoutRequest& text = requests[index];
            const auto ready = readyFonts.find(text.absoluteFontAssetPath);
            const auto atlas = gFonts.find(text.absoluteFontAssetPath);
            if (ready == readyFonts.end() || !ready->second || atlas == gFonts.end()) {
                ++stats.failedRequestCount;
                continue;
            }
            const size_t drawCountBefore = outDraws.size();
            const size_t stopCountBefore = outCaretStops ? outCaretStops->size() : 0;
            const uint32_t droppedGlyphCountBefore = stats.droppedGlyphCount;
            if (!AppendTextDraws(text, static_cast<uint32_t>(index), atlas->second, maximumGlyphCount, outDraws, stats,
                                 outCaretStops)) {
                outDraws.resize(drawCountBefore);
                if (outCaretStops)
                    outCaretStops->resize(stopCountBefore);
                stats.droppedGlyphCount = droppedGlyphCountBefore;
                ++stats.failedRequestCount;
                continue;
            }
            ++stats.readyRequestCount;
        }
        stats.emittedGlyphCount = static_cast<uint32_t>(outDraws.size());
        if (outLease != nullptr && !leaseStorage->generations.empty()) {
            outLease->retainedGenerationCount_ = leaseStorage->generations.size();
            outLease->retention_ = std::move(leaseStorage);
        }
        if (outStats != nullptr)
            *outStats = stats;
        return true;
    } catch (...) {
        outDraws.clear();
        if (outCaretStops)
            outCaretStops->clear();
        if (outLease != nullptr)
            outLease->Reset();
        stats.emittedGlyphCount = 0u;
        if (outStats != nullptr)
            *outStats = stats;
        return false;
    }
}

void ShutdownSdfTextLayout() noexcept {
    // 現行 generation は cache が、旧 generation は各 layout lease が
    // 所有する。clear 後も使用中 lease の texture/UV は不変で残る。
    gFonts.clear();
}

} // namespace HIKARI::TEXT_RENDERING
