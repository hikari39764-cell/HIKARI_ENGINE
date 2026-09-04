#include "Render2D/HIKARI_GameUiTextLayout.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "Render3D/Resources/HIKARI_TextureResourceSystem.h"
#include "Render3D/Text/HIKARI_SdfTextLayout.h"

namespace HIKARI::GAME_UI {
    namespace {
        constexpr size_t kMaximumGlyphDraws = 32768u;

        TEXT_RENDERING::SdfTextHorizontalAlign ConvertHorizontalAlign(
            GameProjectUiTextAlign value) noexcept {
            switch (value) {
            case GameProjectUiTextAlign::Center:
                return TEXT_RENDERING::SdfTextHorizontalAlign::Center;
            case GameProjectUiTextAlign::Right:
                return TEXT_RENDERING::SdfTextHorizontalAlign::Right;
            case GameProjectUiTextAlign::Left:
            default:
                return TEXT_RENDERING::SdfTextHorizontalAlign::Left;
            }
        }

        TEXT_RENDERING::SdfTextVerticalAlign ConvertVerticalAlign(
            GameProjectUiVerticalAlign value) noexcept {
            switch (value) {
            case GameProjectUiVerticalAlign::Center:
                return TEXT_RENDERING::SdfTextVerticalAlign::Center;
            case GameProjectUiVerticalAlign::Bottom:
                return TEXT_RENDERING::SdfTextVerticalAlign::Bottom;
            case GameProjectUiVerticalAlign::Top:
            default:
                return TEXT_RENDERING::SdfTextVerticalAlign::Top;
            }
        }

        TEXT_RENDERING::SdfTextWrap ConvertWrap(
            GameProjectUiTextWrap value) noexcept {
            return value == GameProjectUiTextWrap::Character
                ? TEXT_RENDERING::SdfTextWrap::Character
                : TEXT_RENDERING::SdfTextWrap::None;
        }

        TEXT_RENDERING::SdfTextLayoutRequest MakeLayoutRequest(
            const GameUiTextDraw& source) {
            TEXT_RENDERING::SdfTextLayoutRequest request{};
            request.absoluteFontAssetPath = source.absoluteFontAssetPath;
            request.utf8Text = source.utf8Text;
            request.x = source.request.x;
            request.y = source.request.y;
            request.fontHeight = source.request.pixelHeight;
            request.maxWidth = source.request.maxWidth;
            request.maxHeight = source.request.maxHeight;
            request.lineHeightMultiplier =
                source.request.lineHeightMultiplier;
            request.letterSpacing = source.request.letterSpacing;
            request.revealAmount = source.request.revealAmount;
            request.horizontalAlign = ConvertHorizontalAlign(
                source.request.horizontalAlign);
            request.verticalAlign = ConvertVerticalAlign(
                source.request.verticalAlign);
            request.wrap = ConvertWrap(source.request.wrap);
            request.submissionSequence = source.submissionSequence;
            return request;
        }

        GameUiTextGlyphDraw MakeGlyphDraw(
            const GameUiTextDraw& text,
            const TEXT_RENDERING::SdfTextGlyphDraw& glyph,
            int textureHandle) {
            GameUiTextGlyphDraw draw{};
            draw.textureHandle = textureHandle;
            draw.u0 = glyph.u0;
            draw.v0 = glyph.v0;
            draw.u1 = glyph.u1;
            draw.v1 = glyph.v1;
            draw.submissionSequence = text.submissionSequence;
            draw.glyphSequence = glyph.glyphSequence;
            draw.request.sourceId = text.request.sourceId;
            draw.request.order = text.request.order;
            draw.request.flags = text.request.flags;
            draw.request.referenceWidth = text.request.referenceWidth;
            draw.request.referenceHeight = text.request.referenceHeight;
            draw.request.x = glyph.x;
            draw.request.y = glyph.y;
            draw.request.width = glyph.width;
            draw.request.height = glyph.height;
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
            draw.request.rotationRadians = text.request.rotationRadians;
            draw.request.fillAmount = 1.0f;
            draw.request.effectProgress = text.request.effectProgress;
            draw.request.effectIntensity = text.request.outlineWidth;
            draw.request.timeSeconds = text.request.timeSeconds;
            draw.request.directionX = text.request.shadowOffsetX;
            draw.request.directionY = text.request.shadowOffsetY;
            draw.request.tint = text.request.textColor;
            draw.request.tint.a *= glyph.revealAlpha;
            draw.request.effectColor = text.request.outlineColor;
            draw.request.effectColor.a *= glyph.revealAlpha;
            draw.request.highlightColor = text.request.shadowColor;
            draw.request.highlightColor.a *= glyph.revealAlpha;
            draw.request.effect = GameProjectUiEffect::TextSdf;
            return draw;
        }
    } // namespace

