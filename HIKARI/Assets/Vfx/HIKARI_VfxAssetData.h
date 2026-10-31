#pragma once
#include "Render3D/Vfx/HIKARI_ParticleForceField.h"
#include "Vfx/HIKARI_VfxPostProcessMode.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Core/HIKARI_AssetGuid.h"
#include "Assets/Vfx/HIKARI_VfxAnimationTiming.h"
#include "Assets/Material/Authoring/HIKARI_MaterialEffectStack.h"
#include "Core/Math/HIKARI_Math3D.h"
#include "Core/Math/HIKARI_MathValidation.h"

namespace HIKARI::VFX {

inline constexpr uint32_t kVfxSourceVersion = 15u;

// 形状はレイヤー時間、粒子は個々の寿命で同じアニメーションを評価する。
enum class VfxLayerKind : uint8_t { Particles, Shape, Group, Adjustment };

enum class VfxValueType : uint8_t {
    Bool,
    Int,
    Float,
    Float2,
    Float3,
    Float4,
    Color,
};

struct VfxValue {
    VfxValueType type = VfxValueType::Float;
    bool boolean = false;
    int64_t integer = 0;
    MATH::Vec4 numeric{0.0f, 0.0f, 0.0f, 0.0f};

    // Bool/Int は常に有効、数値型は 4 成分がすべて有限の時だけ有効。未知の型は無効。
    bool IsFinite() const noexcept {
        switch (type) {
        case VfxValueType::Bool:
        case VfxValueType::Int: return true;
        case VfxValueType::Float:
        case VfxValueType::Float2:
        case VfxValueType::Float3:
        case VfxValueType::Float4:
        case VfxValueType::Color: return MATH::IsFinite(numeric);
        default: return false;
        }
    }
};

enum class VfxShapeType : uint8_t {
    Point,
    Sphere,
    Cone,
    Box,
    Cylinder,
    Circle,
    Edge,
};

enum class VfxRenderShapeType : uint8_t {
    Sprite,
    Ribbon,
    Ring,
    MeshBox,
    MeshSphere,
    MeshCylinder,
    MeshCone,
    MeshHemisphere,
    CrossPlane,
    MeshTorus,
    MeshCapsule,
};

// 形状 UV の契約。Parameter は従来どおりパラメータ比率で正規化し、
// WorldLength は弧長・周長・高さなどの実寸でタイリングする。
enum class VfxShapeUvMode : uint8_t {
    Parameter,
    WorldLength,
};

enum class VfxBlendMode : uint8_t {
    Alpha,
    Additive,
    // 画面の歪み：色は描かず、戦闘の画面歪み遮罩へ書く（RG = 画面 UV のずれ、B = 覆い、A = ぼかし）。
    // 層色の R が強さ、G がぼかし。B が 0.5 以上なら底色貼図の RG を法線場に、未満なら不透明度の縁で屈折する。
    // 全体後処理のある画面でだけ合成され、合成（グループ・マット）には参加しない。
    Distortion,
};

enum class VfxMaterialMode : uint8_t {
    StandardUnlit,
    CustomMaterial,
    StandardLit,
    CustomLit,
};

constexpr bool IsCustomVfxMaterial(VfxMaterialMode mode) noexcept {
    return mode == VfxMaterialMode::CustomMaterial || mode == VfxMaterialMode::CustomLit;
}

enum class VfxParticleDepthMode : uint8_t {
    WorldOccluded,
    SoftIntersection,
    ContactSurface,
    ForegroundAccent,
};

enum class VfxParticleCapacityMode : uint8_t {
    Automatic,
    Fixed,
};

// Numeric values intentionally mirror the cooked HVFXC contract. Values
// 0 and 1 retain the former CameraFacing and Vertical meanings.
enum class VfxBillboardMode : uint8_t {
    RotatedBillboard = 0u,
    FixedYAxis = 1u,
    DirectionalBillboard = 2u,
    Fixed = 3u,
    // 長さ軸は形状に固定し、幅方向だけを常にカメラへ向ける。
    // ビームや帯が真横から見ても紙片にならない。
    CylindricalBillboard = 4u,
    // 面をカメラ平面ではなくカメラ位置へ正対させる。画面端でも破綻しない。
    FaceCameraPosition = 5u,
};

// Look At Line（軸固定ビルボード）が固定するローカル軸。
enum class VfxBillboardAxis : uint8_t {
    LocalX = 0u,
    LocalY = 1u,
    LocalZ = 2u,
};

static_assert(static_cast<uint8_t>(VfxBillboardMode::RotatedBillboard) == 0u);
static_assert(static_cast<uint8_t>(VfxBillboardMode::FixedYAxis) == 1u);
static_assert(static_cast<uint8_t>(VfxBillboardMode::DirectionalBillboard) == 2u);
static_assert(static_cast<uint8_t>(VfxBillboardMode::Fixed) == 3u);
static_assert(static_cast<uint8_t>(VfxBillboardMode::CylindricalBillboard) == 4u);

enum class VfxCurveInterpolation : uint8_t {
    Linear,
    Smooth,
    Step,
};

enum class VfxDistributionMode : uint8_t {
    Constant,
    RandomRange,
};

enum class VfxAnimatedValueMode : uint8_t {
    Fixed,
    Pva,
    Easing,
    Curve,
};

enum class VfxAnimatedColorMode : uint8_t {
    Fixed,
    RandomRange,
    Easing,
    Curve,
    Gradient,
};

enum class VfxEasingFunction : uint8_t {
    Linear,
    EaseIn,
    EaseOut,
    EaseInOut,
};

// These values describe authoring intent only. Runtime resolves the
// owner/socket target through its narrow playback context; an asset never
// stores a live Scene or renderer pointer.
enum class VfxAttachmentMode : uint8_t {
    World,
    Owner,
    OwnerSocket,
};

enum class VfxFollowMode : uint8_t {
    None,
    Position,
    Rotation,
    Transform,
};

enum class VfxSpecialOutputType : uint8_t {
    WeaponPathDistortion,
    ScreenImpulse,
    SkinnedAfterimage,
    // エフェクト自身が runtime 光源を出す。周囲のジオメトリを照らし、
    // 加法発光だけでは出せない「その場に光がある」感覚を作る。
    Light,
};

// Keep these authoring limits identical to the private presentation
// consumers: the screen post path supports four wave layers and the
// afterimage history keeps one to five real silhouettes.
inline constexpr uint32_t kVfxScreenImpulseWaveCountMin = 1u;
inline constexpr uint32_t kVfxScreenImpulseWaveCountMax = 4u;
inline constexpr uint32_t kVfxAfterimageSampleCountMin = 1u;
inline constexpr uint32_t kVfxAfterimageSampleCountMax = 5u;
inline constexpr float kVfxWeaponDistortionMin = 0.0f;

// VFX 光源出力の編集レンジ。UI と検証が同じ範囲を参照する。
// 光束 [lm]。シーンの点光源と同じ単位。
inline constexpr float kVfxLightFluxMin = 0.0f;
inline constexpr float kVfxLightFluxMax = 1000000.0f;
inline constexpr float kVfxLightRangeMin = 0.05f;
inline constexpr float kVfxLightRangeMax = 100.0f;
inline constexpr float kVfxLightSourceRadiusMin = 0.0f;
inline constexpr float kVfxLightSourceRadiusMax = 10.0f;
inline constexpr float kVfxLightFlickerFrequencyMin = 0.0f;
inline constexpr float kVfxLightFlickerFrequencyMax = 60.0f;
inline constexpr float kVfxLightFlickerAmplitudeMin = 0.0f;
inline constexpr float kVfxLightFlickerAmplitudeMax = 1.0f;
inline constexpr uint32_t kVfxLightPriorityMin = 0u;
inline constexpr uint32_t kVfxLightPriorityMax = 32u;
inline constexpr float kVfxWeaponDistortionMax = 8.0f;
inline constexpr float kVfxWeaponBlurMin = 0.0f;
inline constexpr float kVfxWeaponBlurMax = 8.0f;
inline constexpr float kVfxWeaponOpacityMin = 0.0f;
inline constexpr float kVfxWeaponOpacityMax = 1.0f;
inline constexpr float kVfxWeaponEmissiveMin = 0.0f;
inline constexpr float kVfxWeaponEmissiveMax = 8.0f;
inline constexpr float kVfxWeaponFlowMin = 0.0f;
inline constexpr float kVfxWeaponFlowMax = 2.0f;
// The presentation bridge maps profile width / 0.15 to the renderer's
// normalized [0.05, 8] width range.
inline constexpr float kVfxWeaponWidthMin = 0.0075f;
inline constexpr float kVfxWeaponWidthMax = 1.2f;
inline constexpr float kVfxWeaponLifetimeMin = 0.035f;
inline constexpr float kVfxWeaponLifetimeMax = 0.45f;
inline constexpr float kVfxScreenDurationMin = 0.02f;
inline constexpr float kVfxScreenDurationMax = 1.5f;
inline constexpr float kVfxScreenRadiusMin = 0.02f;
inline constexpr float kVfxScreenRadiusMax = 1.5f;
inline constexpr float kVfxScreenRingThicknessMin = 0.001f;
inline constexpr float kVfxScreenRingThicknessMax = 0.35f;
inline constexpr float kVfxScreenDistortionMin = 0.0f;
inline constexpr float kVfxScreenDistortionMax = 0.25f;
inline constexpr float kVfxScreenChromaticMin = 0.0f;
inline constexpr float kVfxScreenChromaticMax = 0.05f;
inline constexpr float kVfxScreenFlashMin = 0.0f;
inline constexpr float kVfxScreenFlashMax = 4.0f;
inline constexpr float kVfxScreenIntensityMin = 0.0f;
inline constexpr float kVfxScreenIntensityMax = 8.0f;
inline constexpr float kVfxScreenWaveFrequencyMin = 0.05f;
inline constexpr float kVfxScreenWaveFrequencyMax = 8.0f;
inline constexpr float kVfxScreenWaveAmplitudeMin = 0.0f;
inline constexpr float kVfxScreenWaveAmplitudeMax = 8.0f;
inline constexpr float kVfxScreenWaveDampingMin = 0.05f;
inline constexpr float kVfxScreenWaveDampingMax = 8.0f;
inline constexpr float kVfxAfterimageLifetimeMin = 0.04f;
inline constexpr float kVfxAfterimageLifetimeMax = 1.0f;
inline constexpr float kVfxAfterimageOpacityMin = 0.01f;
inline constexpr float kVfxAfterimageOpacityMax = 1.0f;
inline constexpr float kVfxAfterimageFresnelMin = 0.5f;
inline constexpr float kVfxAfterimageFresnelMax = 8.0f;
inline constexpr float kVfxAfterimageDissolveMin = 0.1f;
inline constexpr float kVfxAfterimageDissolveMax = 4.0f;
inline constexpr float kVfxAfterimageShellOffsetMin = 0.0f;
inline constexpr float kVfxAfterimageShellOffsetMax = 0.08f;
inline constexpr float kVfxAfterimageIntensityMin = 0.05f;
inline constexpr float kVfxAfterimageIntensityMax = 4.0f;

struct VfxTransformData {
    MATH::Vec3 position{0.0f, 0.0f, 0.0f};
    MATH::Vec3 rotationEulerDegrees{0.0f, 0.0f, 0.0f};
    MATH::Vec3 scale{1.0f, 1.0f, 1.0f};
};

struct VfxAttachmentData {
    std::string id{};
    VfxAttachmentMode mode = VfxAttachmentMode::World;
    VfxFollowMode follow = VfxFollowMode::None;
    // Runtime supplies the owner object; the asset stores only the
    // semantic socket name, never an object/resource pointer.
    std::string socketName{};
    VfxTransformData offset{};
};

struct VfxBurstKeyData {
    std::string id{};
    float time = 0.0f;
    uint32_t count = 0u;
};

struct VfxWeaponPathDistortionProfile {
    float distortion = 1.0f;
    float blur = 0.05f;
    float opacity = 1.0f;
    float emissive = 1.0f;
    float flow = 1.0f;
    float width = 0.15f;
    float lifetimeSeconds = 0.12f;
    MATH::Vec4 bodyColor{1.0f, 1.0f, 1.0f, 1.0f};
    MATH::Vec4 edgeColor{1.0f, 1.0f, 1.0f, 0.0f};
};

struct VfxScreenImpulseProfile {
    float durationSeconds = 0.18f;
    float maxRadius = 1.0f;
    float ringThickness = 0.15f;
    float distortion = 0.1f;
    float chromaticAberration = 0.0f;
    float flash = 0.0f;
    float intensity = 1.0f;
    MATH::Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    float waveFrequency = 1.0f;
    float waveAmplitude = 1.0f;
    float waveDamping = 1.0f;
    uint32_t waveCount = 1u;
};

struct VfxSkinnedAfterimageProfile {
    uint32_t sampleCount = 4u;
    float lifetimeSeconds = 0.24f;
    float opacity = 1.0f;
    float fresnel = 0.5f;
    float dissolve = 0.1f;
    float shellOffset = 0.0f;
    MATH::Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
};

// VFX レイヤーが runtime 光源を出すためのプロファイル。色はレイヤー色と
// 乗算されるので、エフェクトの色をそのまま周囲へ落とせる。
struct VfxLightOutputProfile {
    MATH::Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
    // 光束 [lm]。
    float luminousFlux = 10000.0f;
    float range = 6.0f;
    float sourceRadius = 0.05f;
    // 0 のときは点滅しない。周波数は Hz、振幅は光束への倍率。
    float flickerFrequency = 0.0f;
    float flickerAmplitude = 0.0f;
    // 同時に複数の VFX 光源が競合したときの優先度。大きいほど残る。
    uint32_t priority = 0u;
    bool castsShadows = false;
};

struct VfxSpecialOutputData {
    std::string id{};
    VfxSpecialOutputType type = VfxSpecialOutputType::WeaponPathDistortion;
    bool enabled = true;
    VfxWeaponPathDistortionProfile weaponPathDistortion{};
    VfxScreenImpulseProfile screenImpulse{};
    VfxSkinnedAfterimageProfile skinnedAfterimage{};
    VfxLightOutputProfile light{};
};

template <class T>
struct VfxDistributionData {
    VfxDistributionMode mode = VfxDistributionMode::Constant;
    T minimum{};
    T maximum{};
    uint32_t randomGroup = 0u;
};

using VfxFloatDistributionData = VfxDistributionData<float>;
using VfxVec3DistributionData = VfxDistributionData<MATH::Vec3>;
using VfxColorDistributionData = VfxDistributionData<MATH::Vec4>;

struct VfxCurveKeyData {
    std::string id{};
    float time = 0.0f;
    float value = 0.0f;
    float inTangent = 0.0f;
    float outTangent = 0.0f;
};

struct VfxCurveData {
    VfxCurveInterpolation interpolation = VfxCurveInterpolation::Linear;
    VfxFloatDistributionData randomOffset{};
    std::vector<VfxCurveKeyData> keys{};
};

struct VfxVec3CurveData {
    VfxCurveData x{};
    VfxCurveData y{};
    VfxCurveData z{};
};

struct VfxColorCurveData {
    VfxCurveData r{};
    VfxCurveData g{};
    VfxCurveData b{};
    VfxCurveData a{};
};

struct VfxColorStopData {
    std::string id{};
    float time = 0.0f;
    MATH::Vec4 color{1.0f, 1.0f, 1.0f, 1.0f};
};

struct VfxColorGradientData {
    std::vector<VfxColorStopData> stops{};
};

template <class T>
struct VfxPvaData {
    VfxDistributionData<T> initial{};
    VfxDistributionData<T> velocity{};
    VfxDistributionData<T> acceleration{};
};

template <class T>
struct VfxEasingData {
    VfxDistributionData<T> start{};
    VfxDistributionData<T> middle{};
    VfxDistributionData<T> end{};
    VfxEasingFunction function = VfxEasingFunction::EaseInOut;
    bool useMiddle = false;
};

struct VfxAnimatedVec3Data {
    VfxAnimatedValueMode mode = VfxAnimatedValueMode::Fixed;
    VfxVec3DistributionData fixed{};
    VfxPvaData<MATH::Vec3> pva{};
    VfxEasingData<MATH::Vec3> easing{};
    VfxVec3CurveData curve{};
};

struct VfxAnimatedColorData {
    VfxAnimatedColorMode mode = VfxAnimatedColorMode::Fixed;
    VfxColorDistributionData fixed{};
    VfxEasingData<MATH::Vec4> easing{};
    VfxColorCurveData curve{};
    VfxColorGradientData gradient{};
};

struct VfxExposedInputData {
    std::string id{};
    std::string name{};
    VfxValueType type = VfxValueType::Float;
    VfxValue defaultValue{};
};

struct VfxPropertyAddress {
    std::string emitterId{};
    std::string moduleId{};
    std::string propertyId{};
};

struct VfxInputLinkData {
    std::string id{};
    std::string inputId{};
    VfxPropertyAddress target{};
    float scale = 1.0f;
    float bias = 0.0f;
};

struct VfxBasicModuleData {
    std::string id{};
    bool enabled = true;
    VfxFloatDistributionData lifetime{};
    bool localSimulation = false;
};

struct VfxEmitterTransformModuleData {
    std::string id{};
    VfxTransformData transform{};
};

struct VfxSpawnModuleData {
    std::string id{};
    float ratePerSecond = 10.0f;
    std::vector<VfxBurstKeyData> burstTimeline{};
    VfxParticleCapacityMode capacityMode = VfxParticleCapacityMode::Automatic;
    // In Automatic mode this remains the authored safety ceiling. The
    // compiler derives the smaller live allocation from spawn and TTL.
    uint32_t maximumParticles = 1024u;
};

struct VfxShapeModuleData {
    std::string id{};
    VfxShapeType type = VfxShapeType::Point;
    VfxFloatDistributionData radius{};
    float coneAngleDegrees = 30.0f;
    MATH::Vec3 boxHalfExtents{0.5f, 0.5f, 0.5f};
    float cylinderHeight = 1.0f;
    float circleInnerRadius = 0.0f;
    bool circleEmitOnEdge = false;
    // 円形エミッタの放出角度範囲。360 で全周（従来動作）、0<arc<360 で
    // その角度内だけに放出する。エミッタ自体を回して狙いを付ける。
    float circleArcDegrees = 360.0f;
    float edgeLength = 1.0f;
};

struct VfxPositionModuleData {
    std::string id{};
    bool orientAlongPath = false;
    VfxFloatDistributionData radialSpeed{};
    VfxAnimatedVec3Data value{};
};

struct VfxRotationModuleData {
    std::string id{};
    VfxAnimatedVec3Data eulerDegrees{};
};

struct VfxScaleModuleData {
    std::string id{};
    VfxAnimatedVec3Data value{};
};

struct VfxForcesModuleData {
    std::string id{};
    MATH::Vec3 gravity{0.0f, 0.0f, 0.0f};
    float drag = 0.0f;
    bool collisionPlaneEnabled = false;
    float collisionPlaneHeight = 0.0f;
    float restitution = 0.35f;
    float friction = 0.2f;
    HIKARI::RENDER3D::PARTICLES::ParticleForceField field{};
};

struct VfxColorModuleData {
    std::string id{};
    VfxAnimatedColorData value{};
};

struct VfxRenderCommonModuleData {
    std::string id{};
    VfxMaterialMode materialMode = VfxMaterialMode::StandardUnlit;
    AssetGuid colorTextureAssetGuid{};
    // 追加の貼図スロット。遮罩は不透明度を、ノイズは歪み/溶解の場を
    // 与える。空のままなら従来どおり底色 1 枚だけで描く。
    AssetGuid maskTextureAssetGuid{};
    AssetGuid noiseTextureAssetGuid{};
    // 2 つ目のノイズ場。大まかな流れと細部を別々のノイズで与える。
    AssetGuid detailNoiseTextureAssetGuid{};
    AssetGuid flowMapTextureAssetGuid{};
    AssetGuid materialAssetGuid{};
    VfxBlendMode blendMode = VfxBlendMode::Alpha;
    VfxPostProcessMode postProcessMode = VfxPostProcessMode::Scene;
    VfxParticleDepthMode depthMode = VfxParticleDepthMode::WorldOccluded;
    float softIntersectionDistance = 0.15f;
    float depthBias = 0.00035f;
    uint32_t flipbookColumns = 1u;
    uint32_t flipbookRows = 1u;
    uint32_t flipbookFrameCount = 1u;
    float flipbookFramesPerSecond = 0.0f;
    bool flipbookLoop = true;
    bool flipbookRandomStart = false;
};

struct VfxRenderShapeModuleData {
    std::string id{};
    VfxRenderShapeType type = VfxRenderShapeType::Sprite;
    VfxBillboardMode billboardMode = VfxBillboardMode::RotatedBillboard;
    uint32_t ribbonSegmentCount = 16u;
    float ribbonWidth = 0.1f;
    float ringOuterRadius = 1.0f;
    float ringInnerRadius = 0.5f;
    uint32_t ringSegmentCount = 32u;
    float ringStartAngleDegrees = 0.0f;
    float ringEndAngleDegrees = 360.0f;
    VfxShapeUvMode uvMode = VfxShapeUvMode::Parameter;
    // WorldLength モードでのテクスチャ1リピートあたりの世界長。
    float uvTileLength = 1.0f;
    // 契約適用後の U/V タイル乗数。Parameter モードでも有効。
    float uvTilingU = 1.0f;
    float uvTilingV = 1.0f;
    float uvRotationDegrees = 0.0f;
    bool uvSwapAxes = false;
    // 形状パラメータ 0..1 に沿った幅の倍率。1,1 で形状を変えない。
    float taperStart = 1.0f;
    float taperEnd = 1.0f;
    // 円柱/円錐の半径。既定 0.5 は従来の直径 1 と同じ。
    float cylinderTopRadius = 0.5f;
    float cylinderBottomRadius = 0.5f;
    float coneBaseRadius = 0.5f;
    // 長さ軸を持つ形状の変形。bend は中心線を幅方向へ放物線状に曲げ、
    // twist は断面を長さ方向へ回す（度）。0 で形状を変えない。
    float bendAmount = 0.0f;
    float twistDegrees = 0.0f;
    // トーラ（主半径/副半径）とカプセル（半径）の形状パラメータ。
    float torusMajorRadius = 0.4f;
    float torusMinorRadius = 0.12f;
    float capsuleRadius = 0.35f;
    // Look At Line で固定するローカル軸。既定は長さ軸 = X。
    VfxBillboardAxis billboardAxis = VfxBillboardAxis::LocalX;
};

enum class VfxHistoryMode : uint32_t { None, Afterimage, Trail };

// 接続ターゲット: レイヤーの幾何を「原点 → 参照点」の区間に張る。
// ビームや軌跡が別のレイヤー／指定点へ繋がる、という発想をそのまま表す。
enum class VfxConnectTargetMode : uint8_t {
    None,
    // 同じエフェクト内の別レイヤー原点へ繋ぐ。
    LayerOrigin,
    // エフェクトのローカル座標で指定した点へ繋ぐ。
    LocalPosition,
};

struct VfxConnectTargetData {
    VfxConnectTargetMode mode = VfxConnectTargetMode::None;
    std::string targetLayerId{};
    MATH::Vec3 localPosition{0.0f, 0.0f, 0.0f};
    // 接続区間のどこへレイヤー原点を置くか。0.5 で中心の幾何が両端に届く。
    float pivot = 0.5f;
    // 区間の長さ倍率。0→1 をキーすれば根元から伸びる。
    float lengthScale = 1.0f;
    // 両端の内側への余白。
    float startOffset = 0.0f;
    float endOffset = 0.0f;
    bool orient = true;
    bool stretch = true;
};

// Mesh 出力: 手続き形状ではなく ModelAsset をレイヤーの幾何として使う
// 逃げ道。有効なレイヤーは粒子 pass ではなくモデル描画へ回す。
struct VfxMeshOutputData {
    bool enabled = false;
    AssetGuid meshAssetGuid{};
    bool castShadows = false;
    bool receiveShadow = false;
};

struct VfxHistoryData {
    VfxHistoryMode mode = VfxHistoryMode::None;
    float duration = 0.2f;
    uint32_t samples = 12u;
    float breakDistance = 2.5f;
    float tailWidth = 1.0f;
    float fadePower = 1.0f;
    MATH::Vec4 tailColor{1, 1, 1, 1};
};

// ソース・コンパイル済みデータ・編集操作が共有する範囲契約。
// ソースとコンパイル済みプログラムで同じ出力パラメータ規則を使う。
bool IsValidVfxSpecialOutputProfile(const VfxSpecialOutputData& output) noexcept;

bool IsValidVfxHistory(const VfxHistoryData& history) noexcept;

inline constexpr uint32_t kMaxVfxLayerCurveKeys = 256;
inline constexpr uint32_t kMaxVfxParameterTracks = 64;
struct VfxParameterTrackData {
    std::string id;
    std::string targetId;
    std::string propertyId;
    // 常にレイヤー時間。粒子ごとの寿命カーブとは別の意味を持つ。
    VfxCurveData curve;
};

struct VfxTrackMatteData {
    std::string sourceLayerId;
    bool luminance = false;
    bool inverted = false;
    bool sourceOnly = false;
};
struct VfxEmitterData {
    std::string id{};
    std::string name{};
    VfxLayerKind kind = VfxLayerKind::Particles;
    // レイヤー内容の回転・拡縮の基準点。原点は形状中心のため、
    // 刀身のように端から伸ばす場合はここをずらす。ローカル空間。
    MATH::Vec3 anchor{0.0f, 0.0f, 0.0f};
    VfxConnectTargetData connectTarget{};
    VfxMeshOutputData meshOutput{};
    VfxHistoryData history{};
    VfxTrackMatteData matte{};
    VfxAnimationTiming animationTiming{};
    std::string parentEmitterId{};
    uint32_t order = 0u;
    // Local emitter activation window. A zero activeDuration means
    // "until asset end" after startDelay, preserving old defaults.
    float startDelay = 0.0f;
    float activeDuration = 0.0f;
    VfxBasicModuleData basic{};
    VfxEmitterTransformModuleData emitterTransform{};
    VfxAttachmentData attachment{};
    VfxSpawnModuleData spawn{};
    VfxShapeModuleData shape{};
    VfxPositionModuleData position{};
    VfxRotationModuleData rotation{};
    VfxScaleModuleData scale{};
    VfxForcesModuleData forces{};
    VfxColorModuleData color{};
    VfxRenderCommonModuleData renderCommon{};
    VfxRenderShapeModuleData renderShape{};
    std::vector<VfxParameterTrackData> parameterTracks{};
    std::vector<MaterialEffectStep> effects{};
    std::vector<VfxSpecialOutputData> specialOutputs{};
    std::vector<VfxInputLinkData> inputLinks{};
};

struct VfxAssetData {
    uint32_t version = kVfxSourceVersion;
    std::string name = "New VFX";
    float durationSeconds = 1.0f;
    bool loop = false;
    std::vector<VfxExposedInputData> inputs{};
    std::vector<VfxEmitterData> emitters{};
};

std::string_view ToString(VfxValueType value) noexcept;
std::string_view ToString(VfxShapeType value) noexcept;
std::string_view ToString(VfxRenderShapeType value) noexcept;
std::string_view ToString(VfxShapeUvMode value) noexcept;
std::string_view ToString(VfxBlendMode value) noexcept;
std::string_view ToString(VfxPostProcessMode value) noexcept;
std::string_view ToString(VfxMaterialMode value) noexcept;
std::string_view ToString(VfxParticleDepthMode value) noexcept;
std::string_view ToString(VfxParticleCapacityMode value) noexcept;
std::string_view ToString(VfxBillboardMode value) noexcept;
std::string_view ToString(VfxBillboardAxis value) noexcept;
std::string_view ToString(VfxCurveInterpolation value) noexcept;
std::string_view ToString(VfxDistributionMode value) noexcept;
std::string_view ToString(VfxAnimatedValueMode value) noexcept;
std::string_view ToString(VfxAnimatedColorMode value) noexcept;
std::string_view ToString(VfxEasingFunction value) noexcept;
std::string_view ToString(VfxAttachmentMode value) noexcept;
std::string_view ToString(VfxFollowMode value) noexcept;
std::string_view ToString(VfxSpecialOutputType value) noexcept;
std::string_view ToString(VfxLayerKind value) noexcept;
std::string_view ToString(VfxHistoryMode value) noexcept;
std::string_view ToString(VfxConnectTargetMode value) noexcept;
// 名前から値への変換と選択肢一覧は Assets/Vfx/HIKARI_VfxEnumNames.h の表を使う。

VfxFloatDistributionData MakeVfxFloatConstant(float value) noexcept;
VfxVec3DistributionData MakeVfxVec3Constant(const MATH::Vec3& value) noexcept;
VfxColorDistributionData MakeVfxColorConstant(const MATH::Vec4& value) noexcept;
VfxCurveData MakeVfxConstantCurve(std::string_view idPrefix, float value);
VfxValue MakeDefaultVfxValue(VfxValueType type) noexcept;
VfxAssetData MakeDefaultVfxAssetData();

} // namespace HIKARI::VFX
