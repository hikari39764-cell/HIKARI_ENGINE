#pragma once
#include "Render3D/Vfx/HIKARI_FluidTypes.h"
#include "Render3D/Vfx/HIKARI_ParticleEmitterInstanceKey.h"
#include "Render3D/Vfx/HIKARI_ParticleForceField.h"
#include "Vfx/HIKARI_VfxPostProcessMode.h"
#include "Render3D/Vfx/HIKARI_ParticleSpatialEffect.h"

#include <array>
#include <memory>
#include <cstdint>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

#include "Core/Math/HIKARI_Math3D.h"
#include "RenderContracts/Material/HIKARI_MaterialEffectStack.h"
#include "RenderContracts/Vfx/HIKARI_ParticleMaterialMode.h"

namespace HIKARI {
class AssetRegistry;
struct MaterialAssetDescriptor;

namespace RENDER3D::MATERIAL {
struct MaterialExternalValueSnapshot;
}
} // namespace HIKARI

namespace HIKARI::RENDER3D::PARTICLES {

inline constexpr uint32_t kMaxParticleCurveKeys = 8u;
inline constexpr uint32_t kMaxParticleColorStops = 8u;
inline constexpr uint32_t kGpuParticlePoolCapacity = 65536u;
inline constexpr uint32_t kMaximumVfxSeekSubsteps = 4096u;
inline constexpr uint32_t kMaximumParticleEmitterSubmissionsPerFrame = 4096u;
inline constexpr uint32_t kDefaultParticleEmitterUploadPageCapacity = 256u;

// GPU material sources share one registry with scene surfaces. Particle
// playback namespaces are tagged as odd identities so they cannot alias
// the aligned pointer identities used by surface scene sources.
inline uintptr_t MakeParticleMaterialSourceIdentity(uint64_t sourceIdentity, const void* fallbackIdentity) noexcept {
    const uintptr_t untagged =
        sourceIdentity != 0u ? static_cast<uintptr_t>(sourceIdentity) : reinterpret_cast<uintptr_t>(fallbackIdentity);
    return (untagged << 1u) | uintptr_t{1u};
}

enum class ParticleCompositionLayerKind : uint8_t { Content, Group, Adjustment };
struct ParticleCompositionLayer {
    uint32_t emitterIndex = 0;
    uint32_t parentEmitterIndex = UINT32_MAX;
    ParticleCompositionLayerKind kind = ParticleCompositionLayerKind::Content;
    bool enabled = true;
    uint32_t matteSourceIndex = UINT32_MAX;
    bool matteLuminance = false;
    bool matteInverted = false;
    bool matteOnly = false;
    std::vector<MaterialEffectStep> effects;
};
struct ParticleComposition {
    ParticleEmitterInstanceKey instance{};
    // 上から下のレイヤー順。各グループが独立した合成スコープ。
    std::vector<ParticleCompositionLayer> layers{};
};

enum class ParticleShape : uint32_t {
    Point,
    Sphere,
    Cone,
    Box,
    Cylinder,
    Circle,
    Edge,
};

enum class ParticleRenderShape : uint32_t {
    Sprite,
    Ribbon,
    Ring,
    MeshBox,
    MeshSphere,
    MeshCylinder,
    MeshCone,
    MeshHemisphere,
    CrossPlane,
    HistoryRibbon, // 履歴所有者が生成する接続四角形。アセット形状とは別。
    // アセット形状の追加は履歴形状の後ろへ。番号を詰めると衝突する。
    MeshTorus = 10u,
    MeshCapsule = 11u,
};

enum class ParticleBlendMode : uint32_t {
    Alpha,
    Additive,
    // 戦闘の画面歪み遮罩へ書く層（色の経路では描かない）
    Distortion,
};

enum class ParticleDepthMode : uint32_t {
    WorldOccluded,
    SoftIntersection,
    ContactSurface,
    ForegroundAccent,
};

// Keep these values synchronized with VfxBillboardMode and the particle
// shader constants. Values 0 and 1 preserve cooked-program compatibility.
enum class ParticleBillboardMode : uint32_t {
    RotatedBillboard = 0u,
    FixedYAxis = 1u,
    DirectionalBillboard = 2u,
    Fixed = 3u,
    CylindricalBillboard = 4u,
    FaceCameraPosition = 5u,
};

static_assert(static_cast<uint32_t>(ParticleBillboardMode::RotatedBillboard) == 0u);
static_assert(static_cast<uint32_t>(ParticleBillboardMode::FixedYAxis) == 1u);
static_assert(static_cast<uint32_t>(ParticleBillboardMode::DirectionalBillboard) == 2u);
static_assert(static_cast<uint32_t>(ParticleBillboardMode::Fixed) == 3u);

enum class ParticleCurveInterpolation : uint32_t {
    Linear,
    Smooth,
    Step,
};

enum class ParticleDistributionMode : uint32_t {
    Constant,
    RandomRange,
};

enum class ParticleValueMode : uint32_t {
    Fixed,
    Pva,
    Easing,
    Curve,
};

enum class ParticleColorMode : uint32_t {
    Fixed,
    RandomRange,
    Easing,
    Curve,
    Gradient,
};

enum class ParticleEasingFunction : uint32_t {
    Linear,
    EaseIn,
    EaseOut,
    EaseInOut,
};

enum class ParticleRuntimePropertyType : uint32_t {
    Bool,
    Float,
    Float4,
};

struct ParticleDistributionFloat {
    ParticleDistributionMode mode = ParticleDistributionMode::Constant;
    float minimum = 0.0f;
    float maximum = 0.0f;
    uint32_t randomGroup = 0u;
};

struct ParticleDistributionFloat3 {
    ParticleDistributionMode mode = ParticleDistributionMode::Constant;
    MATH::Vec3 minimum{0.0f, 0.0f, 0.0f};
    MATH::Vec3 maximum{0.0f, 0.0f, 0.0f};
    uint32_t randomGroup = 0u;
};

struct ParticleDistributionFloat4 {
    ParticleDistributionMode mode = ParticleDistributionMode::Constant;
    MATH::Vec4 minimum{0.0f, 0.0f, 0.0f, 0.0f};
    MATH::Vec4 maximum{0.0f, 0.0f, 0.0f, 0.0f};
    uint32_t randomGroup = 0u;
};

struct ParticleCurveKey {
    float time = 0.0f;
    float value = 0.0f;
    float inTangent = 0.0f;
    float outTangent = 0.0f;
};

struct ParticleCurve {
    ParticleCurveInterpolation interpolation = ParticleCurveInterpolation::Linear;
    uint32_t keyCount = 0u;
    ParticleDistributionFloat randomOffset{};
    std::array<ParticleCurveKey, kMaxParticleCurveKeys> keys{};
};

struct ParticleColorStop {
    float time = 0.0f;
    MATH::Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct ParticleColorGradient {
    uint32_t stopCount = 0u;
    std::array<ParticleColorStop, kMaxParticleColorStops> stops{};
};

struct ParticleAnimatedFloat3 {
    ParticleValueMode mode = ParticleValueMode::Fixed;
    ParticleDistributionFloat3 initial{};
    ParticleDistributionFloat3 velocity{};
    ParticleDistributionFloat3 acceleration{};
    ParticleDistributionFloat3 easingStart{};
    ParticleDistributionFloat3 easingMiddle{};
    ParticleDistributionFloat3 easingEnd{};
    ParticleEasingFunction easingFunction = ParticleEasingFunction::EaseInOut;
    uint32_t hasEasingMiddle = 0u;
    std::array<ParticleCurve, 3u> curves{};
};

struct ParticleAnimatedColor {
    ParticleColorMode mode = ParticleColorMode::Fixed;
    ParticleDistributionFloat4 fixed{};
    ParticleDistributionFloat4 easingStart{};
    ParticleDistributionFloat4 easingMiddle{};
    ParticleDistributionFloat4 easingEnd{};
    ParticleEasingFunction easingFunction = ParticleEasingFunction::EaseInOut;
    uint32_t hasEasingMiddle = 0u;
    std::array<ParticleCurve, 4u> curves{};
    ParticleColorGradient gradient{};
};

struct ParticleEmitterSimulationParameters {
    uint32_t capacity = 0u;
    ParticleDistributionFloat lifetime{};
    ParticleShape shape = ParticleShape::Point;
    ParticleDistributionFloat shapeRadius{};
    float coneAngleRadians = 0.0f;
    MATH::Vec3 shapeBoxHalfExtents{0.5f, 0.5f, 0.5f};
    float shapeCylinderHeight = 1.0f;
    float shapeCircleInnerRadius = 0.0f;
    uint32_t shapeCircleEmitOnEdge = 0u;
    // 円形エミッタの放出角度範囲。360 で全周。
    float shapeCircleArcDegrees = 360.0f;
    float shapeEdgeLength = 1.0f;
    ParticleDistributionFloat radialSpeed{};
    ParticleAnimatedFloat3 position{};
    ParticleAnimatedFloat3 rotationRadians{};
    ParticleAnimatedFloat3 scale{};
    uint32_t localSimulation = 0u;
    uint32_t collisionPlaneEnabled = 0u;
    float collisionPlaneHeight = 0.0f;
    float restitution = 0.35f;
    float friction = 0.2f;
    HIKARI::RENDER3D::PARTICLES::ParticleForceField field{};
    MATH::Vec3 gravity{0.0f, 0.0f, 0.0f};
    float drag = 0.0f;
    // 流体に乗る割合 [1/s]（VfxForcesModuleData::fluidFollow）。
    float fluidFollow = 0.0f;
    ParticleAnimatedColor color{};
};

struct ParticleEmitterRuntimeOverrides {
    uint32_t enabled = 1u;
    float spawnRateMultiplier = 1.0f;
    float radialSpeedMultiplier = 1.0f;
    float scaleMultiplier = 1.0f;
    float gravityMultiplier = 1.0f;
    MATH::Vec4 colorMultiplier{1.0f, 1.0f, 1.0f, 1.0f};
};

struct ParticleMaterialParameterValue {
    uint32_t slot = 0;
    float value = 0;
};
struct ParticleEmitterRenderParameters {
    std::vector<ParticleSpatialEffect> spatialEffects;
    std::vector<ParticleMaterialParameterValue> animatedParameters;
    std::shared_ptr<const MaterialAssetDescriptor> effectMaterial{};
    std::string effectMaterialKey{};
    bool effectMaterialIncludesParticleColor = false;
    ParticleMaterialMode materialMode = ParticleMaterialMode::StandardUnlit;
    std::string colorTextureAssetGuid{};
    // 効果スタックが参照する追加スロット。空なら従来どおり底色のみ。
    std::string maskTextureAssetGuid{};
    std::string noiseTextureAssetGuid{};
    std::string materialAssetGuid{};
    ParticleBlendMode blendMode = ParticleBlendMode::Alpha;
    VFX::VfxPostProcessMode postProcessMode = VFX::VfxPostProcessMode::Scene;
    ParticleRenderShape shape = ParticleRenderShape::Sprite;
    ParticleBillboardMode billboardMode = ParticleBillboardMode::RotatedBillboard;
    ParticleDepthMode depthMode = ParticleDepthMode::WorldOccluded;
    float softIntersectionDistance = 0.15f;
    float depthBias = 0.00035f;
    uint32_t flipbookColumns = 1u;
    uint32_t flipbookRows = 1u;
    uint32_t flipbookFrameCount = 1u;
    float flipbookFramesPerSecond = 0.0f;
    bool flipbookLoop = true;
    bool flipbookRandomStart = false;
    uint32_t ribbonSegmentCount = 16u;
    float ribbonWidth = 0.1f;
    float ringOuterRadius = 1.0f;
    float ringInnerRadius = 0.5f;
    uint32_t ringSegmentCount = 32u;
    float ringStartAngleRadians = 0.0f;
    float ringEndAngleRadians = 6.28318530717958647692f;
    // 形状が作るパラメータUVを材質UVへ変換する契約。0 は既存の
    // 正規化挙動を保ち、1 は弧長・周長・高さなどの実寸で U をタイルする。
    uint32_t uvMode = 0u;
    float uvTileLength = 1.0f;
    float uvTilingU = 1.0f;
    float uvTilingV = 1.0f;
    float uvRotationRadians = 0.0f;
    // 円柱/円錐の「周方向U・軸方向V」を入れ替える。
    uint32_t uvSwapAxes = 0u;
    // 回転・拡縮の基準点。形状中心（原点）からのローカルオフセット。
    MATH::Vec3 anchor{0.0f, 0.0f, 0.0f};
    // 接続ターゲット: 幾何を「原点 → 参照点」の区間へ張る。0 は無効。
    inline static constexpr uint32_t kInvalidConnectLayerIndex = 0xffffffffu;
    uint32_t connectTargetMode = 0u;
    uint32_t connectTargetLayerIndex = kInvalidConnectLayerIndex;
    MATH::Vec3 connectLocalPosition{0.0f, 0.0f, 0.0f};
    float connectPivot = 0.5f;
    float connectLengthScale = 1.0f;
    float connectStartOffset = 0.0f;
    float connectEndOffset = 0.0f;
    uint32_t connectFlags = 0u;
    // Mesh 出力: 有効ならこのレイヤーは粒子 pass ではなくモデル描画へ回す。
    std::string meshAssetGuid{};
    uint32_t meshOutputFlags = 0u;
    // 形状パラメータ 0..1 に沿った幅の倍率。1,1 で形状を変えない。
    float taperStart = 1.0f;
    float taperEnd = 1.0f;
    // 円柱/円錐の半径。既定 0.5 は従来の直径 1 と同じ。
    float cylinderTopRadius = 0.5f;
    float cylinderBottomRadius = 0.5f;
    float coneBaseRadius = 0.5f;
    // 長さ軸を持つ形状の変形（bend は無次元、twist はラジアン）。
    float bendAmount = 0.0f;
    float twistRadians = 0.0f;
    float torusMajorRadius = 0.4f;
    float torusMinorRadius = 0.12f;
    float capsuleRadius = 0.35f;
    // 軸固定ビルボードが固定するローカル軸（0=X, 1=Y, 2=Z）。
    uint32_t billboardAxis = 0u;
};

struct ParticleShapePose {
    MATH::Vec3 position{};
    MATH::Quat rotation = MATH::Quat::Identity();
    MATH::Vec3 scale{1, 1, 1};
    MATH::Vec4 color{1, 1, 1, 1};
    float age = 0.0f;
    float duration = 1.0f;
    std::array<MATH::Vec3, 4> corners{};
    MATH::Vec2 edgeAlpha{1, 1};
    MATH::Vec4 startColor{1, 1, 1, 1};
    MATH::Vec2 uvRange{0, 1};
};

struct ParticleEmitterSubmission {
    ParticleEmitterInstanceKey key{};
    // 履歴に依存しない形状投影。生成・物理積分・寿命終了を通らない。
    bool directShape = false;
    // 履歴有効時だけ確保。全サンプルを単一 DEFAULT プール・単一描画で処理。
    std::vector<ParticleShapePose> shapePoses{};
    float shapeAgeSeconds = 0.0f;
    float shapeDurationSeconds = 1.0f;
    uint32_t simulationOrdinal = 0u;
    bool drawAfterSimulation = true;
    bool resetSimulation = false;
    uint32_t spawnCount = 0u;
    uint32_t randomSeed = 0u;
    float deltaSeconds = 0.0f;
    float emitterAgeSeconds = 0.0f;
    MATH::Vec3 worldPosition{0.0f, 0.0f, 0.0f};
    MATH::Quat worldRotation = MATH::Quat::Identity();
    MATH::Vec3 worldScale{1.0f, 1.0f, 1.0f};
    ParticleEmitterRuntimeOverrides overrides{};
    ParticleEmitterSimulationParameters simulation{};
    ParticleEmitterRenderParameters render{};
};

inline ParticleShapePose CurrentShapePose(const ParticleEmitterSubmission& s) {
    return {s.worldPosition,
            s.worldRotation,
            s.worldScale * s.overrides.scaleMultiplier,
            s.overrides.colorMultiplier,
            s.shapeAgeSeconds,
            s.shapeDurationSeconds};
}

struct ParticleRenderDeliveryToken {
    uint64_t sourceIdentity = 0u;
    uint64_t sequence = 0u;