    bool BuildGameUiTextGlyphDraws(
        const std::vector<GameUiTextDraw>& texts,
        std::vector<GameUiTextGlyphDraw>& outDraws,
        TEXT_RENDERING::SdfTextLayoutLease* outAtlasLease) noexcept {
        try {
            outDraws.clear();
            if (outAtlasLease != nullptr) outAtlasLease->Reset();
            std::vector<TEXT_RENDERING::SdfTextLayoutRequest> requests{};
            requests.reserve(texts.size());
            for (const GameUiTextDraw& text : texts) {
                requests.push_back(MakeLayoutRequest(text));
            }

            std::vector<TEXT_RENDERING::SdfTextGlyphDraw> glyphs{};
            TEXT_RENDERING::SdfTextLayoutStats stats{};
            if (!TEXT_RENDERING::BuildSdfTextGlyphDraws(
                    requests,
                    glyphs,
                    &stats,
                    static_cast<uint32_t>(kMaximumGlyphDraws),
                    outAtlasLease) ||
                stats.failedRequestCount != 0u) {
                outDraws.clear();
                if (outAtlasLease != nullptr) outAtlasLease->Reset();
                return false;
            }

            outDraws.reserve((std::min)(
                kMaximumGlyphDraws,
                glyphs.size() * 2u));
            std::vector<uint32_t> glyphSequences(texts.size(), 0u);
            for (const TEXT_RENDERING::SdfTextGlyphDraw& glyph : glyphs) {
                if (outDraws.size() >= kMaximumGlyphDraws ||
                    glyph.requestIndex >= texts.size()) {
                    break;
                }
                const int textureHandle =
                    RENDER3D::GetTextureResourceBackendHandle(
                        glyph.atlasTexture);
                if (textureHandle < 0) {
                    outDraws.clear();
                    if (outAtlasLease != nullptr) outAtlasLease->Reset();
                    return false;
                }
                const GameUiTextDraw& text = texts[glyph.requestIndex];
                uint32_t& glyphSequence = glyphSequences[
                    glyph.requestIndex];
                GameUiTextGlyphDraw draw = MakeGlyphDraw(
                    text, glyph, textureHandle);
                if (text.style.glowIntensity > 0.0f &&
                    text.style.glowWidth > 0.0f &&
                    text.style.glowColor.a > 0.0f &&
                    outDraws.size() < kMaximumGlyphDraws) {
                    GameUiTextGlyphDraw glow = draw;
                    glow.glyphSequence = glyphSequence++;
                    glow.request.effectProgress = 1.0f;
                    glow.request.effectIntensity = text.style.glowWidth;
                    glow.request.timeSeconds = text.style.glowIntensity;
                    glow.request.tint = text.style.glowColor;
                    glow.request.tint.a *= glyph.revealAlpha;
                    glow.request.effectColor = {};
                    glow.request.highlightColor = {};
                    glow.request.effect = GameProjectUiEffect::TextSdfGlow;
                    outDraws.push_back(std::move(glow));
                }
                if (outDraws.size() >= kMaximumGlyphDraws) continue;
                draw.glyphSequence = glyphSequence++;
                outDraws.push_back(std::move(draw));
            }
            return true;
        } catch (...) {
            outDraws.clear();
            if (outAtlasLease != nullptr) outAtlasLease->Reset();
            return false;
        }
    }

    void ShutdownGameUiTextLayout() noexcept {
        TEXT_RENDERING::ShutdownSdfTextLayout();
    }

} // namespace HIKARI::GAME_UI
