#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceController.h"

#include <algorithm>
#include <array>

#include "Assets/HIKARI_AssetGuid.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxWorkspaceDomain.h"

namespace HIKARI::EDITOR {
    namespace {

        std::array<VfxCurveAddress, 13u> CurveAddresses(
            const VFX::VfxEmitterData& emitter) {

            return {
                VfxCurveAddress{ emitter.id, emitter.position.id,
                    "value", "x" },
                VfxCurveAddress{ emitter.id, emitter.position.id,
                    "value", "y" },
                VfxCurveAddress{ emitter.id, emitter.position.id,
                    "value", "z" },
                VfxCurveAddress{ emitter.id, emitter.rotation.id,
                    "eulerDegrees", "x" },
                VfxCurveAddress{ emitter.id, emitter.rotation.id,
                    "eulerDegrees", "y" },
                VfxCurveAddress{ emitter.id, emitter.rotation.id,
                    "eulerDegrees", "z" },
                VfxCurveAddress{ emitter.id, emitter.scale.id,
                    "value", "x" },
                VfxCurveAddress{ emitter.id, emitter.scale.id,
                    "value", "y" },
                VfxCurveAddress{ emitter.id, emitter.scale.id,
                    "value", "z" },
                VfxCurveAddress{ emitter.id, emitter.color.id,
                    "value", "r" },
                VfxCurveAddress{ emitter.id, emitter.color.id,
                    "value", "g" },
                VfxCurveAddress{ emitter.id, emitter.color.id,
                    "value", "b" },
                VfxCurveAddress{ emitter.id, emitter.color.id,
                    "value", "a" },
            };
        }

    } // namespace

    const VFX::VfxEmitterData* VfxWorkspaceController::FindEmitter(
        std::string_view emitterId) const noexcept {

        if (!document_.IsOpen()) {
            return nullptr;
        }
        const auto found = std::find_if(
            document_.Asset().emitters.begin(),
            document_.Asset().emitters.end(),
            [emitterId](const VFX::VfxEmitterData& emitter) {
                return emitter.id == emitterId;
            });
        return found != document_.Asset().emitters.end()
            ? &*found
            : nullptr;
    }

    const VFX::VfxExposedInputData* VfxWorkspaceController::FindInput(
        std::string_view inputId) const noexcept {

        if (!document_.IsOpen()) {
            return nullptr;
        }
        const auto found = std::find_if(
            document_.Asset().inputs.begin(),
            document_.Asset().inputs.end(),
            [inputId](const VFX::VfxExposedInputData& input) {
                return input.id == inputId;
            });
        return found != document_.Asset().inputs.end()
            ? &*found
            : nullptr;
    }

    const VFX::VfxInputLinkData*
        VfxWorkspaceController::FindInputLink(
            std::string_view linkId,
            const VFX::VfxEmitterData** outEmitter) const noexcept {

        if (outEmitter != nullptr) {
            *outEmitter = nullptr;
        }
        if (!document_.IsOpen()) {
            return nullptr;
        }
        for (const VFX::VfxEmitterData& emitter :
             document_.Asset().emitters) {
            const auto found = std::find_if(
                emitter.inputLinks.begin(), emitter.inputLinks.end(),
                [linkId](const VFX::VfxInputLinkData& link) {
                    return link.id == linkId;
                });
            if (found != emitter.inputLinks.end()) {
                if (outEmitter != nullptr) {
                    *outEmitter = &emitter;
                }
                return &*found;
            }
        }
        return nullptr;
    }

    const VFX::VfxCurveKeyData*
        VfxWorkspaceController::FindCurveKey(
            std::string_view keyId,
            const VFX::VfxEmitterData** outEmitter,
            VfxCurveAddress* outAddress) const noexcept {

        if (outEmitter != nullptr) {
            *outEmitter = nullptr;
        }
        if (outAddress != nullptr) {
            *outAddress = {};
        }
        if (!document_.IsOpen()) {
            return nullptr;
        }
        for (const VFX::VfxEmitterData& emitter :
             document_.Asset().emitters) {
            for (const VfxCurveAddress& address :
                 CurveAddresses(emitter)) {
                const VFX::VfxCurveData* curve =
                    ResolveVfxCurve(emitter, address);
                if (curve == nullptr) {
                    continue;
                }
                const auto found = std::find_if(
                    curve->keys.begin(), curve->keys.end(),
                    [keyId](const VFX::VfxCurveKeyData& key) {
                        return key.id == keyId;
                    });
                if (found == curve->keys.end()) {
                    continue;
                }
                if (outEmitter != nullptr) {
                    *outEmitter = &emitter;
                }
                if (outAddress != nullptr) {
                    *outAddress = address;
                }
                return &*found;
            }
        }
        return nullptr;
    }