    bool IsValid() const noexcept { return sourceIdentity != 0u && sequence != 0u; }

    bool operator==(const ParticleRenderDeliveryToken&) const noexcept = default;
};

struct ParticleRenderInput {
    // Stable owner identity used by the shared GPU material registry.
    // It must remain stable for the lifetime of the playback namespace.
    uint64_t sourceIdentity = 0u;
    // Monotonic delivery identity. Runtime playback retains an event-like
    // reset/spawn snapshot until the renderer reports this exact token as
    // recorded into a valid frame command list.
    uint64_t deliverySequence = 0u;
    uint64_t frameIndex = 0u;
    std::vector<ParticleEmitterInstanceKey> retiredEmitters{};
    std::vector<ParticleEmitterSubmission> emitters{};
    std::vector<ParticleComposition> compositions{};
    // 流体層の域。退役は retiredEmitters の同じ実例键で伝える。
    std::vector<FLUID::FluidDomainSubmission> fluids{};

    ParticleRenderDeliveryToken GetDeliveryToken() const noexcept { return {sourceIdentity, deliverySequence}; }
};

// Immutable frame boundary consumed by the main GPU-driven renderer.
// Particle simulation and drawing no longer own a public render entry
// point; every view supplies this source through RenderViewContext.
struct ParticleRenderFrameSource {
    const ParticleRenderInput* input = nullptr;
    const AssetRegistry* assetRegistry = nullptr;
    const MATERIAL::MaterialExternalValueSnapshot* externalValues = nullptr;
    // 場面の流体を押すもの（このフレームの障碍物・冲量・力・注入）。呼び出し側が描画の間持つ。
    std::span<const FLUID::FluidInteraction> fluidInteractions{};
    // 回読が届いた汇の量を受け取る先（場面が次のフレームに FluidInteractionService へ渡す）。null なら pass に溜める。
    std::vector<FLUID::FluidSinkReport>* fluidSinkReports = nullptr;

    bool HasDrawWork() const noexcept { return input != nullptr && !input->emitters.empty(); }

    bool HasFrameWork() const noexcept {
        return input != nullptr &&
               (!input->emitters.empty() || !input->retiredEmitters.empty() || !input->fluids.empty());
    }
};

static_assert(std::is_trivially_copyable_v<ParticleDistributionFloat>);
static_assert(std::is_trivially_copyable_v<ParticleDistributionFloat3>);
static_assert(std::is_trivially_copyable_v<ParticleDistributionFloat4>);
static_assert(std::is_trivially_copyable_v<ParticleCurve>);
static_assert(std::is_trivially_copyable_v<ParticleAnimatedFloat3>);
static_assert(std::is_trivially_copyable_v<ParticleAnimatedColor>);
static_assert(std::is_trivially_copyable_v<ParticleEmitterSimulationParameters>);
static_assert(std::is_trivially_copyable_v<ParticleEmitterRuntimeOverrides>);

} // namespace HIKARI::RENDER3D::PARTICLES
