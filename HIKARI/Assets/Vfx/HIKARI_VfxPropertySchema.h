#pragma once

#include <cstdint>
#include <array>
#include <span>
#include <string_view>
#include <utility>

#include "Assets/Vfx/HIKARI_VfxAssetData.h"

namespace HIKARI::VFX {

    enum class VfxModuleKind : uint8_t {
        Basic,
        EmitterTransform,
        Spawn,
        Shape,
        Position,
        Rotation,
        Scale,
        Forces,
        Color,
        RenderCommon,
        RenderShape,
    };

    enum class VfxRuntimeBindingSemantic : uint8_t {
        EmitterEnabled,
        SpawnRateMultiplier,
        RadialSpeedMultiplier,
        ScaleMultiplier,
        GravityMultiplier,
        ColorMultiplier,
    };

    struct VfxPropertyRange {
        bool hasMinimum = false;
        bool hasMaximum = false;
        float minimum = 0.0f;
        float maximum = 0.0f;
    };

    struct VfxPropertySchema {
        VfxModuleKind moduleKind = VfxModuleKind::Basic;
        std::string_view propertyId{};
        std::string_view displayKey{};
        VfxValueType valueType = VfxValueType::Float;
        bool bindable = false;
        bool runtimeMutable = false;
        VfxRuntimeBindingSemantic runtimeSemantic =
            VfxRuntimeBindingSemantic::EmitterEnabled;
        VfxPropertyRange range{};
        std::string_view units{};
    };

    inline bool IsVfxInputTypeCompatible(
        VfxValueType inputType,
        VfxValueType propertyType) noexcept {

        if (inputType == propertyType) {
            return true;
        }
        if (propertyType == VfxValueType::Float) {
            return inputType == VfxValueType::Int;
        }
        if (propertyType == VfxValueType::Color) {
            return inputType == VfxValueType::Float4;
        }
        return false;
    }

    inline bool IsVfxInputTypeCompatible(
        VfxValueType inputType,
        const VfxPropertySchema& property) noexcept {

        return IsVfxInputTypeCompatible(inputType, property.valueType);
    }

    inline constexpr std::array<VfxPropertySchema, 6u>
        kVfxPropertySchemas{
            VfxPropertySchema{
                VfxModuleKind::Basic,
                "enabled",
                "vfx.property.enabled",
                VfxValueType::Bool,
                true,
                true,
                VfxRuntimeBindingSemantic::EmitterEnabled,
                {},
                {},
            },
            VfxPropertySchema{
                VfxModuleKind::Spawn,
                "rateMultiplier",
                "vfx.property.spawn_rate_multiplier",
                VfxValueType::Float,
                true,
                true,
                VfxRuntimeBindingSemantic::SpawnRateMultiplier,
                { true, true, 0.0f, 1024.0f },
                "multiplier",
            },
            VfxPropertySchema{
                VfxModuleKind::Position,
                "radialSpeedMultiplier",
                "vfx.property.radial_speed_multiplier",
                VfxValueType::Float,
                true,
                true,
                VfxRuntimeBindingSemantic::RadialSpeedMultiplier,
                { true, true, 0.0f, 1024.0f },
                "multiplier",
            },
            VfxPropertySchema{
                VfxModuleKind::Scale,
                "valueMultiplier",
                "vfx.property.scale_multiplier",
                VfxValueType::Float,
                true,
                true,
                VfxRuntimeBindingSemantic::ScaleMultiplier,
                { true, true, 0.0f, 1024.0f },
                "multiplier",
            },
            VfxPropertySchema{
                VfxModuleKind::Forces,
                "gravityMultiplier",
                "vfx.property.gravity_multiplier",
                VfxValueType::Float,
                true,
                true,
                VfxRuntimeBindingSemantic::GravityMultiplier,
                { true, true, -1024.0f, 1024.0f },
                "multiplier",
            },
            VfxPropertySchema{
                VfxModuleKind::Color,
                "valueMultiplier",
                "vfx.property.color_multiplier",
                VfxValueType::Color,
                true,
                true,
                VfxRuntimeBindingSemantic::ColorMultiplier,
                { true, true, 0.0f, 64.0f },
                "multiplier",
            },
        };

