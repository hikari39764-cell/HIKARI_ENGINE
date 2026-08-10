#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceDomain.h"
#include "Render3D/Vfx/HIKARI_ParticleRenderInput.h"

namespace HIKARI::EDITOR {
    namespace {

        float FindAvailableCurveTime(std::vector<float> times) {
            if (times.empty()) {
                return 0.5f;
            }
            std::sort(times.begin(), times.end());
            float bestStart = 0.0f;
            float bestEnd = times.front();
            for (size_t index = 1u; index < times.size(); ++index) {
                if (times[index] - times[index - 1u] >
                    bestEnd - bestStart) {
                    bestStart = times[index - 1u];
                    bestEnd = times[index];
                }
            }
            if (1.0f - times.back() > bestEnd - bestStart) {
                bestStart = times.back();
                bestEnd = 1.0f;
            }
            return (bestStart + bestEnd) * 0.5f;
        }

        float DefaultCurveValue(
            const VFX::VfxEmitterData& emitter,
            const VfxCurveAddress& address) noexcept {

            if (address.moduleId == emitter.scale.id ||
                address.moduleId == emitter.color.id) {
                return 1.0f;
            }
            return 0.0f;
        }

    } // namespace

    bool VfxWorkspaceController::AddCurveKey(
        std::string_view emitterId,
        std::string_view moduleId,
        std::string_view propertyId,
        std::string_view channel,
        std::string& outId,
        std::string& outMessage) {

        const VFX::VfxEmitterData* source = FindEmitter(emitterId);
        if (source == nullptr) {
            outMessage = "VFX emitter was not found";
            return false;
        }
        const VfxCurveAddress address{
            std::string(emitterId), std::string(moduleId),
            std::string(propertyId), std::string(channel)
        };
        const VFX::VfxCurveData* sourceCurve =
            ResolveVfxCurve(*source, address);
        if (sourceCurve == nullptr) {
            outMessage = "VFX curve property address was not found";
            return false;
        }
        if (sourceCurve->keys.size() >=
            RENDER3D::PARTICLES::kMaxParticleCurveKeys) {
            outMessage = "GPU VFX curve key limit reached";
            return false;
        }
        std::vector<float> times{};
        for (const VFX::VfxCurveKeyData& key : sourceCurve->keys) {
            times.push_back(key.time);
        }
        const std::string id = AllocateStableId("curve.key");
        VFX::VfxAssetData next = document_.Asset();
        VFX::VfxEmitterData* emitter =
            FindEmitterMutable(next, emitterId);
        VFX::VfxCurveData* curve = ResolveVfxCurve(*emitter, address);
        curve->keys.push_back({
            id,
            FindAvailableCurveTime(std::move(times)),
            DefaultCurveValue(*source, address),
            0.0f,
            0.0f,
        });
        std::sort(
            curve->keys.begin(), curve->keys.end(),
            [](const auto& left, const auto& right) {
                return left.time != right.time
                    ? left.time < right.time
                    : left.id < right.id;
            });
        if (!ApplyAsset(std::move(next), outMessage)) {
            return false;
        }
        outId = id;
        selectedCurveKeyId_ = id;
        Select({ VfxWorkspaceSelectionKind::CurveKey, id,
            std::string(emitterId), std::string(moduleId),
            std::string(propertyId), std::string(channel) });
        outMessage = "VFX curve key created";
        statusMessage_ = outMessage;
        return true;
    }

    bool VfxWorkspaceController::RemoveCurveKey(
        std::string_view emitterId,
        std::string_view moduleId,
        std::string_view propertyId,
        std::string_view channel,
        std::string_view keyId,
        std::string& outMessage) {

        const VFX::VfxEmitterData* source = FindEmitter(emitterId);
        const VfxCurveAddress address{
            std::string(emitterId), std::string(moduleId),
            std::string(propertyId), std::string(channel)
        };
        const VFX::VfxCurveData* sourceCurve = source != nullptr
            ? ResolveVfxCurve(*source, address)
            : nullptr;
        if (sourceCurve == nullptr) {
            outMessage = "VFX curve property address was not found";
            return false;
        }
        const auto found = std::find_if(
            sourceCurve->keys.begin(), sourceCurve->keys.end(),
            [keyId](const auto& key) { return key.id == keyId; });
        if (found == sourceCurve->keys.end()) {
            outMessage = "VFX curve key was not found in the addressed curve";
            return false;
        }
        if (sourceCurve->keys.size() <= 1u) {
            outMessage = "A VFX curve must keep at least one key";
            return false;
        }
        VFX::VfxAssetData next = document_.Asset();
        VFX::VfxEmitterData* emitter =
            FindEmitterMutable(next, emitterId);
        VFX::VfxCurveData* curve = ResolveVfxCurve(*emitter, address);
        std::erase_if(
            curve->keys,
            [keyId](const auto& key) { return key.id == keyId; });
        if (!ApplyAsset(std::move(next), outMessage)) {
            return false;
        }
        selectedCurveKeyId_.clear();
        Select({ VfxWorkspaceSelectionKind::Emitter,
            std::string(emitterId), std::string(emitterId) });
        outMessage = "VFX curve key removed";
        statusMessage_ = outMessage;
        return true;
    }

