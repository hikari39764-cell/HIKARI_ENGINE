#pragma once

#include <cstdint>
#include <vector>

#include "Core/Math/HIKARI_Math3D.h"
#include "Core/Serialization/Json/HIKARI_JsonFields.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Render3D/Vfx/HIKARI_ParticleEmitterInstanceKey.h"
#include "Render3D/Vfx/HIKARI_ParticleForceField.h"

// 体積流体（VFX の流体層）の設定と、再生から描画への毎フレームの提交。
// 設定は字段表がただ一つの宣言で、VFX 源 JSON・烘焙（HVFXC）・正規化・検査器・Describe が共有する。
namespace HIKARI::RENDER3D::FLUID {

inline constexpr uint32_t kFluidResolutionMin = 16u;
inline constexpr uint32_t kFluidResolutionMax = 256u;
// 一つの域に入る源（流体源層と、域自身の力場）の上限。GPU の源の表の大きさでもある。
inline constexpr uint32_t kMaximumFluidSourcesPerDomain = 16u;
// 一つの VFX 資産が持てる域の数と、1 フレームに提交できる域の数。
inline constexpr uint32_t kMaximumFluidDomainsPerAsset = 4u;
inline constexpr uint32_t kMaximumFluidDomainSubmissionsPerFrame = 64u;
// 解算と占有の砖块（第 7 步の疎な割り当ての単位）。格子の各辺はこの倍数に丸める。
inline constexpr uint32_t kFluidBrickCells = 8u;

// 域の境界。Open は外へ流れ出て外の圧力は 0、Closed は壁（法線方向の速度 0、圧力は勾配 0）。
enum class FluidBoundary : uint8_t { Open, Closed };
inline constexpr TEXT::EnumName<FluidBoundary> kFluidBoundaryNames[] = {
    {FluidBoundary::Open, "Open"},
    {FluidBoundary::Closed, "Closed"},
};

// 源の力場の効き方。Acceleration は加速度として足し、TargetVelocity は力場が示す速度へ寄せる（目標速度場）。
enum class FluidForceMode : uint8_t { Acceleration, TargetVelocity };
inline constexpr TEXT::EnumName<FluidForceMode> kFluidForceModeNames[] = {
    {FluidForceMode::Acceleration, "Acceleration"},
    {FluidForceMode::TargetVelocity, "TargetVelocity"},
};

// 流体域の作者値。空間の値は層の局所空間 [m]、時間は秒。
struct FluidDomainSettings {
    MATH::Vec3 domainSize{4.0f, 4.0f, 4.0f};
    // 最長辺の格の数。他の辺は同じ格の大きさで決まり、どれも砖块の倍数に丸める。
    uint32_t resolution = 64u;
    // 局所空間で解く時、挂点の加速度を逆向きに入れる割合（1 で物理どおり、0 で入れない）。
    float inertia = 1.0f;
    float substepRate = 60.0f;
    uint32_t maxSubsteps = 3u;
    float prewarmSeconds = 0.0f;
    FluidBoundary boundary = FluidBoundary::Open;
    float vorticity = 0.3f;
    // 温度 1 あたりの上向き加速度と、密度 1 あたりの下向き加速度 [m/s^2]。
    float buoyancy = 1.5f;
    float weight = 0.0f;
    float densityDissipation = 0.1f;
    float temperatureDissipation = 0.5f;
    // 外観。消光は密度 1 の時 [1/m]。
    MATH::Vec3 scatteringColor{0.92f, 0.92f, 0.94f};
    float extinction = 4.0f;
    float anisotropy = 0.2f;
    float multipleScattering = 0.8f;
    float ambientStrength = 1.0f;
    // 温度の色帯：emissionTemperature で emissionColor から光り始め（強さは 1 度上で全強度）、色は emissionHotTemperature
    // へ向かって emissionHotColor へ変わる（火の赤 → 橙 → 白黄の芯）。emissionHotTemperature が起亮温度以下なら一色。
    MATH::Vec3 emissionColor{1.0f, 0.45f, 0.12f};
    float emissionIntensity = 0.0f;
    float emissionTemperature = 0.6f;
    MATH::Vec3 emissionHotColor{1.0f, 0.85f, 0.55f};
    float emissionHotTemperature = 0.0f;
    // 格ごとの色：流体源の層の色で注入した烟の色を流れと一緒に運び、散乱色と発光に掛ける（同じ域で色の違う烟が混ざらずに
    // 並ぶ）。密度の体積が 1 格 2 → 8 バイトになる。
    bool cellColor = false;
    // 燃焼（NVIDIA Flow の燃焼模型に合わせる）：燃料は温度が ignitionTemperature を超えると毎秒
    // burnRate × min(温度 - 点火温度, 1) の割合で燃え（燃料が尽きるまで）、燃えた量 1 あたり燃料を fuelPerBurn 使い、
    // 温度を heatPerBurn 上げ、煤（烟の密度）を smokePerBurn 出し、毎秒 expansionPerBurn × 燃える速さだけ体積を
    // 膨らませる（開いた境界の域だけ）。熱い煤は温度の色帯で光り（炎）、冷めると暗い烟になる。燃料の体積が 1 格 4 バイト増える。
    bool combustion = false;
    float ignitionTemperature = 0.5f;
    float burnRate = 4.0f;
    float fuelPerBurn = 1.0f;
    float heatPerBurn = 3.0f;
    float smokePerBurn = 0.6f;
    float expansionPerBurn = 0.5f;
    float detailStrength = 0.5f;
    float detailScale = 0.25f;
    bool castShadows = true;