    inline std::string_view ToString(VfxModuleKind value) noexcept {
        switch (value) {
        case VfxModuleKind::EmitterTransform: return "EmitterTransform";
        case VfxModuleKind::Spawn: return "Spawn";
        case VfxModuleKind::Shape: return "Shape";
        case VfxModuleKind::Position: return "Position";
        case VfxModuleKind::Rotation: return "Rotation";
        case VfxModuleKind::Scale: return "Scale";
        case VfxModuleKind::Forces: return "Forces";
        case VfxModuleKind::Color: return "Color";
        case VfxModuleKind::RenderCommon: return "RenderCommon";
        case VfxModuleKind::RenderShape: return "RenderShape";
        case VfxModuleKind::Basic:
        default: return "Basic";
        }
    }

    inline std::string_view ToString(
        VfxRuntimeBindingSemantic value) noexcept {

        switch (value) {
        case VfxRuntimeBindingSemantic::SpawnRateMultiplier:
            return "SpawnRateMultiplier";
        case VfxRuntimeBindingSemantic::RadialSpeedMultiplier:
            return "RadialSpeedMultiplier";
        case VfxRuntimeBindingSemantic::ScaleMultiplier:
            return "ScaleMultiplier";
        case VfxRuntimeBindingSemantic::GravityMultiplier:
            return "GravityMultiplier";
        case VfxRuntimeBindingSemantic::ColorMultiplier:
            return "ColorMultiplier";
        case VfxRuntimeBindingSemantic::EmitterEnabled:
        default:
            return "EmitterEnabled";
        }
    }

    inline std::span<const VfxPropertySchema>
        GetVfxPropertySchemas() noexcept {

        return kVfxPropertySchemas;
    }

    inline bool TryResolveVfxModuleKind(
        const VfxEmitterData& emitter,
        std::string_view moduleId,
        VfxModuleKind& outKind) noexcept {

        const std::array<std::pair<std::string_view, VfxModuleKind>, 11u>
            modules{
                std::pair{ std::string_view(emitter.basic.id),
                    VfxModuleKind::Basic },
                std::pair{ std::string_view(emitter.emitterTransform.id),
                    VfxModuleKind::EmitterTransform },
                std::pair{ std::string_view(emitter.spawn.id),
                    VfxModuleKind::Spawn },
                std::pair{ std::string_view(emitter.shape.id),
                    VfxModuleKind::Shape },
                std::pair{ std::string_view(emitter.position.id),
                    VfxModuleKind::Position },
                std::pair{ std::string_view(emitter.rotation.id),
                    VfxModuleKind::Rotation },
                std::pair{ std::string_view(emitter.scale.id),
                    VfxModuleKind::Scale },
                std::pair{ std::string_view(emitter.forces.id),
                    VfxModuleKind::Forces },
                std::pair{ std::string_view(emitter.color.id),
                    VfxModuleKind::Color },
                std::pair{ std::string_view(emitter.renderCommon.id),
                    VfxModuleKind::RenderCommon },
                std::pair{ std::string_view(emitter.renderShape.id),
                    VfxModuleKind::RenderShape },
            };
        for (const auto& [candidate, kind] : modules) {
            if (!candidate.empty() && candidate == moduleId) {
                outKind = kind;
                return true;
            }
        }
        return false;
    }

    inline const VfxPropertySchema* FindVfxPropertySchema(
        VfxModuleKind moduleKind,
        std::string_view propertyId) noexcept {

        for (const VfxPropertySchema& schema : kVfxPropertySchemas) {
            if (schema.moduleKind == moduleKind &&
                schema.propertyId == propertyId) {
                return &schema;
            }
        }
        return nullptr;
    }

    inline const VfxPropertySchema* ResolveVfxPropertySchema(
        const VfxEmitterData& emitter,
        const VfxPropertyAddress& address) noexcept {

        if (address.emitterId != emitter.id) return nullptr;
        VfxModuleKind kind{};
        return TryResolveVfxModuleKind(emitter, address.moduleId, kind)
            ? FindVfxPropertySchema(kind, address.propertyId)
            : nullptr;
    }

} // namespace HIKARI::VFX
