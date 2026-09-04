#pragma once

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <type_traits>

namespace HIKARI {

    inline constexpr size_t kGameProjectUiTextureGuidCapacity = 96u;
    inline constexpr size_t kGameProjectUiTexturePathCapacity = 512u;

    struct GameProjectUiTextureReference {
        char assetGuid[kGameProjectUiTextureGuidCapacity]{};
        char assetPath[kGameProjectUiTexturePathCapacity]{};
    };

    enum class GameProjectUiEffect : uint8_t {
        None = 0u,
        HorizontalVitalFill = 1u,
        StaminaFill = 2u,
        SkillCooldownEnergy = 3u,
        SkillReadyPulse = 4u,
        WeaponSwitchIn = 5u,
        WeaponSwitchOut = 6u,
        CounterAvailable = 7u,
        CounterPressed = 8u,
        RejectedPulse = 9u,
        TextSdf = 10u,
        DialoguePanelReveal = 11u,
        DialogueChoiceIdle = 12u,
        DialogueChoiceSelected = 13u,
        TextSdfGlow = 14u,
        DialoguePortraitFocus = 15u,
        BurstGauge = 16u,
        BurstGaugeActive = 17u,
        ItemChargeIdle = 18u,
        ItemChargeUse = 19u,
        HudIdleEnergy = 20u,
        // Title-screen effects are appended to preserve the serialized/UI
        // effect values used by existing game projects.
        TitleLogoReveal = 21u,
        TitleLogoIdleSheen = 22u,
        TitleMenuReveal = 23u,
        TitleMenuFocus = 24u,
        TitleMenuPress = 25u,
        TitleMenuDisabled = 26u,
        ItemChargeRecharge = 27u,
        Count = 28u,
    };

    // Submission flags are shared by sprites, text and backdrop requests.
    // Modal elements remain visible when the winning backdrop requests HUD
    // suppression. The suppression bit is meaningful on backdrops only.
    inline constexpr uint32_t kGameProjectUiSubmissionModal = 1u << 0u;
    inline constexpr uint32_t kGameProjectUiSuppressNonModal = 1u << 1u;

    enum class GameProjectUiTextAlign : uint8_t {
        Left = 0u,
        Center = 1u,
        Right = 2u,
    };

    enum class GameProjectUiVerticalAlign : uint8_t {
        Top = 0u,
        Center = 1u,
        Bottom = 2u,
    };

    enum class GameProjectUiTextWrap : uint8_t {
        None = 0u,
        Character = 1u,
    };

    struct GameProjectUiColor {
        float r = 1.0f;
        float g = 1.0f;
        float b = 1.0f;
        float a = 1.0f;
    };

    // Shared aspect-preserving canvas projection used by both rendering and
    // pointer hit testing. This keeps Scene-authored UI and code submissions
    // on one coordinate contract.
    struct GameProjectUiCanvasFit {
        float scale = 0.0f;
        float offsetX = 0.0f;
        float offsetY = 0.0f;
        float fittedWidth = 0.0f;
        float fittedHeight = 0.0f;

        bool IsValid() const noexcept {
            return std::isfinite(scale) && scale > 0.0f &&
                std::isfinite(offsetX) && std::isfinite(offsetY) &&
                std::isfinite(fittedWidth) && fittedWidth > 0.0f &&
                std::isfinite(fittedHeight) && fittedHeight > 0.0f;
        }
    };

    inline bool ResolveGameProjectUiCanvasFit(
        uint32_t logicalWidth,
        uint32_t logicalHeight,
        float referenceWidth,
        float referenceHeight,
        GameProjectUiCanvasFit& outFit) noexcept {

        outFit = {};
        if (logicalWidth == 0u || logicalHeight == 0u ||
            !std::isfinite(referenceWidth) || referenceWidth <= 0.0f ||
            !std::isfinite(referenceHeight) || referenceHeight <= 0.0f) {
            return false;
        }
        const float scaleX =
            static_cast<float>(logicalWidth) / referenceWidth;
        const float scaleY =
            static_cast<float>(logicalHeight) / referenceHeight;
        outFit.scale = scaleX < scaleY ? scaleX : scaleY;
        outFit.fittedWidth = referenceWidth * outFit.scale;
        outFit.fittedHeight = referenceHeight * outFit.scale;
        outFit.offsetX =
            (static_cast<float>(logicalWidth) - outFit.fittedWidth) * 0.5f;
        outFit.offsetY =
            (static_cast<float>(logicalHeight) - outFit.fittedHeight) * 0.5f;
        return outFit.IsValid();
    }

    inline bool ProjectGameUiScreenPointToCanvas(
        const GameProjectUiCanvasFit& fit,
        float screenX,
        float screenY,
        float& outCanvasX,
        float& outCanvasY) noexcept {

        outCanvasX = 0.0f;
        outCanvasY = 0.0f;
        if (!fit.IsValid() || !std::isfinite(screenX) ||
            !std::isfinite(screenY)) {
            return false;
        }
        outCanvasX = (screenX - fit.offsetX) / fit.scale;
        outCanvasY = (screenY - fit.offsetY) / fit.scale;
        return std::isfinite(outCanvasX) && std::isfinite(outCanvasY);
    }

