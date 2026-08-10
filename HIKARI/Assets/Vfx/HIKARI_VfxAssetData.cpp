#include "Assets/Vfx/HIKARI_VfxAssetData.h"

#include <initializer_list>
#include <utility>

namespace HIKARI::VFX {
    namespace {
        template<class Enum>
        bool ParseEnum(
            std::string_view text,
            std::initializer_list<std::pair<std::string_view, Enum>> values,
            Enum& outValue) noexcept {

            for (const auto& [name, value] : values) {
                if (text == name) {
                    outValue = value;
                    return true;
                }
            }
            return false;
        }
    }

    std::string_view ToString(VfxValueType value) noexcept {
        switch (value) {
        case VfxValueType::Bool: return "Bool";
        case VfxValueType::Int: return "Int";
        case VfxValueType::Float2: return "Float2";
        case VfxValueType::Float3: return "Float3";
        case VfxValueType::Float4: return "Float4";
        case VfxValueType::Color: return "Color";
        case VfxValueType::Float:
        default: return "Float";
        }
    }

    std::string_view ToString(VfxShapeType value) noexcept {
        switch (value) {
        case VfxShapeType::Sphere: return "Sphere";
        case VfxShapeType::Cone: return "Cone";
        case VfxShapeType::Point:
        default: return "Point";
        }
    }

    std::string_view ToString(VfxRenderShapeType) noexcept {
        return "Sprite";
    }

    std::string_view ToString(VfxBlendMode value) noexcept {
        return value == VfxBlendMode::Additive ? "Additive" : "Alpha";
    }

    std::string_view ToString(VfxBillboardMode value) noexcept {
        switch (value) {
        case VfxBillboardMode::RotatedBillboard:
            return "RotatedBillboard";
        case VfxBillboardMode::FixedYAxis:
            return "FixedYAxis";
        case VfxBillboardMode::DirectionalBillboard:
            return "DirectionalBillboard";
        case VfxBillboardMode::Fixed:
        default:
            return "Fixed";
        }
    }

    std::string_view ToString(VfxCurveInterpolation value) noexcept {
        switch (value) {
        case VfxCurveInterpolation::Smooth: return "Smooth";
        case VfxCurveInterpolation::Step: return "Step";
        case VfxCurveInterpolation::Linear:
        default: return "Linear";
        }
    }

    std::string_view ToString(VfxDistributionMode value) noexcept {
        return value == VfxDistributionMode::RandomRange
            ? "RandomRange"
            : "Constant";
    }

    std::string_view ToString(VfxAnimatedValueMode value) noexcept {
        switch (value) {
        case VfxAnimatedValueMode::Pva: return "Pva";
        case VfxAnimatedValueMode::Easing: return "Easing";
        case VfxAnimatedValueMode::Curve: return "Curve";
        case VfxAnimatedValueMode::Fixed:
        default: return "Fixed";
        }
    }

    std::string_view ToString(VfxAnimatedColorMode value) noexcept {
        switch (value) {
        case VfxAnimatedColorMode::RandomRange: return "RandomRange";
        case VfxAnimatedColorMode::Easing: return "Easing";
        case VfxAnimatedColorMode::Curve: return "Curve";
        case VfxAnimatedColorMode::Gradient: return "Gradient";
        case VfxAnimatedColorMode::Fixed:
        default: return "Fixed";
        }
    }

    std::string_view ToString(VfxEasingFunction value) noexcept {
        switch (value) {
        case VfxEasingFunction::EaseIn: return "EaseIn";
        case VfxEasingFunction::EaseOut: return "EaseOut";
        case VfxEasingFunction::EaseInOut: return "EaseInOut";
        case VfxEasingFunction::Linear:
        default: return "Linear";
        }
    }

    bool TryParseVfxValueType(
        std::string_view text,
        VfxValueType& outValue) noexcept {

        return ParseEnum(text, {
            { "Bool", VfxValueType::Bool },
            { "Int", VfxValueType::Int },
            { "Float", VfxValueType::Float },
            { "Float2", VfxValueType::Float2 },
            { "Float3", VfxValueType::Float3 },
            { "Float4", VfxValueType::Float4 },
            { "Color", VfxValueType::Color },
        }, outValue);
    }

    bool TryParseVfxShapeType(
        std::string_view text,
        VfxShapeType& outValue) noexcept {

        return ParseEnum(text, {
            { "Point", VfxShapeType::Point },
            { "Sphere", VfxShapeType::Sphere },
            { "Cone", VfxShapeType::Cone },
        }, outValue);
    }

    bool TryParseVfxRenderShapeType(
        std::string_view text,
        VfxRenderShapeType& outValue) noexcept {

        return ParseEnum(text, {
            { "Sprite", VfxRenderShapeType::Sprite },
        }, outValue);
    }

    bool TryParseVfxBlendMode(
        std::string_view text,
        VfxBlendMode& outValue) noexcept {

        return ParseEnum(text, {
            { "Alpha", VfxBlendMode::Alpha },
            { "Additive", VfxBlendMode::Additive },
        }, outValue);
    }

    bool TryParseVfxBillboardMode(
        std::string_view text,
        VfxBillboardMode& outValue) noexcept {

        return ParseEnum(text, {
            { "RotatedBillboard",
                VfxBillboardMode::RotatedBillboard },
            { "FixedYAxis", VfxBillboardMode::FixedYAxis },
            { "DirectionalBillboard",
                VfxBillboardMode::DirectionalBillboard },
            { "Fixed", VfxBillboardMode::Fixed },
            // VFX source v2 compatibility aliases. New saves always emit the
            // canonical Effekseer-aligned names above.
            { "CameraFacing",
                VfxBillboardMode::RotatedBillboard },
            { "Vertical", VfxBillboardMode::FixedYAxis },
        }, outValue);
    }