    const VFX::VfxColorStopData*
        VfxWorkspaceController::FindColorStop(
            std::string_view stopId,
            const VFX::VfxEmitterData** outEmitter,
            std::string* outModuleId,
            std::string* outPropertyId) const noexcept {

        if (outEmitter != nullptr) {
            *outEmitter = nullptr;
        }
        if (outModuleId != nullptr) {
            outModuleId->clear();
        }
        if (outPropertyId != nullptr) {
            outPropertyId->clear();
        }
        if (!document_.IsOpen()) {
            return nullptr;
        }
        for (const VFX::VfxEmitterData& emitter :
             document_.Asset().emitters) {
            const VFX::VfxColorGradientData* gradient =
                ResolveVfxGradient(emitter, emitter.color.id, "value");
            if (gradient == nullptr) {
                continue;
            }
            const auto found = std::find_if(
                gradient->stops.begin(), gradient->stops.end(),
                [stopId](const VFX::VfxColorStopData& stop) {
                    return stop.id == stopId;
                });
            if (found == gradient->stops.end()) {
                continue;
            }
            if (outEmitter != nullptr) {
                *outEmitter = &emitter;
            }
            if (outModuleId != nullptr) {
                *outModuleId = emitter.color.id;
            }
            if (outPropertyId != nullptr) {
                *outPropertyId = "value";
            }
            return &*found;
        }
        return nullptr;
    }

    bool VfxWorkspaceController::ApplyAsset(
        VFX::VfxAssetData asset,
        std::string& outMessage,
        uint64_t mergeGroup) {

        if (!document_.ApplyAsset(
                std::move(asset), outMessage, mergeGroup)) {
            statusMessage_ = outMessage;
            return false;
        }
        NormalizeSelection();
        OnDocumentChanged();
        statusMessage_ = outMessage;
        return true;
    }

    void VfxWorkspaceController::OnDocumentChanged() {
        diagnostics_ = document_.IsOpen()
            ? VFX::ValidateVfxAsset(document_.Asset())
            : std::vector<VFX::VfxDiagnostic>{};
        NotifyEmitterStructureChanged();
        previewProgramRevision_ = 0u;
        if (previewPlaying_) {
            previewRestartPending_ = true;
        }
    }

    void VfxWorkspaceController::NormalizeSelection() {
        if (!document_.IsOpen()) {
            selection_ = {};
            return;
        }
        const bool valid = [&]() {
            switch (selection_.kind) {
            case VfxWorkspaceSelectionKind::Asset:
            case VfxWorkspaceSelectionKind::Inputs:
                return true;
            case VfxWorkspaceSelectionKind::Input:
                return FindInput(selection_.targetId) != nullptr;
            case VfxWorkspaceSelectionKind::Emitter:
            case VfxWorkspaceSelectionKind::Bindings:
                return FindEmitter(selection_.emitterId) != nullptr;
            case VfxWorkspaceSelectionKind::Link:
                return FindInputLink(selection_.targetId) != nullptr;
            case VfxWorkspaceSelectionKind::CurveKey:
                return FindCurveKey(selection_.targetId) != nullptr;
            case VfxWorkspaceSelectionKind::ColorStop:
                return FindColorStop(selection_.targetId) != nullptr;
            }
            return false;
        }();
        if (!valid) {
            selection_ = {};
            return;
        }
        if (selection_.kind == VfxWorkspaceSelectionKind::Emitter ||
            selection_.kind == VfxWorkspaceSelectionKind::Bindings) {
            if (const VFX::VfxEmitterData* emitter =
                    FindEmitter(selection_.emitterId)) {
                selection_.targetId = emitter->id;
            }
        } else if (selection_.kind ==
                   VfxWorkspaceSelectionKind::CurveKey) {
            VfxCurveAddress address{};
            if (FindCurveKey(
                    selection_.targetId, nullptr, &address) != nullptr) {
                selection_.emitterId = address.emitterId;
                selection_.moduleId = address.moduleId;
                selection_.propertyId = address.propertyId;
                selection_.channel = address.channel;
            }
        } else if (selection_.kind ==
                   VfxWorkspaceSelectionKind::ColorStop) {
            const VFX::VfxEmitterData* emitter = nullptr;
            if (FindColorStop(
                    selection_.targetId,
                    &emitter,
                    &selection_.moduleId,
                    &selection_.propertyId) != nullptr &&
                emitter != nullptr) {
                selection_.emitterId = emitter->id;
            }
        }
    }

