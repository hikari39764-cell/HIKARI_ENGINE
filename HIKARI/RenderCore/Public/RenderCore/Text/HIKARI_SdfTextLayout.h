#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "RenderCore/Resources/HIKARI_RenderResourceHandle.h"

namespace HIKARI::TEXT_RENDERING {

inline constexpr float kSdfTextAtlasPixelHeight = 64.0f;
inline constexpr float kSdfTextPixelDistanceScale = 16.0f;
inline constexpr uint8_t kSdfTextOnEdgeValue = 128u;
inline constexpr size_t kSdfTextMaximumUtf8Bytes = 64u * 1024u;
inline constexpr uint32_t kSdfTextDefaultMaximumGlyphCount = 32768u;

enum class SdfTextHorizontalAlign : uint8_t {
    Left,
    Center,
    Right,
};

enum class SdfTextVerticalAlign : uint8_t {
    Top,
    Center,
    Bottom,
};

enum class SdfTextWrap : uint8_t {
    None,
    Character,
};

// Layout unit は呼び出し側が所有する。Game UI は logical pixel、
// WorldText は world unit を渡し、同じ atlas/metrics を共有する。
// 数値域は engine 固有の上限値ではなく、有限な float とする。
// 実際に生成する scale/advance/position/extent も有限であることを
// renderer が確認し、表現できない派生値を持つ request は失敗扱いに
// する。
struct SdfTextLayoutRequest {
    std::string absoluteFontAssetPath{};
    uint64_t fontRevision = 0;
    std::string utf8Text{};
    float x = 0.0f;
    float y = 0.0f;
    float fontHeight = 1.0f;
    float maxWidth = 1.0f;
    float maxHeight = 1.0f;
    float lineHeightMultiplier = 1.2f;
    float letterSpacing = 0.0f;
    float revealAmount = 1.0f;
    SdfTextHorizontalAlign horizontalAlign = SdfTextHorizontalAlign::Left;
    SdfTextVerticalAlign verticalAlign = SdfTextVerticalAlign::Top;
    SdfTextWrap wrap = SdfTextWrap::None;
    uint64_t submissionSequence = 0u;
    friend bool operator==(const SdfTextLayoutRequest&, const SdfTextLayoutRequest&) = default;
};

struct SdfTextGlyphDraw {
    RENDER3D::TextureResourceHandle atlasTexture{};
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 1.0f;
    float v1 = 1.0f;
    float revealAlpha = 1.0f;
    uint64_t submissionSequence = 0u;
    uint32_t requestIndex = 0u;
    uint32_t glyphSequence = 0u;
};

struct SdfTextLayoutStats {
    uint32_t requestCount = 0u;
    uint32_t readyRequestCount = 0u;
    uint32_t failedRequestCount = 0u;
    uint32_t emittedGlyphCount = 0u;
    uint32_t droppedGlyphCount = 0u;
    uint32_t atlasRebuildCount = 0u;
};

struct SdfTextCaretStop {
    uint32_t requestIndex = 0;
    size_t byteOffset = 0;
    float x = 0, y = 0, height = 0;
};

// 一回の layout 結果が参照する不変 atlas generation を所有する。
// Copy は同じ generation を共有し、Reset/破棄後に最後の所有者が
// texture resource を GPU-safe release へ渡す。
class SdfTextLayoutLease final {
public:
    SdfTextLayoutLease() noexcept = default;

    explicit operator bool() const noexcept { return retention_ != nullptr; }

    size_t GetRetainedGenerationCount() const noexcept { return retainedGenerationCount_; }

    void Reset() noexcept {
        retention_.reset();
        retainedGenerationCount_ = 0u;
    }

private:
    friend bool BuildSdfTextGlyphDraws(std::span<const SdfTextLayoutRequest>, std::vector<SdfTextGlyphDraw>&,
                                       SdfTextLayoutStats*, uint32_t, SdfTextLayoutLease*,
                                       std::vector<SdfTextCaretStop>*) noexcept;

    std::shared_ptr<const void> retention_{};
    size_t retainedGenerationCount_ = 0u;
};

// Render thread 専用。outLease を渡した場合、結果の typed texture
// handle と UV は lease の Reset/破棄まで同じ不変 atlas generation
// を参照する。省略時は互換経路で、次の Build/Shutdown まで有効。
bool BuildSdfTextGlyphDraws(std::span<const SdfTextLayoutRequest> requests, std::vector<SdfTextGlyphDraw>& outDraws,
                            SdfTextLayoutStats* outStats = nullptr,
                            uint32_t maximumGlyphCount = kSdfTextDefaultMaximumGlyphCount,
                            SdfTextLayoutLease* outLease = nullptr,
                            std::vector<SdfTextCaretStop>* outCaretStops = nullptr) noexcept;

// Renderer shutdown owner だけが呼ぶ。各 consumer は個別 atlas を持たない。
void ShutdownSdfTextLayout() noexcept;

} // namespace HIKARI::TEXT_RENDERING
