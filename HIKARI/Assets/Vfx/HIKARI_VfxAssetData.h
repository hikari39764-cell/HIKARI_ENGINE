#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"
#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI::VFX {

    inline constexpr uint32_t kVfxSourceVersion = 3u;

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
    };

    enum class VfxBlendMode : uint8_t {
        Alpha,
        Additive,
    };

    enum class VfxMaterialMode : uint8_t {
        StandardUnlit,
        CustomMaterial,
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

    struct VfxTransformData {
        MATH::Vec3 position{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 rotationEulerDegrees{ 0.0f, 0.0f, 0.0f };
        MATH::Vec3 scale{ 1.0f, 1.0f, 1.0f };
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
    };

    struct VfxEmitterTransformModuleData {
        std::string id{};
        VfxTransformData transform{};
    };

    struct VfxSpawnModuleData {
        std::string id{};
        float ratePerSecond = 10.0f;
        uint32_t burstCount = 0u;
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
        bool depthTest = true;
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

    struct VfxEmitterData {
        std::string id{};
        std::string name{};
        std::string parentEmitterId{};
        uint32_t order = 0u;
        VfxBasicModuleData basic{};
        VfxEmitterTransformModuleData emitterTransform{};
        VfxSpawnModuleData spawn{};
        VfxShapeModuleData shape{};
        VfxPositionModuleData position{};
        VfxRotationModuleData rotation{};
        VfxScaleModuleData scale{};
        VfxForcesModuleData forces{};
        VfxColorModuleData color{};
        VfxRenderCommonModuleData renderCommon{};
        VfxRenderShapeModuleData renderShape{};
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
    std::string_view ToString(VfxParticleCapacityMode value) noexcept;
    std::string_view ToString(VfxBillboardMode value) noexcept;
    std::string_view ToString(VfxCurveInterpolation value) noexcept;
    std::string_view ToString(VfxDistributionMode value) noexcept;
    std::string_view ToString(VfxAnimatedValueMode value) noexcept;
    std::string_view ToString(VfxAnimatedColorMode value) noexcept;
    std::string_view ToString(VfxEasingFunction value) noexcept;

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