    bool operator==(const FluidDomainSettings&) const = default;
};

// 流体源の作者値。形は層の shape、注入量は spawn.ratePerSecond（密度/秒、burst は一度に注入する密度）、
// 力場・風・減衰は forces を使い、ここは流体だけが持つ値。速度は源の局所空間 [m/s]。
struct FluidSourceSettings {
    float temperature = 1.0f;
    MATH::Vec3 velocity{0.0f, 0.0f, 0.0f};
    float radialSpeed = 0.0f;
    // 汇：形の中の密度を毎秒この割合で消す。
    float absorption = 0.0f;
    // 体積の湧き出し：形の表面から外へこの速さ [m/s] で流れ出る量を形の中で一様に湧かせる（負で吸い込む。漏斗の底の
    // 汇、吹き出し口）。開いた境界の域だけ（閉じた箱では非圧縮の流体に逃げ場が無い）。Domain の形では効かない。
    float outflow = 0.0f;
    // 形状引导：形の外へ guideThickness [m] 以内まで出た烟を形の中へ引き戻す強さ [1/s]。烟（密度）だけを運び、流れは変えない。
    float guideStrength = 0.0f;
    float guideThickness = 0.25f;
    // 注入の揺らぎ：注入する量を emissionNoiseScale [m] ほどの塊の 3D ノイズで場所ごとに揺らす強さ（0 で一様、1 で塊の間が
    // 所々抜け、濃い塊は 2〜3 倍）。塊はゆっくり形を変える。大きな源（球壳・霧の帯）を一様な膜でなく濃淡のある烟にする。
    float emissionNoise = 0.0f;
    float emissionNoiseScale = 8.0f;
    FluidForceMode forceMode = FluidForceMode::Acceleration;
    // 注入量のうち燃料になる割合（残りは烟）。燃焼の域だけ：1 で燃料だけを入れ、煤は燃えて生まれる（火の元）。
    float fuelFraction = 0.0f;

