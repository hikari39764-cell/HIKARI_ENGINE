#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "HIKARI/Project/ValueTypes.h"

namespace HIKARI {

inline constexpr uint32_t kGameProjectSceneCaptureVersion = 1u;
inline constexpr uint32_t kGameProjectSceneCaptureDescV1Size = 48u;
inline constexpr uint32_t kGameProjectSceneCaptureMaterialBindingDescV1Size = 152u;
inline constexpr uint32_t kGameProjectSceneCaptureMaterialRegionBindingVersion = 1u;
inline constexpr uint32_t kGameProjectSceneCaptureMaterialRegionBindingDescV1Size = 176u;
inline constexpr uint32_t kGameProjectSceneCaptureSnapshotV1Size = 64u;
inline constexpr uint32_t kGameProjectSceneCaptureMaterialSlotNameCapacity = 96u;
inline constexpr uint32_t kGameProjectSceneCaptureMinimumExtent = 64u;
inline constexpr uint32_t kGameProjectSceneCaptureMaximumExtent = 2048u;
// A World may still own a small number of genuinely independent views.
// Surfaces that share a camera should prefer region bindings on one
// capture instead of consuming additional targets.
inline constexpr uint32_t kGameProjectSceneCaptureMaximumActiveCount = 4u;
inline constexpr uint32_t kGameProjectInvalidMaterialIndex = 0xffffffffu;

// 電視など、キャプチャを貼る画面だけに掛ける描き方。主画面の後処理とは別。
// None   : 従来どおり前露出の HDR をそのまま公開する（後処理なし）。
// Clean  : 3D 描画を表示参照の線形色へ直して明るさだけを掛ける（原版の確認用）。
// Pixel  : ゲーム画素（pixelSize texel 四方）ごとに一色で塗り、背景へ落ち影と外側の輪郭を足す。
// Sketch : 紙（paperColor）に鉛筆（lineColor）で外形と色の段の境を描き、暗い段に斜線、赤は accentColor の色鉛筆。
//          物体の焼いた色は fillStrength だけ紙に乗せる（明るい天板はそれより 0.23 濃く、鮮やかな物体は常に濃く塗る）。
// Pixel と Sketch は、線形輝度が backgroundLuminance 未満の画素を背景（壁）、以上を物体として扱う。
// 物体の色は作者が決めた少数の色（陰影を焼いた無光照の材質）であることを前提にし、色の量子化はしない。
// 版 4 はキャプチャの深度（カメラから視線方向の距離、world 単位）を使う効果を足す。depthFocus が 0 なら全部無効で、
// 版 3 と同じ絵になる。深度の効果は全部の描き方に掛かる：
// - backgroundDepth：前景の面（depthFocus）からこれより奥を背景として扱う（輝度の閾値より優先。輝度 0.75 以上は常に物体）。
// - 霧：背景を奥ほど fogColor へ寄せる。fogDepth で最も濃い。
// - 遮蔽：手前の物の近くにある奥の面を暗くする（物の周り・入隅）。
// - 深度の線：深度の段差（物の外形）に lineColor の線を引く。
// - 光の影：shadowOffsetX/Y の向きへ深度を辿り、光線より手前に物があれば影にする（物の高さに応じた長さの影）。
enum class GameProjectSceneCaptureStyle : uint32_t { None = 0u, Clean = 1u, Pixel = 2u, Sketch = 3u };

// 長さは「ゲーム画素」単位（1 ゲーム画素 = pixelSize texel）。色は表示 sRGB（各成分 0..1）。
struct GameProjectSceneCaptureStyleDesc {
    uint32_t structSize = 144u;         // 版 2・3 は 80（fillStrength まで）、版 4 は 140（depthLineGap まで）。足りない欄は既定として読む
    uint32_t version = 5u;              // 2 は fillStrength の欄が予約（0）で、塗りは既定の 0.32
    GameProjectSceneCaptureStyle style = GameProjectSceneCaptureStyle::None;
    float pixelSize = 2.0f;             // 1..32 texel
    float lineWidth = 1.0f;             // 0..4 ゲーム画素。Pixel の輪郭（0 で無し）、Sketch の鉛筆線
    float shadowStrength = 0.4f;        // 0..1 Pixel は背景に落とす影の濃さ、Sketch は纵深の強さ（0 で無し）
    float shadowOffsetX = 2.0f;         // -16..16 ゲーム画素。右と下が正
    float shadowOffsetY = 4.0f;         // -16..16
    float backgroundLuminance = 0.035f; // 0..1
    float brightness = 1.0f;            // 0..4
    GameProjectFloat3 lineColor{0.04f, 0.07f, 0.09f};
    GameProjectFloat3 paperColor{0.10f, 0.15f, 0.20f};
    GameProjectFloat3 accentColor{0.66f, 0.47f, 0.94f};   // この色相の鮮やかな物体を残影として横縞で描く
    float fillStrength = 0.32f;         // 0..1 Sketch で物体の焼いた色を紙に乗せる濃さ
    // 版 4：深度の効果（距離は world 単位）。
    float depthFocus = 0.0f;            // 0 か 0..100000 カメラから前景の面までの距離。0 なら深度を使わない
    float backgroundDepth = 0.0f;       // 0..100000 前景からこれより奥を背景にする（0 なら輝度で分ける）
    float fogStrength = 0.0f;           // 0..1
    float fogDepth = 1.0f;              // 0.001..100000 前景から霧が最も濃くなるまでの奥行き
    GameProjectFloat3 fogColor{0.05f, 0.07f, 0.10f};
    float occlusion = 0.0f;             // 0..1
    float occlusionRadius = 2.0f;       // 0..16 ゲーム画素
    float occlusionDepth = 1.0f;        // 0.001..100000 この深度差で暗がりが最も濃い
    float depthLine = 0.0f;             // 0..1
    float lightShadow = 0.0f;           // 0..1
    float lightSlope = 0.1f;            // 0..100000 影の光線が 1 ゲーム画素進む間に手前へ上がる距離
    float lightReach = 8.0f;            // 0..32 影を探す最大のゲーム画素
    float depthLineGap = 0.1f;          // 0.001..100000 深度の線を引く段差
    // 版 5：1 なら背景（物体でも線でもない画素）の alpha を 0 にして出す。主画面へ半透明の板で重ねる時に使う
    // （貼る材質は AlphaBlend の結び付けにする）。0 なら従来どおり全面 alpha 1。
    float transparentBackground = 0.0f; // 0..1
};

static_assert(std::is_standard_layout_v<GameProjectSceneCaptureStyleDesc>);
static_assert(std::is_trivially_copyable_v<GameProjectSceneCaptureStyleDesc>);
static_assert(sizeof(GameProjectSceneCaptureStyleDesc) == 144u);
static_assert(offsetof(GameProjectSceneCaptureStyleDesc, transparentBackground) == 140u);
static_assert(offsetof(GameProjectSceneCaptureStyleDesc, depthFocus) == 80u);
static_assert(offsetof(GameProjectSceneCaptureStyleDesc, lineColor) == 40u);

enum class GameProjectSceneCaptureMaterialBindingFlag : uint32_t {
    None = 0u,
    FlipTextureX = 1u << 0u,
    FlipTextureY = 1u << 1u,
    ProtectReadability = 1u << 2u,
    // 貼る材質を半透明（キャプチャの alpha で混ぜる）にする。transparentBackground の風格と組にして重ね板に使う。
    AlphaBlend = 1u << 3u,
    // 貼る材質をキャプチャの alpha で切り抜く（0.5 未満を捨てる不透明面）。深度と動きの情報を書くので、
    // 時間方向のアンチエイリアスで重ね板の跡が残らない。AlphaBlend より優先。
    AlphaMask = 1u << 4u,
};

constexpr uint32_t GameProjectSceneCaptureMaterialBindingFlags(
    GameProjectSceneCaptureMaterialBindingFlag flags) noexcept {
    return static_cast<uint32_t>(flags);
}

inline constexpr uint32_t kGameProjectSceneCaptureMaterialBindingKnownFlags =
    GameProjectSceneCaptureMaterialBindingFlags(GameProjectSceneCaptureMaterialBindingFlag::FlipTextureX) |
    GameProjectSceneCaptureMaterialBindingFlags(GameProjectSceneCaptureMaterialBindingFlag::FlipTextureY) |
    GameProjectSceneCaptureMaterialBindingFlags(GameProjectSceneCaptureMaterialBindingFlag::ProtectReadability) |
    GameProjectSceneCaptureMaterialBindingFlags(GameProjectSceneCaptureMaterialBindingFlag::AlphaBlend) |
    GameProjectSceneCaptureMaterialBindingFlags(GameProjectSceneCaptureMaterialBindingFlag::AlphaMask);

struct GameProjectSceneCaptureHandle {
    uint64_t value = 0u;
    uint64_t epoch = 0u;