    bool VfxWorkspaceController::AddColorStop(
        std::string_view emitterId,
        std::string_view moduleId,
        std::string_view propertyId,
        std::string& outId,
        std::string& outMessage) {

        const VFX::VfxEmitterData* source = FindEmitter(emitterId);
        const VFX::VfxColorGradientData* sourceGradient = source != nullptr
            ? ResolveVfxGradient(*source, moduleId, propertyId)
            : nullptr;
        if (sourceGradient == nullptr) {
            outMessage = "VFX gradient property address was not found";
            return false;
        }
        if (sourceGradient->stops.size() >=
            RENDER3D::PARTICLES::kMaxParticleColorStops) {
            outMessage = "GPU VFX gradient stop limit reached";
            return false;
        }
        std::vector<float> times{};
        for (const VFX::VfxColorStopData& stop : sourceGradient->stops) {
            times.push_back(stop.time);
        }
        const std::string id = AllocateStableId("gradient.stop");
        VFX::VfxAssetData next = document_.Asset();
        VFX::VfxEmitterData* emitter =
            FindEmitterMutable(next, emitterId);
        VFX::VfxColorGradientData* gradient =
            ResolveVfxGradient(*emitter, moduleId, propertyId);
        gradient->stops.push_back({
            id,
            FindAvailableCurveTime(std::move(times)),
            { 1.0f, 1.0f, 1.0f, 1.0f },
        });
        std::sort(
            gradient->stops.begin(), gradient->stops.end(),
            [](const auto& left, const auto& right) {
                return left.time != right.time
                    ? left.time < right.time
                    : left.id < right.id;
            });
        if (!ApplyAsset(std::move(next), outMessage)) {
            return false;
        }
        outId = id;
        selectedColorStopId_ = id;
        Select({ VfxWorkspaceSelectionKind::ColorStop, id,
            std::string(emitterId), std::string(moduleId),
            std::string(propertyId) });
        outMessage = "VFX color stop created";
        statusMessage_ = outMessage;
        return true;
    }

    bool VfxWorkspaceController::RemoveColorStop(
        std::string_view emitterId,
        std::string_view moduleId,
        std::string_view propertyId,
        std::string_view stopId,
        std::string& outMessage) {

        const VFX::VfxEmitterData* source = FindEmitter(emitterId);
        const VFX::VfxColorGradientData* sourceGradient = source != nullptr
            ? ResolveVfxGradient(*source, moduleId, propertyId)
            : nullptr;
        if (sourceGradient == nullptr) {
            outMessage = "VFX gradient property address was not found";
            return false;
        }
        const auto found = std::find_if(
            sourceGradient->stops.begin(), sourceGradient->stops.end(),
            [stopId](const auto& stop) { return stop.id == stopId; });
        if (found == sourceGradient->stops.end()) {
            outMessage =
                "VFX color stop was not found in the addressed gradient";
            return false;
        }
        if (sourceGradient->stops.size() <= 1u) {
            outMessage = "A VFX gradient must keep at least one stop";
            return false;
        }
        VFX::VfxAssetData next = document_.Asset();
        VFX::VfxEmitterData* emitter =
            FindEmitterMutable(next, emitterId);
        VFX::VfxColorGradientData* gradient =
            ResolveVfxGradient(*emitter, moduleId, propertyId);
        std::erase_if(
            gradient->stops,
            [stopId](const auto& stop) { return stop.id == stopId; });
        if (!ApplyAsset(std::move(next), outMessage)) {
            return false;
        }
        selectedColorStopId_.clear();
        Select({ VfxWorkspaceSelectionKind::Emitter,
            std::string(emitterId), std::string(emitterId) });
        outMessage = "VFX color stop removed";
        statusMessage_ = outMessage;
        return true;
    }

} // namespace HIKARI::EDITOR