    bool operator==(const FluidSourceSettings&) const = default;
};

inline constexpr auto kFluidDomainSettingsFields = [](auto&& field) {
    using S = FluidDomainSettings;
    using namespace JSONFIELDS;
    field("domainSize", &S::domainSize, Range{0.25, 2000.0}, Unit{"m"}, Group{"Domain"}, Common{});
    field("resolution", &S::resolution, Range{kFluidResolutionMin, kFluidResolutionMax}, Group{"Domain"}, Common{});
    field("boundary", &S::boundary, kFluidBoundaryNames, Group{"Domain"});
    field("inertia", &S::inertia, Slider(0.0, 2.0), Group{"Domain"});
    field("prewarmSeconds", &S::prewarmSeconds, Range{0.0, 30.0}, Unit{"s"}, Group{"Domain"});
    field("vorticity", &S::vorticity, Range{0.0, 8.0}, Group{"Motion"}, Common{});
    field("buoyancy", &S::buoyancy, Range{-50.0, 50.0}, Unit{"m/s2"}, Group{"Motion"}, Common{});
    field("weight", &S::weight, Range{-50.0, 50.0}, Unit{"m/s2"}, Group{"Motion"});
    field("densityDissipation", &S::densityDissipation, Range{0.0, 20.0}, Unit{"1/s"}, Group{"Motion"}, Common{});
    field("temperatureDissipation", &S::temperatureDissipation, Range{0.0, 20.0}, Unit{"1/s"}, Group{"Motion"});
    field("scatteringColor", &S::scatteringColor, Color{}, Range{0.0, 1.0}, Group{"Look"}, Common{});
    field("extinction", &S::extinction, Range{0.0, 200.0}, Unit{"1/m"}, Group{"Look"}, Common{});
    field("anisotropy", &S::anisotropy, Slider(-0.8, 0.8), Group{"Look"});
    field("multipleScattering", &S::multipleScattering, Slider(0.0, 1.0), Group{"Look"});
    field("ambientStrength", &S::ambientStrength, Range{0.0, 8.0}, Group{"Look"});
    field("emissionColor", &S::emissionColor, Color{}, Range{0.0, 1.0}, Group{"Look"});
    // 発光は太陽に照らされた烟と同じ尺度（前露光を掛ける）。昼の自動露出では前露光が約 1e-4 なので、昼に見える火は
    // 1e4〜1e5（実際の炎の輝度の桁）が要る。夜は同じ値で白く飛ぶのが自然で、露出が受け持つ。
    field("emissionIntensity", &S::emissionIntensity, Range{0.0, 1000000.0}, Group{"Look"});
    field("emissionTemperature", &S::emissionTemperature, Range{0.0, 10.0}, Group{"Look"});
    field("emissionHotColor", &S::emissionHotColor, Color{}, Range{0.0, 1.0}, Group{"Look"});
    field("emissionHotTemperature", &S::emissionHotTemperature, Range{0.0, 10.0}, Group{"Look"});
    field("cellColor", &S::cellColor, Group{"Look"});
    field("combustion", &S::combustion, Group{"Combustion"});
    field("ignitionTemperature", &S::ignitionTemperature, Range{0.0, 10.0}, Group{"Combustion"});
    field("burnRate", &S::burnRate, Range{0.0, 100.0}, Unit{"1/s"}, Group{"Combustion"});
    field("fuelPerBurn", &S::fuelPerBurn, Range{0.01, 10.0}, Group{"Combustion"});
    field("heatPerBurn", &S::heatPerBurn, Range{0.0, 50.0}, Group{"Combustion"});
    field("smokePerBurn", &S::smokePerBurn, Range{0.0, 10.0}, Group{"Combustion"});
    field("expansionPerBurn", &S::expansionPerBurn, Range{0.0, 10.0}, Group{"Combustion"});
    field("detailStrength", &S::detailStrength, Slider(0.0, 1.0), Group{"Look"});
    field("detailScale", &S::detailScale, Range{0.01, 100.0}, Unit{"m"}, Group{"Look"});
    field("castShadows", &S::castShadows, Group{"Look"});
    field("substepRate", &S::substepRate, Range{15.0, 240.0}, Unit{"Hz"}, Advanced{});
    field("maxSubsteps", &S::maxSubsteps, Range{1, 8}, Advanced{});
};

inline constexpr auto kFluidSourceSettingsFields = [](auto&& field) {
    using S = FluidSourceSettings;
    using namespace JSONFIELDS;
    field("temperature", &S::temperature, Range{-10.0, 10.0}, Common{});
    field("velocity", &S::velocity, Range{-500.0, 500.0}, Unit{"m/s"}, Common{});
    field("radialSpeed", &S::radialSpeed, Range{-500.0, 500.0}, Unit{"m/s"});
    field("absorption", &S::absorption, Range{0.0, 100.0}, Unit{"1/s"});
    field("outflow", &S::outflow, Range{-100.0, 100.0}, Unit{"m/s"});
    field("guideStrength", &S::guideStrength, Range{0.0, 100.0}, Unit{"1/s"}, Group{"Shape Guide"});
    field("guideThickness", &S::guideThickness, Range{0.01, 500.0}, Unit{"m"}, Group{"Shape Guide"});
    field("emissionNoise", &S::emissionNoise, Slider(0.0, 1.0), Group{"Emission Noise"});
    field("emissionNoiseScale", &S::emissionNoiseScale, Range{0.05, 500.0}, Unit{"m"}, Group{"Emission Noise"});
    field("forceMode", &S::forceMode, kFluidForceModeNames);
    field("fuelFraction", &S::fuelFraction, Slider(0.0, 1.0), Group{"Combustion"});
};

// 流体を押すもの（場面の障碍物とゲームの押し）。障碍物は形の中の速度を自分の速度へ寄せ（Brinkman の罰則、投影の前）、
// 烟は止まった障碍物を回り込み、動く障碍物に押し出され引きずられる（回転の速度は持たない）。冲量・力は extents.x を
// 半径とする球の範囲で中心ほど強い。向きがあれば加速（冲量は一子步で速さを足す）、向きが 0 なら体積の湧き出しで、
// 半径の球面を外へ strength [m/s] で流れ出す（負で吸い込む。冲量は kFluidRadialImpulseSeconds の間、開いた境界の域だけ）。
// Emit は提交している間、球の中へ烟を注入する（中心ほど濃く、strength は中心での毎秒の密度、direction は注入する
// 烟の初速 [m/s]、extents.y は温度。源の数の上限とは別枠で、位置は毎フレーム自由）。1 フレームの提交はそのフレームの
// 時間ぶん（strength × 時間）で、次に走る子步たちが入れる（子步の無いフレームの分も落とさない）。動いている Emit は
// 前のフレームの位置からの線分を掃いて入れる（velocity。流体源の扫掠注入と同じく総量は変わらない）。
enum class FluidInteractionKind : uint32_t { Obstacle, Impulse, Force, Emit };
enum class FluidInteractionShape : uint32_t { Sphere, Capsule, Box };

struct FluidInteraction {
    FluidInteractionKind kind = FluidInteractionKind::Obstacle;
    FluidInteractionShape shape = FluidInteractionShape::Sphere;
    MATH::Vec3 worldPosition{};
    MATH::Quat worldRotation = MATH::Quat::Identity();
    // Sphere：x 半径。Capsule：x 半径、y 局所 Y 軸の線分の半分。Box：半分の大きさ [m]。冲量・力は x 半径。
    MATH::Vec3 extents{0.5f, 0.5f, 0.5f};
    // 障碍物と注入の動く速さ（世界 [m/s]）と、そのフレームに動いた量（世界 [m]）。FluidInteractionService が前のフレームの
    // 位置から決める（瞬間移動は 0）。障碍物は速さで烟を押し、注入は動いた量の線分を掃く（域の時間倍率に左右されない）。
    MATH::Vec3 velocity{};
    MATH::Vec3 displacement{};
    // 冲量・力の向き（世界、0 なら中心から外向き）。
    MATH::Vec3 direction{};
    // 向きのある冲量は足す速さ [m/s]、力は加速度 [m/s²]。向きの無い冲量・力は半径での外向きの流れの速さ [m/s]。
    float strength = 0.0f;
    // 0 でなければ、向きの無い負の力（汇）が吸い込んだ烟の量を回読してこの値で返す（FluidSinkReport）。出した側が決める。
    uint64_t reportKey = 0u;
    // 出した物と実例の鍵（FluidInteractionService が決める。0 は鍵なし）。子步の無いフレームを跨いで持ち越す注入は同じ鍵の
    // 行へまとめ、1 つの注入が表の行を何行も取らない。
    uint64_t key = 0u;
};

// 汇が吸い込んだ烟の量（1 フレームの子步ぶん、域を合わせて）。単位は密度 × m³。GPU の回読なので 1〜3 フレーム遅れる。
struct FluidSinkReport {
    uint64_t reportKey = 0u;
    double removed = 0.0;
};

// 向きの無い冲量が湧き出し続けるゲーム時間 [s]（半径での押し出しの距離は約 strength × この時間）。
inline constexpr double kFluidRadialImpulseSeconds = 0.1;

// 1 フレームの移動がこの速さ [m/s] を超えたら瞬間移動とみなし、そのフレームは烟を押さない（障碍物の速度は 0、局所空間の
// 域は慣性を入れない）。
inline constexpr float kFluidTeleportSpeed = 60.0f;

inline constexpr uint32_t kMaximumFluidInteractionsPerFrame = 64u;
inline constexpr uint32_t kMaximumFluidInteractionsPerDomain = 32u;

// 源の形（VFX の emission shape と同じ種類）。値は ParticleShape と同じ並び。
enum class FluidSourceShape : uint32_t { Point, Sphere, Cone, Box, Cylinder, Circle, Edge, Domain };

// 1 フレームぶんの源。座標は源の局所空間（形と力場）で、GPU が域の格子から変換する。
// 域は源ごとに最後の子步の位置を覚え、次の子步たちをそこから今の位置までの線分に割り振り、各子步はその区間を掃いた
// 形へ注入する（扫掠注入：速く動く源も点の列にならず、子步の無いフレームの動きも次の子步が拾う）。
struct FluidSourceSubmission {
    // 源を見分ける鍵（FluidSourceId。0 は動きを引き継がず、burst もそのフレームの子步だけ）。
    uint64_t id = 0u;
    MATH::Vec3 worldPosition{};
    MATH::Quat worldRotation = MATH::Quat::Identity();
    MATH::Vec3 worldScale{1.0f, 1.0f, 1.0f};
    FluidSourceShape shape = FluidSourceShape::Sphere;
    float radius = 0.5f;
    float innerRadius = 0.0f;
    float coneAngleRadians = 0.0f;
    MATH::Vec3 boxHalfExtents{0.5f, 0.5f, 0.5f};
    float height = 1.0f;
    // 毎秒の注入密度（倍率と可視を掛けた後）と、このフレームで一度に注入する密度（burst）。
    float ratePerSecond = 0.0f;
    float burstDensity = 0.0f;
    // 注入する烟の色（層の色。cellColor の域だけが使う）。
    MATH::Vec3 color{1.0f, 1.0f, 1.0f};
    // 流体注入層（域の下に無い源、場面の域へ注入する）。力場・風・減衰は半径 0 でも域全体へは効かせず、形の届く範囲だけ。
    bool sceneInjection = false;
    FluidSourceSettings settings{};
    PARTICLES::ParticleForceField field{};
    // 世界空間の一様な加速度（風）と、場の中の速度の減衰 [1/s]。場の半径 0 は域全体。
    MATH::Vec3 wind{};
    float drag = 0.0f;
};

// 再生の実例の層から源の鍵を作る（0 にはしない）。
inline uint64_t FluidSourceId(const PARTICLES::ParticleEmitterInstanceKey& key) noexcept {
    uint64_t h = key.playbackNamespace * 0x9e3779b97f4a7c15ull ^
                 ((uint64_t{key.playbackSlot} << 32u | key.playbackGeneration) + 0x632be59bd9b4e019ull) ^
                 (uint64_t{key.emitterIndex} * 0xd6e8feb86659fd93ull);
    h = (h ^ (h >> 30u)) * 0xbf58476d1ce4e5b9ull;
    h = (h ^ (h >> 27u)) * 0x94d049bb133111ebull;
    h ^= h >> 31u;
    return h != 0u ? h : 1u;
}

// 1 フレームぶんの域。delta は時間の倍率を掛けたゲーム時間（一時停止・顿帧で 0）。域の大きさは settings.domainSize
// で決まり、層の拡縮は掛けない（拡縮やその動画で格子を作り直さないため）。
struct FluidDomainSubmission {
    PARTICLES::ParticleEmitterInstanceKey key{};
    bool resetSimulation = false;
    bool visible = true;
    // 局所空間で解く（域が層の変換に付いて動く）。偽なら作り直した時の世界の位置に固定する。
    bool localSpace = false;
    float deltaSeconds = 0.0f;
    MATH::Vec3 worldPosition{};
    MATH::Quat worldRotation = MATH::Quat::Identity();
    MATH::Vec4 colorMultiplier{1.0f, 1.0f, 1.0f, 1.0f};
    FluidDomainSettings settings{};
    std::vector<FluidSourceSubmission> sources{};
};

} // namespace HIKARI::RENDER3D::FLUID