    std::string VfxWorkspaceController::AllocateStableId(
        std::string_view prefix) const {

        for (;;) {
            const std::string candidate = std::string(prefix) + "." +
                GenerateAssetGuid().value;
            if (!ContainsStableId(candidate)) {
                return candidate;
            }
        }
    }

    bool VfxWorkspaceController::ContainsStableId(
        std::string_view id) const noexcept {

        if (!document_.IsOpen()) {
            return false;
        }
        if (FindEmitter(id) != nullptr || FindInput(id) != nullptr ||
            FindInputLink(id) != nullptr || FindCurveKey(id) != nullptr ||
            FindColorStop(id) != nullptr) {
            return true;
        }
        for (const VFX::VfxEmitterData& emitter :
             document_.Asset().emitters) {
            for (uint8_t value = 0u;
                 value <= static_cast<uint8_t>(
                     VFX::VfxModuleKind::RenderShape);
                 ++value) {
                if (GetVfxModuleId(
                        emitter,
                        static_cast<VFX::VfxModuleKind>(value)) == id) {
                    return true;
                }
            }
        }
        return false;
    }

    void VfxWorkspaceController::RegenerateEmitterIds(
        VFX::VfxEmitterData& emitter) const {

        const std::string previousEmitterId = emitter.id;
        const std::array<std::string, 11u> previousModuleIds{
            emitter.basic.id,
            emitter.emitterTransform.id,
            emitter.spawn.id,
            emitter.shape.id,
            emitter.position.id,
            emitter.rotation.id,
            emitter.scale.id,
            emitter.forces.id,
            emitter.color.id,
            emitter.renderCommon.id,
            emitter.renderShape.id,
        };
        emitter.id = AllocateStableId("emitter");
        emitter.basic.id = AllocateStableId("module.basic");
        emitter.emitterTransform.id =
            AllocateStableId("module.emitter_transform");
        emitter.spawn.id = AllocateStableId("module.spawn");
        emitter.shape.id = AllocateStableId("module.shape");
        emitter.position.id = AllocateStableId("module.position");
        emitter.rotation.id = AllocateStableId("module.rotation");
        emitter.scale.id = AllocateStableId("module.scale");
        emitter.forces.id = AllocateStableId("module.forces");
        emitter.color.id = AllocateStableId("module.color");
        emitter.renderCommon.id =
            AllocateStableId("module.render_common");
        emitter.renderShape.id = AllocateStableId("module.render_shape");
        const std::array<std::string_view, 11u> nextModuleIds{
            emitter.basic.id,
            emitter.emitterTransform.id,
            emitter.spawn.id,
            emitter.shape.id,
            emitter.position.id,
            emitter.rotation.id,
            emitter.scale.id,
            emitter.forces.id,
            emitter.color.id,
            emitter.renderCommon.id,
            emitter.renderShape.id,
        };
        for (VFX::VfxInputLinkData& link : emitter.inputLinks) {
            link.id = AllocateStableId("link");
            if (link.target.emitterId == previousEmitterId) {
                link.target.emitterId = emitter.id;
            }
            for (size_t index = 0u;
                 index < previousModuleIds.size();
                 ++index) {
                if (link.target.moduleId == previousModuleIds[index]) {
                    link.target.moduleId = nextModuleIds[index];
                    break;
                }
            }
        }
        for (const VfxCurveAddress& address : CurveAddresses(emitter)) {
            VFX::VfxCurveData* curve = ResolveVfxCurve(emitter, address);
            if (curve == nullptr) {
                continue;
            }
            for (VFX::VfxCurveKeyData& key : curve->keys) {
                key.id = AllocateStableId("curve.key");
            }
        }
        if (VFX::VfxColorGradientData* gradient = ResolveVfxGradient(
                emitter, emitter.color.id, "value")) {
            for (VFX::VfxColorStopData& stop : gradient->stops) {
                stop.id = AllocateStableId("gradient.stop");
            }
        }
    }

    VFX::VfxEmitterData* VfxWorkspaceController::FindEmitterMutable(
        VFX::VfxAssetData& asset,
        std::string_view emitterId) const noexcept {

        const auto found = std::find_if(
            asset.emitters.begin(), asset.emitters.end(),
            [emitterId](const VFX::VfxEmitterData& emitter) {
                return emitter.id == emitterId;
            });
        return found != asset.emitters.end() ? &*found : nullptr;
    }

} // namespace HIKARI::EDITOR
