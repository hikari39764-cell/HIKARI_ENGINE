#pragma once

#include <string>
#include <cstdint>

#include "RenderContracts/RenderStyle/HIKARI_RenderStyleShading.h"
#include "Core/Math/HIKARI_Math3D.h"
#include "Render3D/Color/HIKARI_ExposureState.h"

// 環境の設定（.henvironment の内容）と、環境ランタイムが毎フレーム解く光源状態。
// 単位: 照度 lux、輝度 nits（cd/m^2）、距離 m、角度は注記のとおり。
namespace HIKARI {

enum class SkySource : uint8_t {
    Atmosphere = 0,
    // Sky 資産（HDR 立方体）。水平面照度で絶対輝度へ校正する。
    Hdri,
    // 等距円筒の背景画像（表示参照）。任意の雲層を重ねられる。
    Panorama,
};

struct SkySettings {
    SkySource source = SkySource::Atmosphere;
    // Hdri では Sky 資産名、Panorama ではテクスチャ資産。
    std::string asset{};
    // Hdri と Panorama の画素値の較正。画素値 1 の一様な空が地面に与える水平面照度 [lux]。
    float textureIlluminance = 20000.0f;
    std::string panoramaCloudAsset{};
    MATH::Vec2 panoramaCloudScrollSpeed{0.0015f, 0.0f};
    float panoramaCloudOpacity = 1.0f;
    // 空の回転 [度]。
    float yaw = 0.0f;
    MATH::Vec3 tint{1.0f, 1.0f, 1.0f};
    // 天空光（拡散）と反射の作画用倍率。
    float lightIntensity = 1.0f;
    float reflectionIntensity = 1.0f;
};

// 物理大気の空で、地平線より下に見える背景。
enum class BelowHorizonMode : uint8_t {
    // 地平線の霞を下へ延ばす。地面は場面の物体が受け持ち、背景には地面も雲の影も描かない。
    Haze = 0,
    // 惑星の地表（地面反照率の色を空気遠近で霞ませる）。高い視点から遠くの地表を見せる時に使う。
    Ground,
};

struct AtmosphereSettings {
    float planetRadius = 6360000.0f;
    float atmosphereHeight = 80000.0f;
    float rayleighScaleHeight = 8000.0f;
    // エアロゾルは地表近くの境界層に集まる。
    float mieScaleHeight = 800.0f;
    MATH::Vec3 rayleighScattering{5.8e-6f, 13.5e-6f, 33.1e-6f};
    // 地表のエアロゾルの散乱係数 [1/m]。普通の晴天（地表の視程約 58km、光学的厚さ約 0.04）。地平線の白み、遠くの
    // 雲や地面が霞に溶ける具合、太陽まわりの光環を決める。境界層に集めて柱の厚さを抑え、天頂の青は保つ
    // （多重散乱は等方近似なので、柱が厚いと空全体が灰色の幕をかぶったようになる）。
    MATH::Vec3 mieScattering{5.0e-5f, 5.0e-5f, 5.0e-5f};
    MATH::Vec3 ozoneAbsorption{6.5e-7f, 1.881e-6f, 8.5e-8f};
    float ozoneCenterHeight = 25000.0f;
    float ozoneWidth = 15000.0f;
    float mieAnisotropy = 0.8f;
    // 地面反照率は背景の種類に関わらず、環境光の下からの照り返しに使う。
    MATH::Vec3 groundAlbedo{0.1f, 0.1f, 0.1f};
    BelowHorizonMode belowHorizon = BelowHorizonMode::Haze;
    float aerialPerspectiveStrength = 1.0f;
    uint32_t transmittanceLutWidth = 256u;
    uint32_t transmittanceLutHeight = 64u;
    uint32_t multiScatteringLutSize = 32u;
    uint32_t skyViewLutWidth = 192u;
    uint32_t skyViewLutHeight = 108u;
};

enum class SunMode : uint8_t {
    // 日付・時刻・地点から天体位置を求める。
    Celestial = 0,
    // 方位と高度を固定する。
    Fixed,
};

struct SunSettings {
    SunMode mode = SunMode::Celestial;
    // Fixed の方位（北 0、東 90）と高度 [度]。
    float fixedAzimuthDegrees = 135.0f;
    float fixedElevationDegrees = 45.0f;
    // 大気外の法線照度 [lux]。地表の値は大気の透過率を掛けて求める。
    float illuminance = 128000.0f;
    MATH::Vec3 color{1.0f, 1.0f, 1.0f};
    // 視半径 [rad]。
    float angularRadius = 0.004675f;
    bool showDisk = true;
};

struct MoonSettings {
    bool enabled = true;
    // 満月の大気外照度 [lux]。位相で減光する。
    float illuminance = 0.27f;
    MATH::Vec3 color{0.88f, 0.92f, 1.0f};
    float angularRadius = 0.0045f;
};

// 夜空。星は実際の星等級分布に従う点光源で、明るさは物理値に intensity を掛ける。
struct StarSettings {
    bool enabled = true;
    float intensity = 1.0f;
    // 描く最も暗い星の等級。大きいほど暗い星まで増える（6.5 で全天およそ 9000 個）。
    float limitingMagnitude = 6.5f;
    // 大気光と分解できない星の光による天頂の背景輝度 [nits]。暗い空でおよそ 22 等/平方秒角。
    float backgroundLuminance = 0.00017f;
};

// 太陽方向光の影。技術的な解像度・カスケード・フィルターは画質設定が持つ。
struct ShadowSettings {
    bool enabled = true;
    float distance = 30.0f;
    // 様式化した陰影の演出用。Standard PBR は実際の可視性を直接使う。
    float strength = 0.75f;
    // 接触影：画面空間で主光の方へ短く深度を辿り、カスケードの分解能では消える足元や小物の接地の影を補う。
    // length は辿る長さ [m]、strength は濃さ、thickness は遮る物の厚みの想定 [m]（これより奥の面は遮らない）。
    // 有効にすると不透明面の Forward の前に場面の深度が要る（GeometryAux が無ければ深度 prepass を足す）ので既定は無効。
    bool contactShadows = false;
    float contactShadowLength = 0.25f;
    float contactShadowStrength = 1.0f;
    float contactShadowThickness = 0.25f;
};

struct SunGlareSettings {
    bool enabled = false;
    float intensity = 0.15f;
    float haloRadius = 0.08f;
    float haloFalloff = 3.0f;
    float streakIntensity = 0.0f;
    float streakLength = 0.15f;
    float occlusionRadius = 0.012f;
    float edgeFade = 0.15f;
    float maxRadiance = 4.0f;
    float shaftIntensity = 0.0f;
    float shaftLength = 0.55f;
    float shaftDecay = 0.94f;
    // レンズ内面の反射の像（太陽と画面中心を結ぶ線上の淡い円盤）。光芒の強さに対する割合。
    float ghostIntensity = 0.0f;
};

// 体積雲の層。被覆率・雲の種類・降水・風は天気が毎フレーム与える（EnvironmentLightingFrame::clouds）。
struct CloudLayerSettings {
    bool enabled = true;
    // 積雲の雲底の高度 [m]。層雲・積乱雲の雲底と各雲の厚さもこれに比例する。
    float baseAltitude = 1500.0f;
    // 雲の消散係数の倍率（基準 0.1 /m、実際の積雲の芯と同じ）。
    float densityScale = 1.0f;
    // 雲の塊の大きさの倍率（基準は水平 9.6 km・鉛直 4.8 km の周期）。
    float shapeScale = 1.0f;
    // 細部ノイズで縁を削る強さ。
    float detailErosion = 0.12f;
    // 空と地面から雲へ入る環境光の倍率。
    float ambientScale = 1.0f;
    // 天気の被覆率への加算（-0.5..0.5）。晴天でも雲を増やしたい時に。
    float coverageBias = 0.0f;
    // 雲の群れ（雲街）の強さ。被覆率が低い時に雲を風に沿った群れに集め、間を晴らす（0 で一様に散る）。
    float clusterStrength = 1.0f;
    // 置かれた形状（網目・地形・水面）に落とす雲の影の濃さ（1 で物理どおり。小さいほど影の中にも日照が残り、見た目向きに
    // 淡くなる）。空そのものには雲の影は落ちない。
    float shadowStrength = 0.65f;
    // 雲に当たる主光源の倍率と、前方散乱（銀の縁）の倍率。見た目の調整用。
    float sunlightScale = 1.0f;
    float silverLining = 1.0f;
};

enum class SsaoMode {
    Off = 0,
    Reference,
    OptimizedHigh,
    Balanced,
};

// 近場の GI（画面空間、可視性ビットマスク）。半径の中の遮蔽と照り返しを探針の間接光に重ねる（以前の SSAO の代わり）。
// thickness は画面に見えている面から奥へ続くとみなす物の厚さ（光はその裏へ回り込む）。
struct AmbientOcclusionSettings {
    bool enabled = false;
    SsaoMode mode = SsaoMode::OptimizedHigh;
    float radius = 1.0f;
    float thickness = 0.5f;
    float bias = 0.025f;
    float strength = 1.0f;
    float power = 1.0f;
    float diffuseStrength = 1.0f;
    float specularStrength = 0.5f;
    uint32_t sampleCount = 16;
    uint32_t blurIterations = 1;
};

enum class ScreenSpaceReflectionQuality {
    Off = 0,
    Low,
    Medium,
    High,
};

inline uint32_t ResolveScreenSpaceReflectionMaxSteps(ScreenSpaceReflectionQuality quality) {
    switch (quality) {
    case ScreenSpaceReflectionQuality::Low: return 24u;
    case ScreenSpaceReflectionQuality::High: return 64u;
    case ScreenSpaceReflectionQuality::Medium: return 40u;
    case ScreenSpaceReflectionQuality::Off:
    default: return 0u;
    }
}

struct ScreenSpaceReflectionSettings {
    bool enabled = true;
    ScreenSpaceReflectionQuality quality = ScreenSpaceReflectionQuality::Medium;
    float intensity = 0.75f;
    float maxDistance = 24.0f;
    float thickness = 0.0025f;
    float maxRoughness = 0.72f;
    float edgeFade = 0.08f;
    float stride = 0.18f;
};

// 画面空間の次表面散乱。材質の Subsurface（強さ）と Subsurface Radius（[m]）を持つ面の拡散光を、深度に沿って
// 水平→垂直にぼかして差分を足す。channelFalloff はチャネルごとの半径の比（皮膚では赤が最も遠くへ散る）。
struct SubsurfaceScatteringSettings {
    bool enabled = true;
    float strength = 1.0f;
    MATH::Vec3 channelFalloff{1.0f, 0.4f, 0.25f};
    float maxPixelRadius = 48.0f;
};

// 半解像度から始まる縮小ピラミッドで作る光のにじみ。全段の和を intensity 倍して足す。
struct BloomSettings {
    bool enabled = true;
    // 表示の明るさの閾値。既定の 0 は画面全体がわずかににじむ写実の設定（レンズと目の散乱に近い）。
    float threshold = 0.0f;
    float intensity = 0.05f;
    // 段を戻すときの広がりの倍率。
    float radius = 1.0f;
    // ピラミッドの段数。多いほど光の輪が遠くまで届く。
    uint32_t downsampleCount = 6;
};

struct FogSettings {
    bool enabled = false;
    bool volumetric = true;
    // 散乱アルベド。散乱光の色は空と太陽から求める。
    MATH::Vec3 albedo{0.9f, 0.9f, 0.9f};
    // 基準高さでの消散係数 [1/m]。天気の霧はこれに加わる。
    float density = 0.002f;
    float startDistance = 0.0f;
    float heightFalloff = 0.05f;
    float baseHeight = 0.0f;
    float anisotropy = 0.2f;
    float temporalWeight = 0.9f;
    float scatteringIntensity = 1.0f;
    float ambientScatteringStrength = 1.0f;
    float sunShaftStrength = 1.0f;
    // 密度の揺らぎの強さ（0 で一様、1 で 0..2 倍）と揺らぎの大きさ [m]。揺らぎは地上の風で流れる。
    float noise = 0.0f;
    float noiseScale = 16.0f;
};

// 表示の階調曲線。露出と表示変換は SDR 出力時に常に適用し、曲線だけを選ぶ。
// AgX は高い輝度を色相を保ったまま白へ寄せる（写実の既定）。AcesFitted は ACES の近似で彩度が強い。
enum class ToneCurve : uint8_t {
    None = 0u,
    Reinhard = 1u,
    AcesFitted = 2u,
    AgX = 3u,
};

inline constexpr TEXT::EnumName<ToneCurve> kToneCurveNames[] = {
    {ToneCurve::None, "None"},
    {ToneCurve::Reinhard, "Reinhard"},
    {ToneCurve::AcesFitted, "AcesFitted"},
    {ToneCurve::AgX, "AgX"},
};

// contrast は露出後・階調曲線の前に、輝度を中間灰 0.18 を中心に対数で伸ばす倍率。既定 1.15 は写真の階調に近い
// 張り（AgX の肩だけでは日の当たる雲が灰色に寄る）。
struct ToneMappingSettings {
    ToneCurve curve = ToneCurve::AgX;
    float whiteBalanceTemperature = 6500.0f;
    float whiteBalanceTint = 0.0f;
    float contrast = 1.15f;
    float saturation = 1.0f;
    float gamma = 2.2f;
};

// 表示の色の仕上げ。階調曲線と表示の符号化の後の色に、暗部・中間・明部の倍率（符号化した輝度で重みを分ける）と
// .cube の 3D LUT（資産の GUID、空なら無し）を掛け、lutIntensity で LUT の前の色と混ぜる。
struct ColorGradingSettings {
    MATH::Vec3 shadowsTint{1.0f, 1.0f, 1.0f};
    MATH::Vec3 midtonesTint{1.0f, 1.0f, 1.0f};
    MATH::Vec3 highlightsTint{1.0f, 1.0f, 1.0f};
    std::string lut{};
    float lutIntensity = 1.0f;

