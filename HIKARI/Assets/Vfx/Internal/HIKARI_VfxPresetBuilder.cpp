#include "Assets/Vfx/Internal/HIKARI_VfxPresetBuilder.h"

#include <utility>

namespace HIKARI::VFX::PRESETS::INTERNAL {
    namespace {
        void QualifyId(std::string& id, std::string_view suffix) {
            if (!id.empty()) {
                id += ".";
                id += suffix;
            }
        }

        void UniquifyEmitterIds(
            VfxEmitterData& emitter,
            std::string_view idSuffix) {

            const std::string oldEmitterId = emitter.id;
            QualifyId(emitter.id, idSuffix);
            QualifyId(emitter.basic.id, idSuffix);
            QualifyId(emitter.emitterTransform.id, idSuffix);
            QualifyId(emitter.attachment.id, idSuffix);
            QualifyId(emitter.spawn.id, idSuffix);
            QualifyId(emitter.shape.id, idSuffix);
            QualifyId(emitter.position.id, idSuffix);
            QualifyId(emitter.rotation.id, idSuffix);
            QualifyId(emitter.scale.id, idSuffix);
            QualifyId(emitter.forces.id, idSuffix);
            QualifyId(emitter.color.id, idSuffix);
            QualifyId(emitter.renderCommon.id, idSuffix);
            QualifyId(emitter.renderShape.id, idSuffix);
            if (!emitter.parentEmitterId.empty()) {
                QualifyId(emitter.parentEmitterId, idSuffix);
            }
            for (VfxBurstKeyData& key : emitter.spawn.burstTimeline) {
                QualifyId(key.id, idSuffix);
            }
            for (VfxSpecialOutputData& output : emitter.specialOutputs) {
                QualifyId(output.id, idSuffix);
            }
            const auto qualifyCurve = [idSuffix](VfxCurveData& curve) {
                for (VfxCurveKeyData& key : curve.keys) {
                    QualifyId(key.id, idSuffix);
                }
            };
            const auto qualifyVec3Curve =
                [&qualifyCurve](VfxVec3CurveData& curve) {
                    qualifyCurve(curve.x);
                    qualifyCurve(curve.y);
                    qualifyCurve(curve.z);
                };
            qualifyVec3Curve(emitter.position.value.curve);
            qualifyVec3Curve(emitter.rotation.eulerDegrees.curve);
            qualifyVec3Curve(emitter.scale.value.curve);
            qualifyCurve(emitter.color.value.curve.r);
            qualifyCurve(emitter.color.value.curve.g);
            qualifyCurve(emitter.color.value.curve.b);
            qualifyCurve(emitter.color.value.curve.a);
            for (VfxColorStopData& stop : emitter.color.value.gradient.stops) {
                QualifyId(stop.id, idSuffix);
            }
            for (VfxInputLinkData& link : emitter.inputLinks) {
                QualifyId(link.id, idSuffix);
                if (link.target.emitterId == oldEmitterId) {
                    link.target.emitterId = emitter.id;
                }
            }
        }
    }

    VfxEmitterData MakePresetEmitterBase(
        std::string idSuffix,
        std::string name) {

        VfxEmitterData emitter =
            MakeDefaultVfxAssetData().emitters.front();
        UniquifyEmitterIds(emitter, idSuffix);
        emitter.name = std::move(name);
        return emitter;
    }

    VfxAssetData MakePresetAssetFromEmitters(
        VfxAssetData asset,
        std::vector<VfxEmitterData> emitters) {

        if (!emitters.empty()) {
            asset.emitters = std::move(emitters);
            uint32_t order = 0u;
            for (VfxEmitterData& emitter : asset.emitters) {
                emitter.order = order++;
            }
        }
        return asset;
    }

    void SetPresetColorStops(
        VfxEmitterData& emitter,
        const MATH::Vec4& start,
        const MATH::Vec4& end) {

        emitter.color.value.mode = VfxAnimatedColorMode::Gradient;
        const std::string prefix = emitter.id + ".color";
        emitter.color.value.gradient.stops = {
            { prefix + ".start", 0.0f, start },
            { prefix + ".end", 1.0f, end },
        };
    }

} // namespace HIKARI::VFX::PRESETS::INTERNAL
