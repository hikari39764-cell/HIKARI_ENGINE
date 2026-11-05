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
    MATH::Vec3 emissionColor{1.0f, 0.45f, 0.12f};
    float emissionIntensity = 0.0f;
    float emissionTemperature = 0.6f;
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
    // 形状引导：形の表面から guideThickness [m] の帯の中の烟を表面へ引き戻す強さ [1/s]。
    float guideStrength = 0.0f;
    float guideThickness = 0.25f;
    FluidForceMode forceMode = FluidForceMode::Acceleration;

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
    field("emissionIntensity", &S::emissionIntensity, Range{0.0, 1000.0}, Group{"Look"});
    field("emissionTemperature", &S::emissionTemperature, Range{0.0, 10.0}, Group{"Look"});
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
    field("guideStrength", &S::guideStrength, Range{0.0, 100.0}, Unit{"1/s"}, Group{"Shape Guide"});
    field("guideThickness", &S::guideThickness, Range{0.01, 500.0}, Unit{"m"}, Group{"Shape Guide"});
    field("forceMode", &S::forceMode, kFluidForceModeNames);
};

// 流体を押すもの（場面の障碍物とゲームの押し）。障碍物は形の中の速度を自分の速度へ寄せ（Brinkman の罰則、投影の前）、
// 烟は止まった障碍物を回り込み、動く障碍物に押し出され引きずられる（回転の速度は持たない）。冲量・力は extents.x を
// 半径とする球の範囲で中心ほど強い。向きがあれば加速（冲量は一子步で速さを足す）、向きが 0 なら体積の湧き出しで、
// 半径の球面を外へ strength [m/s] で流れ出す（負で吸い込む。冲量は kFluidRadialImpulseSeconds の間、開いた境界の域だけ）。
enum class FluidInteractionKind : uint32_t { Obstacle, Impulse, Force };
enum class FluidInteractionShape : uint32_t { Sphere, Capsule, Box };

struct FluidInteraction {
    FluidInteractionKind kind = FluidInteractionKind::Obstacle;
    FluidInteractionShape shape = FluidInteractionShape::Sphere;
    MATH::Vec3 worldPosition{};
    MATH::Quat worldRotation = MATH::Quat::Identity();
    // Sphere：x 半径。Capsule：x 半径、y 局所 Y 軸の線分の半分。Box：半分の大きさ [m]。冲量・力は x 半径。
    MATH::Vec3 extents{0.5f, 0.5f, 0.5f};
    // 障碍物の速度（世界 [m/s]）。
    MATH::Vec3 velocity{};
    // 冲量・力の向き（世界、0 なら中心から外向き）。
    MATH::Vec3 direction{};
    // 向きのある冲量は足す速さ [m/s]、力は加速度 [m/s²]。向きの無い冲量・力は半径での外向きの流れの速さ [m/s]。
    float strength = 0.0f;
};

// 向きの無い冲量が湧き出し続けるゲーム時間 [s]（半径での押し出しの距離は約 strength × この時間）。
inline constexpr double kFluidRadialImpulseSeconds = 0.1;

inline constexpr uint32_t kMaximumFluidInteractionsPerFrame = 64u;
inline constexpr uint32_t kMaximumFluidInteractionsPerDomain = 32u;

// 源の形（VFX の emission shape と同じ種類）。値は ParticleShape と同じ並び。
enum class FluidSourceShape : uint32_t { Point, Sphere, Cone, Box, Cylinder, Circle, Edge, Domain };

// 1 子步ぶんの源。座標は源の局所空間（形と力場）で、GPU が域の格子から変換する。
struct FluidSourceSubmission {
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
    FluidSourceSettings settings{};
    PARTICLES::ParticleForceField field{};
    // 世界空間の一様な加速度（風）と、場の中の速度の減衰 [1/s]。場の半径 0 は域全体。
    MATH::Vec3 wind{};
    float drag = 0.0f;
};

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
