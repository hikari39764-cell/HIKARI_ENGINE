#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Vfx/HIKARI_VfxAnimationTiming.h"
#include "Assets/Material/Authoring/HIKARI_MaterialEffectStack.h"
#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::VFX {

    inline constexpr uint32_t kVfxSourceVersion = 9u;

    // 形状はレイヤー時間、粒子は個々の寿命で同じアニメーションを評価する。
    enum class VfxLayerKind : uint8_t { Particles, Shape, Group };

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
        MATH::Vec4 numeric{ 0.0f, 0.0f, 0.0f, 0.0f };
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
    };

    std::span<const std::string_view> GetVfxRenderShapeChoices() noexcept;
    std::span<const std::string_view> GetVfxMaterialModeChoices() noexcept;

    enum class VfxBlendMode : uint8_t {
        Alpha,
        Additive,
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
    };

    static_assert(
        static_cast<uint8_t>(
            VfxBillboardMode::RotatedBillboard) == 0u);
    static_assert(
        static_cast<uint8_t>(
            VfxBillboardMode::FixedYAxis) == 1u);
    static_assert(
        static_cast<uint8_t>(
            VfxBillboardMode::DirectionalBillboard) == 2u);
    static_assert(
        static_cast<uint8_t>(VfxBillboardMode::Fixed) == 3u);

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
    };

    // Keep these authoring limits identical to the private presentation
    // consumers: the screen post path supports four wave layers and the
    // afterimage history keeps three to five silhouettes.
    inline constexpr uint32_t kVfxScreenImpulseWaveCountMin = 1u;
    inline constexpr uint32_t kVfxScreenImpulseWaveCountMax = 4u;
    inline constexpr uint32_t kVfxAfterimageSampleCountMin = 3u;
    inline constexpr uint32_t kVfxAfterimageSampleCountMax = 5u;
    inline constexpr float kVfxWeaponDistortionMin = 0.0f;
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
        MATH::Vec3 position{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 rotationEulerDegrees{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 scale{ 1.0f, 1.0f, 1.0f };
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
        MATH::Vec4 bodyColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        MATH::Vec4 edgeColor{ 1.0f, 1.0f, 1.0f, 0.0f };
    };

    struct VfxScreenImpulseProfile {
        float durationSeconds = 0.18f;
        float maxRadius = 1.0f;
        float ringThickness = 0.15f;
        float distortion = 0.1f;
        float chromaticAberration = 0.0f;
        float flash = 0.0f;
        float intensity = 1.0f;
        MATH::Vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
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
        MATH::Vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
        float intensity = 1.0f;
    };

    struct VfxSpecialOutputData {
        std::string id{};
        VfxSpecialOutputType type =
            VfxSpecialOutputType::WeaponPathDistortion;
        bool enabled = true;
        VfxWeaponPathDistortionProfile weaponPathDistortion{};
        VfxScreenImpulseProfile screenImpulse{};
        VfxSkinnedAfterimageProfile skinnedAfterimage{};
    };

    template<class T>
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
        VfxCurveInterpolation interpolation =
            VfxCurveInterpolation::Linear;
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
        MATH::Vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    struct VfxColorGradientData {
        std::vector<VfxColorStopData> stops{};
    };

    template<class T>
    struct VfxPvaData {
        VfxDistributionData<T> initial{};
        VfxDistributionData<T> velocity{};
        VfxDistributionData<T> acceleration{};
    };

    template<class T>
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
        // Legacy single-burst field retained as a compatibility view. New
        // authored data is represented by burstTimeline. When the timeline
        // is non-empty, its keys are authoritative and this field is ignored.
        uint32_t burstCount = 0u;
        std::vector<VfxBurstKeyData> burstTimeline{};
        VfxParticleCapacityMode capacityMode =
            VfxParticleCapacityMode::Automatic;
        // In Automatic mode this remains the authored safety ceiling. The
        // compiler derives the smaller live allocation from spawn and TTL.
        uint32_t maximumParticles = 1024u;
    };

    struct VfxShapeModuleData {
        std::string id{};
        VfxShapeType type = VfxShapeType::Point;
        VfxFloatDistributionData radius{};
        float coneAngleDegrees = 30.0f;
        MATH::Vec3 boxHalfExtents{ 0.5f, 0.5f, 0.5f };
        float cylinderHeight = 1.0f;
        float circleInnerRadius = 0.0f;
        bool circleEmitOnEdge = false;
        float edgeLength = 1.0f;
    };

    struct VfxPositionModuleData {
        std::string id{};
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
        MATH::Vec3 gravity{ 0.0f, 0.0f, 0.0f };
        float drag = 0.0f;
        bool collisionPlaneEnabled = false;
        float collisionPlaneHeight = 0.0f;
        float restitution = 0.35f;
        float friction = 0.2f;
    };

    struct VfxColorModuleData {
        std::string id{};
        VfxAnimatedColorData value{};
    };

    struct VfxRenderCommonModuleData {
        std::string id{};
        VfxMaterialMode materialMode = VfxMaterialMode::StandardUnlit;
        AssetGuid colorTextureAssetGuid{};
        AssetGuid materialAssetGuid{};
        VfxBlendMode blendMode = VfxBlendMode::Alpha;
        VfxParticleDepthMode depthMode =
            VfxParticleDepthMode::WorldOccluded;
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
        VfxBillboardMode billboardMode =
            VfxBillboardMode::RotatedBillboard;
        uint32_t ribbonSegmentCount = 16u;
        float ribbonWidth = 0.1f;
        float ringOuterRadius = 1.0f;
        float ringInnerRadius = 0.5f;
        uint32_t ringSegmentCount = 32u;
        float ringStartAngleDegrees = 0.0f;
        float ringEndAngleDegrees = 360.0f;
    };

    enum class VfxHistoryMode : uint32_t { None, Afterimage, Trail };

    struct VfxHistoryData {
        VfxHistoryMode mode = VfxHistoryMode::None;
        float duration = 0.2f;
        uint32_t samples = 12u;
        float breakDistance = 2.5f;
    };

    // ソース・コンパイル済みデータ・編集操作が共有する範囲契約。
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

    struct VfxEmitterData {
        std::string id{};
        std::string name{};
        VfxLayerKind kind = VfxLayerKind::Particles;
        VfxHistoryData history{};
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
    std::string_view ToString(VfxBlendMode value) noexcept;
    std::string_view ToString(VfxMaterialMode value) noexcept;
    std::string_view ToString(VfxParticleDepthMode value) noexcept;
    std::string_view ToString(VfxParticleCapacityMode value) noexcept;
    std::string_view ToString(VfxBillboardMode value) noexcept;
    std::string_view ToString(VfxCurveInterpolation value) noexcept;
    std::string_view ToString(VfxDistributionMode value) noexcept;
    std::string_view ToString(VfxAnimatedValueMode value) noexcept;
    std::string_view ToString(VfxAnimatedColorMode value) noexcept;
    std::string_view ToString(VfxEasingFunction value) noexcept;
    std::string_view ToString(VfxAttachmentMode value) noexcept;
    std::string_view ToString(VfxFollowMode value) noexcept;
    std::string_view ToString(VfxSpecialOutputType value) noexcept;

    bool TryParseVfxValueType(
        std::string_view text,
        VfxValueType& outValue) noexcept;
    bool TryParseVfxShapeType(
        std::string_view text,
        VfxShapeType& outValue) noexcept;
    bool TryParseVfxRenderShapeType(
        std::string_view text,
        VfxRenderShapeType& outValue) noexcept;
    bool TryParseVfxBlendMode(
        std::string_view text,
        VfxBlendMode& outValue) noexcept;
    bool TryParseVfxMaterialMode(
        std::string_view text,
        VfxMaterialMode& outValue) noexcept;
    bool TryParseVfxParticleDepthMode(
        std::string_view text,
        VfxParticleDepthMode& outValue) noexcept;
    bool TryParseVfxParticleCapacityMode(
        std::string_view text,
        VfxParticleCapacityMode& outValue) noexcept;
    bool TryParseVfxBillboardMode(
        std::string_view text,
        VfxBillboardMode& outValue) noexcept;
    bool TryParseVfxCurveInterpolation(
        std::string_view text,
        VfxCurveInterpolation& outValue) noexcept;
    bool TryParseVfxDistributionMode(
        std::string_view text,
        VfxDistributionMode& outValue) noexcept;
    bool TryParseVfxAnimatedValueMode(
        std::string_view text,
        VfxAnimatedValueMode& outValue) noexcept;
    bool TryParseVfxAnimatedColorMode(
        std::string_view text,
        VfxAnimatedColorMode& outValue) noexcept;
    bool TryParseVfxEasingFunction(
        std::string_view text,
        VfxEasingFunction& outValue) noexcept;
    bool TryParseVfxAttachmentMode(
        std::string_view text,
        VfxAttachmentMode& outValue) noexcept;
    bool TryParseVfxFollowMode(
        std::string_view text,
        VfxFollowMode& outValue) noexcept;
    bool TryParseVfxSpecialOutputType(
        std::string_view text,
        VfxSpecialOutputType& outValue) noexcept;

    VfxFloatDistributionData MakeVfxFloatConstant(float value) noexcept;
    VfxVec3DistributionData MakeVfxVec3Constant(
        const MATH::Vec3& value) noexcept;
    VfxColorDistributionData MakeVfxColorConstant(
        const MATH::Vec4& value) noexcept;
    VfxCurveData MakeVfxConstantCurve(
        std::string_view idPrefix,
        float value);
    VfxValue MakeDefaultVfxValue(VfxValueType type) noexcept;
    VfxAssetData MakeDefaultVfxAssetData();

} // namespace HIKARI::VFX