    bool IsValid() const noexcept { return value != 0u && epoch != 0u; }

    bool operator==(const GameProjectSceneCaptureHandle&) const noexcept = default;
};

enum class GameProjectSceneCaptureFlag : uint32_t {
    None = 0u,
    Enabled = 1u << 0u,
    RenderSky = 1u << 1u,
    RenderTransparent = 1u << 2u,
    RenderVfx = 1u << 3u,
};

constexpr GameProjectSceneCaptureFlag operator|(GameProjectSceneCaptureFlag lhs,
                                                GameProjectSceneCaptureFlag rhs) noexcept {
    return static_cast<GameProjectSceneCaptureFlag>(static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
}

constexpr uint32_t GameProjectSceneCaptureFlags(GameProjectSceneCaptureFlag flags) noexcept {
    return static_cast<uint32_t>(flags);
}

enum class GameProjectSceneCaptureStatus : uint32_t {
    Disabled = 0u,
    Pending,
    Ready,
    Failed,
};

enum class GameProjectSceneCaptureFailure : uint32_t {
    None = 0u,
    InvalidRequest,
    InvalidCamera,
    RenderTargetUnavailable,
    RenderFailed,
    MaterialBindingFailed,
    WorldClosing,
};

// A World owns a small bounded set of secondary captures. Camera handles
// are resolved every frame and never extend object lifetime. The renderer
// owns every target, descriptor, and GPU synchronization resource.
struct GameProjectSceneCaptureDesc {
    uint32_t structSize = kGameProjectSceneCaptureDescV1Size;
    uint32_t version = kGameProjectSceneCaptureVersion;
    GameProjectObjectHandle cameraObject{};
    uint32_t width = 960u;
    uint32_t height = 640u;
    uint32_t flags = GameProjectSceneCaptureFlags(
        GameProjectSceneCaptureFlag::Enabled | GameProjectSceneCaptureFlag::RenderSky |
        GameProjectSceneCaptureFlag::RenderTransparent | GameProjectSceneCaptureFlag::RenderVfx);
    uint32_t reserved[3] = {0u, 0u, 0u};
};

// The binding resolves an imported material by exact source material
// name. fallbackMaterialIndex is used only when the name is empty. Flags
// correct the imported display surface's UV orientation without mutating
// model data. The capture texture remains engine-owned and is never
// exposed to the GameProject as a descriptor or native resource.
struct GameProjectSceneCaptureMaterialBindingDesc {
    uint32_t structSize = kGameProjectSceneCaptureMaterialBindingDescV1Size;
    uint32_t version = kGameProjectSceneCaptureVersion;
    GameProjectSceneCaptureHandle capture{};
    GameProjectObjectHandle modelObject{};
    char materialSlotName[kGameProjectSceneCaptureMaterialSlotNameCapacity]{};
    uint32_t fallbackMaterialIndex = kGameProjectInvalidMaterialIndex;
    uint32_t flags = GameProjectSceneCaptureMaterialBindingFlags(GameProjectSceneCaptureMaterialBindingFlag::None);
    uint32_t reserved[2] = {0u, 0u};
};

struct GameProjectSceneCaptureSourceRect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 1.0f;
    float height = 1.0f;
};

// Binds a normalized region of one capture to a display material. This
// allows several cabinet surfaces to share one rendered scene feed. The
// optional quarter-turn rotation is applied after FlipTextureX/Y.
struct GameProjectSceneCaptureMaterialRegionBindingDesc {
    uint32_t structSize = kGameProjectSceneCaptureMaterialRegionBindingDescV1Size;
    uint32_t version = kGameProjectSceneCaptureMaterialRegionBindingVersion;
    GameProjectSceneCaptureHandle capture{};
    GameProjectObjectHandle modelObject{};
    char materialSlotName[kGameProjectSceneCaptureMaterialSlotNameCapacity]{};
    uint32_t fallbackMaterialIndex = kGameProjectInvalidMaterialIndex;
    uint32_t flags = GameProjectSceneCaptureMaterialBindingFlags(GameProjectSceneCaptureMaterialBindingFlag::None);
    GameProjectSceneCaptureSourceRect sourceRect{};
    uint32_t quarterTurns = 0u;
    uint32_t reserved[3] = {0u, 0u, 0u};
};

struct GameProjectSceneCaptureSnapshot {
    uint32_t structSize = kGameProjectSceneCaptureSnapshotV1Size;
    uint32_t version = kGameProjectSceneCaptureVersion;
    GameProjectSceneCaptureHandle capture{};
    GameProjectSceneCaptureStatus status = GameProjectSceneCaptureStatus::Disabled;
    GameProjectSceneCaptureFailure failure = GameProjectSceneCaptureFailure::None;
    uint32_t width = 0u;
    uint32_t height = 0u;
    uint64_t outputRevision = 0u;
    uint64_t renderedFrameIndex = 0u;
    uint32_t reserved[2] = {0u, 0u};
};

static_assert(std::is_standard_layout_v<GameProjectSceneCaptureHandle>);
static_assert(std::is_trivially_copyable_v<GameProjectSceneCaptureHandle>);
static_assert(sizeof(GameProjectSceneCaptureHandle) == 16u);
static_assert(std::is_standard_layout_v<GameProjectSceneCaptureDesc>);
static_assert(std::is_trivially_copyable_v<GameProjectSceneCaptureDesc>);
static_assert(sizeof(GameProjectSceneCaptureDesc) == kGameProjectSceneCaptureDescV1Size);
static_assert(offsetof(GameProjectSceneCaptureDesc, cameraObject) == 8u);
static_assert(std::is_standard_layout_v<GameProjectSceneCaptureMaterialBindingDesc>);
static_assert(std::is_trivially_copyable_v<GameProjectSceneCaptureMaterialBindingDesc>);
static_assert(sizeof(GameProjectSceneCaptureMaterialBindingDesc) == kGameProjectSceneCaptureMaterialBindingDescV1Size);
static_assert(offsetof(GameProjectSceneCaptureMaterialBindingDesc, materialSlotName) == 40u);
static_assert(offsetof(GameProjectSceneCaptureMaterialBindingDesc, flags) == 140u);
static_assert(std::is_standard_layout_v<GameProjectSceneCaptureSourceRect>);
static_assert(std::is_trivially_copyable_v<GameProjectSceneCaptureSourceRect>);
static_assert(sizeof(GameProjectSceneCaptureSourceRect) == 16u);
static_assert(std::is_standard_layout_v<GameProjectSceneCaptureMaterialRegionBindingDesc>);
static_assert(std::is_trivially_copyable_v<GameProjectSceneCaptureMaterialRegionBindingDesc>);
static_assert(sizeof(GameProjectSceneCaptureMaterialRegionBindingDesc) ==
              kGameProjectSceneCaptureMaterialRegionBindingDescV1Size);
static_assert(offsetof(GameProjectSceneCaptureMaterialRegionBindingDesc, sourceRect) == 144u);
static_assert(std::is_standard_layout_v<GameProjectSceneCaptureSnapshot>);
static_assert(std::is_trivially_copyable_v<GameProjectSceneCaptureSnapshot>);
static_assert(sizeof(GameProjectSceneCaptureSnapshot) == kGameProjectSceneCaptureSnapshotV1Size);

} // namespace HIKARI