    bool TryParseVfxCurveInterpolation(
        std::string_view text,
        VfxCurveInterpolation& outValue) noexcept {

        return ParseEnum(text, {
            { "Linear", VfxCurveInterpolation::Linear },
            { "Smooth", VfxCurveInterpolation::Smooth },
            { "Step", VfxCurveInterpolation::Step },
        }, outValue);
    }

    bool TryParseVfxDistributionMode(
        std::string_view text,
        VfxDistributionMode& outValue) noexcept {

        return ParseEnum(text, {
            { "Constant", VfxDistributionMode::Constant },
            { "RandomRange", VfxDistributionMode::RandomRange },
        }, outValue);
    }

    bool TryParseVfxAnimatedValueMode(
        std::string_view text,
        VfxAnimatedValueMode& outValue) noexcept {

        return ParseEnum(text, {
            { "Fixed", VfxAnimatedValueMode::Fixed },
            { "Pva", VfxAnimatedValueMode::Pva },
            { "Easing", VfxAnimatedValueMode::Easing },
            { "Curve", VfxAnimatedValueMode::Curve },
        }, outValue);
    }

    bool TryParseVfxAnimatedColorMode(
        std::string_view text,
        VfxAnimatedColorMode& outValue) noexcept {

        return ParseEnum(text, {
            { "Fixed", VfxAnimatedColorMode::Fixed },
            { "RandomRange", VfxAnimatedColorMode::RandomRange },
            { "Easing", VfxAnimatedColorMode::Easing },
            { "Curve", VfxAnimatedColorMode::Curve },
            { "Gradient", VfxAnimatedColorMode::Gradient },
        }, outValue);
    }

    bool TryParseVfxEasingFunction(
        std::string_view text,
        VfxEasingFunction& outValue) noexcept {

        return ParseEnum(text, {
            { "Linear", VfxEasingFunction::Linear },
            { "EaseIn", VfxEasingFunction::EaseIn },
            { "EaseOut", VfxEasingFunction::EaseOut },
            { "EaseInOut", VfxEasingFunction::EaseInOut },
        }, outValue);
    }

    VfxFloatDistributionData MakeVfxFloatConstant(float value) noexcept {
        return { VfxDistributionMode::Constant, value, value, 0u };
    }

    VfxVec3DistributionData MakeVfxVec3Constant(
        const MATH::Vec3& value) noexcept {

        return { VfxDistributionMode::Constant, value, value, 0u };
    }

    VfxColorDistributionData MakeVfxColorConstant(
        const MATH::Vec4& value) noexcept {

        return { VfxDistributionMode::Constant, value, value, 0u };
    }

    VfxCurveData MakeVfxConstantCurve(
        std::string_view idPrefix,
        float value) {

        VfxCurveData curve{};
        curve.randomOffset = MakeVfxFloatConstant(0.0f);
        curve.keys = {
            { std::string(idPrefix) + ".start", 0.0f, value, 0.0f, 0.0f },
            { std::string(idPrefix) + ".end", 1.0f, value, 0.0f, 0.0f },
        };
        return curve;
    }

    VfxValue MakeDefaultVfxValue(VfxValueType type) noexcept {
        VfxValue value{};
        value.type = type;
        if (type == VfxValueType::Color) {
            value.numeric = { 1.0f, 1.0f, 1.0f, 1.0f };
        }
        return value;
    }

    VfxAssetData MakeDefaultVfxAssetData() {
        const MATH::Vec3 zero{ 0.0f, 0.0f, 0.0f };
        const MATH::Vec3 one{ 1.0f, 1.0f, 1.0f };
        const MATH::Vec4 white{ 1.0f, 1.0f, 1.0f, 1.0f };

        VfxAssetData asset{};
        VfxEmitterData emitter{};
        emitter.id = "emitter.main";
        emitter.name = "Main";
        emitter.basic.id = "module.basic";
        emitter.basic.lifetime = MakeVfxFloatConstant(1.0f);
        emitter.emitterTransform.id = "module.emitter_transform";
        emitter.spawn.id = "module.spawn";
        emitter.shape.id = "module.shape";
        emitter.shape.radius = MakeVfxFloatConstant(0.0f);
        emitter.position.id = "module.position";
        emitter.position.radialSpeed = MakeVfxFloatConstant(1.0f);
        emitter.position.value.fixed = MakeVfxVec3Constant(zero);
        emitter.rotation.id = "module.rotation";
        emitter.rotation.eulerDegrees.fixed = MakeVfxVec3Constant(zero);
        emitter.scale.id = "module.scale";
        emitter.scale.value.fixed = MakeVfxVec3Constant(one);
        emitter.forces.id = "module.forces";
        emitter.color.id = "module.color";
        emitter.color.value.mode = VfxAnimatedColorMode::Gradient;
        emitter.color.value.fixed = MakeVfxColorConstant(white);
        emitter.color.value.gradient.stops = {
            { "color.start", 0.0f, white },
            { "color.end", 1.0f, { 1.0f, 1.0f, 1.0f, 0.0f } },
        };
        emitter.renderCommon.id = "module.render_common";
        emitter.renderShape.id = "module.render_shape";
        asset.emitters.push_back(std::move(emitter));
        return asset;
    }

} // namespace HIKARI::VFX
