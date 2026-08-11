#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceDomain.h"

namespace HIKARI::EDITOR {
    namespace {

        template<class Emitter>
        auto* ResolveCurveImpl(
            Emitter& emitter,
            const VfxCurveAddress& address) noexcept {

            if (address.emitterId != emitter.id) {
                return static_cast<decltype(&emitter.position.value.curve.x)>(
                    nullptr);
            }
            if (address.moduleId == emitter.position.id &&
                address.propertyId == "value") {
                if (address.channel == "x") return &emitter.position.value.curve.x;
                if (address.channel == "y") return &emitter.position.value.curve.y;
                if (address.channel == "z") return &emitter.position.value.curve.z;
            }
            if (address.moduleId == emitter.rotation.id &&
                address.propertyId == "eulerDegrees") {
                if (address.channel == "x") return &emitter.rotation.eulerDegrees.curve.x;
                if (address.channel == "y") return &emitter.rotation.eulerDegrees.curve.y;
                if (address.channel == "z") return &emitter.rotation.eulerDegrees.curve.z;
            }
            if (address.moduleId == emitter.scale.id &&
                address.propertyId == "value") {
                if (address.channel == "x") return &emitter.scale.value.curve.x;
                if (address.channel == "y") return &emitter.scale.value.curve.y;
                if (address.channel == "z") return &emitter.scale.value.curve.z;
            }
            if (address.moduleId == emitter.color.id &&
                address.propertyId == "value") {
                if (address.channel == "r") return &emitter.color.value.curve.r;
                if (address.channel == "g") return &emitter.color.value.curve.g;
                if (address.channel == "b") return &emitter.color.value.curve.b;
                if (address.channel == "a") return &emitter.color.value.curve.a;
            }
            return static_cast<decltype(&emitter.position.value.curve.x)>(
                nullptr);
        }

        template<class Emitter>
        auto* ResolveGradientImpl(
            Emitter& emitter,
            std::string_view moduleId,
            std::string_view propertyId) noexcept {

            return moduleId == emitter.color.id && propertyId == "value"
                ? &emitter.color.value.gradient
                : static_cast<decltype(&emitter.color.value.gradient)>(
                    nullptr);
        }

    } // namespace

    std::string_view GetVfxModuleId(
        const VFX::VfxEmitterData& emitter,
        VFX::VfxModuleKind kind) noexcept {

        switch (kind) {
        case VFX::VfxModuleKind::Basic: return emitter.basic.id;
        case VFX::VfxModuleKind::EmitterTransform:
            return emitter.emitterTransform.id;
        case VFX::VfxModuleKind::Spawn: return emitter.spawn.id;
        case VFX::VfxModuleKind::Shape: return emitter.shape.id;
        case VFX::VfxModuleKind::Position: return emitter.position.id;
        case VFX::VfxModuleKind::Rotation: return emitter.rotation.id;
        case VFX::VfxModuleKind::Scale: return emitter.scale.id;
        case VFX::VfxModuleKind::Forces: return emitter.forces.id;
        case VFX::VfxModuleKind::Color: return emitter.color.id;
        case VFX::VfxModuleKind::RenderCommon:
            return emitter.renderCommon.id;
        case VFX::VfxModuleKind::RenderShape:
            return emitter.renderShape.id;
        }
        return {};
    }

    const VFX::VfxCurveData* ResolveVfxCurve(
        const VFX::VfxEmitterData& emitter,
        const VfxCurveAddress& address) noexcept {

        return ResolveCurveImpl(emitter, address);
    }

    VFX::VfxCurveData* ResolveVfxCurve(
        VFX::VfxEmitterData& emitter,
        const VfxCurveAddress& address) noexcept {

        return ResolveCurveImpl(emitter, address);
    }

    const VFX::VfxColorGradientData* ResolveVfxGradient(
        const VFX::VfxEmitterData& emitter,
        std::string_view moduleId,
        std::string_view propertyId) noexcept {

        return ResolveGradientImpl(emitter, moduleId, propertyId);
    }

    VFX::VfxColorGradientData* ResolveVfxGradient(
        VFX::VfxEmitterData& emitter,
        std::string_view moduleId,
        std::string_view propertyId) noexcept {

        return ResolveGradientImpl(emitter, moduleId, propertyId);
    }

} // namespace HIKARI::EDITOR