    // All positions and extents are expressed in the caller's reference
    // canvas. The engine performs one aspect-preserving fit so authored HUD
    // layouts remain stable at non-16:9 output sizes.
    struct GameProjectUiSpriteRequest {
        uint64_t sourceId = 0u;
        int32_t order = 0;
        uint32_t flags = 0u;

        float referenceWidth = 1920.0f;
        float referenceHeight = 1080.0f;
        float x = 0.0f;
        float y = 0.0f;
        float width = 1.0f;
        float height = 1.0f;
        float pivotX = 0.0f;
        float pivotY = 0.0f;
        float rotationRadians = 0.0f;

        // fillAmount is the visible vital fraction for fill effects and the
        // remaining cooldown fraction for SkillCooldownEnergy.
        float fillAmount = 1.0f;
        float effectProgress = 1.0f;
        float effectIntensity = 1.0f;
        float timeSeconds = 0.0f;
        float directionX = 1.0f;
        float directionY = 0.0f;

        GameProjectUiColor tint{};
        GameProjectUiColor effectColor{
            0.0156863f,
            0.1686275f,
            0.5058824f,
            1.0f };
        GameProjectUiColor highlightColor{};
        GameProjectUiEffect effect = GameProjectUiEffect::None;
        uint8_t reserved[7]{};
    };

    // UTF-8 text is copied synchronously by UiView::SubmitText. The font path
    // is project-relative below Assets and may point to a TTF, OTF, or TTC
    // file. Text layout values use the same reference-canvas contract as
    // GameProjectUiSpriteRequest.
    struct GameProjectUiTextRequest {
        uint64_t sourceId = 0u;
        int32_t order = 0;
        uint32_t flags = 0u;

        float referenceWidth = 1920.0f;
        float referenceHeight = 1080.0f;
        float x = 0.0f;
        float y = 0.0f;
        float maxWidth = 1.0f;
        float maxHeight = 1.0f;
        float pivotX = 0.0f;
        float pivotY = 0.0f;
        float rotationRadians = 0.0f;

        float pixelHeight = 32.0f;
        float lineHeightMultiplier = 1.15f;
        float letterSpacing = 0.0f;
        float revealAmount = 1.0f;
        float effectProgress = 1.0f;
        float effectIntensity = 1.0f;
        float timeSeconds = 0.0f;
        float outlineWidth = 1.5f;
        float shadowOffsetX = 2.0f;
        float shadowOffsetY = 2.0f;

        GameProjectUiColor textColor{};
        GameProjectUiColor outlineColor{
            0.0156863f,
            0.1686275f,
            0.5058824f,
            1.0f };
        GameProjectUiColor shadowColor{
            0.0f,
            0.0f,
            0.0f,
            0.55f };
        GameProjectUiTextAlign horizontalAlign =
            GameProjectUiTextAlign::Left;
        GameProjectUiVerticalAlign verticalAlign =
            GameProjectUiVerticalAlign::Top;
        GameProjectUiTextWrap wrap = GameProjectUiTextWrap::Character;
        uint8_t reserved[5]{};
    };

    struct GameProjectUiBackdropRequest {
        uint64_t sourceId = 0u;
        int32_t priority = 0;
        uint32_t flags = 0u;
        float dimAmount = 0.35f;
        float blurRadiusPixels = 4.0f;
        float desaturation = 0.15f;
        float vignette = 0.20f;
        float progress = 1.0f;
        GameProjectUiColor tint{
            0.0156863f,
            0.0352941f,
            0.1019608f,
            1.0f };
        uint8_t reserved[8]{};
    };

    static_assert(std::is_standard_layout_v<GameProjectUiColor>);
    static_assert(std::is_trivially_copyable_v<GameProjectUiColor>);
    static_assert(std::is_standard_layout_v<GameProjectUiCanvasFit>);
    static_assert(std::is_trivially_copyable_v<GameProjectUiCanvasFit>);
    static_assert(sizeof(GameProjectUiCanvasFit) == 20u);
    static_assert(std::is_standard_layout_v<
        GameProjectUiTextureReference>);
    static_assert(std::is_trivially_copyable_v<
        GameProjectUiTextureReference>);
    static_assert(std::is_standard_layout_v<GameProjectUiSpriteRequest>);
    static_assert(std::is_trivially_copyable_v<GameProjectUiSpriteRequest>);
    static_assert(std::is_standard_layout_v<GameProjectUiTextRequest>);
    static_assert(std::is_trivially_copyable_v<GameProjectUiTextRequest>);
    static_assert(std::is_standard_layout_v<GameProjectUiBackdropRequest>);
    static_assert(std::is_trivially_copyable_v<GameProjectUiBackdropRequest>);

} // namespace HIKARI