    bool operator==(const ColorGradingSettings&) const = default;
};

// 全画面後処理は後処理プロファイル資産を参照する（エンジン内蔵 ID かプロジェクト資産の GUID）。
// 空のテクスチャの画素値を輝度 [nits] へ写す倍率。一様な空の輝度 L は水平面に πL の照度を与える。
inline float ResolveSkyTextureRadianceScale(const SkySettings& sky) noexcept {
    return sky.source == SkySource::Atmosphere ? 1.0f : sky.textureIlluminance / 3.14159265358979f;
}

// .henvironment の内容。シーンは参照と初期時刻・地点・天気だけを持つ。
struct EnvironmentProfile {
    SkySettings sky{};
    AtmosphereSettings atmosphere{};
    SunSettings sun{};
    MoonSettings moon{};
    StarSettings stars{};
    CloudLayerSettings clouds{};
    FogSettings fog{};
    RENDER3D::COLOR::ExposureSettings exposure{};
    ToneMappingSettings toneMapping{};
    ColorGradingSettings colorGrading{};
    BloomSettings bloom{};
    SunGlareSettings sunGlare{};
    AmbientOcclusionSettings ambientOcclusion{};
    ScreenSpaceReflectionSettings screenSpaceReflections{};
    SubsurfaceScatteringSettings subsurfaceScattering{};
    ShadowSettings shadows{};
    // 天気の指定が無い時の天気（内蔵 ID か .hweather の GUID）。
    std::string defaultWeather = "engine.weather.clear";
};

// 天体の方向光。toLight は光源へ向かう単位ベクトル。
struct EnvironmentDirectionalLight {
    MATH::Vec3 toLight{0.0f, 1.0f, 0.0f};
    // 大気外の法線照度 [lux]。地平線の下でも保つ（大気の空は地球の影で地表の直射を 0 にし、空の高い所や高い雲には
    // 日没後も光が届いて薄明が残る）。
    float illuminance = 0.0f;
    MATH::Vec3 color{1.0f, 1.0f, 1.0f};
    float angularRadius = 0.004675f;
    // 地表から見た円盤の地平線より上の割合（0..1）。大気を持たない空や影、地表の光の有無はこれを掛けて判断する。
    float horizonVisibility = 1.0f;
};

// 雲の天気と風。被覆率、雲型（0 層雲・0.5 積雲・1 積乱雲）、濃さの倍率、降水と、
// 風の速度 [m/s] と積分した移動量 [m]（どちらもワールド xz）。
struct CloudWeatherFrame {
    float coverage = 0.0f;
    float type = 0.5f;
    float density = 1.0f;
    float precipitation = 0.0f;
    MATH::Vec2 windVelocity{};
    double windDisplacementX = 0.0;
    double windDisplacementZ = 0.0;
    // 雲の発達を進める実時間 [s]。
    double seconds = 0.0;
};

// 環境ランタイムが毎フレーム解く値。資産には保存しない。
struct EnvironmentLightingFrame {
    EnvironmentDirectionalLight sun{};
    EnvironmentDirectionalLight moon{};
    // 影を落とす主光源が月か（太陽が十分沈んだ夜）。
    bool moonIsPrimary = false;
    // 天球座標からワールドへの回転（星空用）。
    MATH::Vec3 celestialToWorldX{1.0f, 0.0f, 0.0f};
    MATH::Vec3 celestialToWorldY{0.0f, 1.0f, 0.0f};
    MATH::Vec3 celestialToWorldZ{0.0f, 0.0f, 1.0f};
    // 地表の濡れ・水たまり・積雪（0..1）。
    float surfaceWetness = 0.0f;
    float surfacePuddles = 0.0f;
    float surfaceSnowCover = 0.0f;
    // 落雷の閃光（0..1、発生時に 1 で短時間で減衰）、最後の落雷の経過時間 [s]、視点から見た落雷の水平の向き
    // （ワールド xz の単位ベクトル）と距離 [m]。
    float lightningFlash = 0.0f;
    float lightningAge = 0.0f;
    MATH::Vec2 lightningDirection{0.0f, 1.0f};
    float lightningDistance = 0.0f;
    float localHours = 12.0f;
    CloudWeatherFrame clouds{};

    const EnvironmentDirectionalLight& Primary() const noexcept { return moonIsPrimary ? moon : sun; }
};

// 描画への入力。天気を重ねた設定値と、この時刻の光源状態、シーンの描画の様式が決める標準の材質の光の受け方。
struct SceneEnvironment : EnvironmentProfile {
    EnvironmentLightingFrame frame{};
    RENDERSTYLE::RenderStyleShading styleShading{};
};

} // namespace HIKARI
