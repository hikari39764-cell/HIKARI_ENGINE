#include "Render3D/Temporal/HIKARI_TemporalResourceSystem.h"

#include <algorithm>

#include "Render3D/Resources/Descriptors/HIKARI_RenderResourceDescriptorPool.h"
#include "Render3D/Temporal/Internal/HIKARI_TemporalResourceSystemInternal.h"

namespace HIKARI::RENDER3D::TEMPORAL {

using namespace DETAIL;

RenderTarget2D* GetMotionVectorRenderTarget(TemporalResourceFrame frame) {
    TemporalResourceState* state = FindWritableState(frame);
    return state != nullptr && state->motionVectors.ready ? &state->motionVectors.target : nullptr;
}

RenderTarget2D* GetMotionMetadataRenderTarget(TemporalResourceFrame frame) {
    TemporalResourceState* state = FindWritableState(frame);
    return state != nullptr && state->motionMetadata.ready ? &state->motionMetadata.target : nullptr;
}

RenderTarget2D* GetHistoryColorWriteRenderTarget(TemporalResourceFrame frame) {
    TemporalResourceState* state = FindWritableState(frame);
    if (state == nullptr) {
        return nullptr;
    }
    TemporalTarget& target = state->historyColor[state->historyWriteIndex];
    return target.ready ? &target.target : nullptr;
}

RenderTarget2D* GetHistoryDepthWriteRenderTarget(TemporalResourceFrame frame) {
    TemporalResourceState* state = FindWritableState(frame);
    if (state == nullptr) {
        return nullptr;
    }
    TemporalTarget& target = state->historyDepth[state->historyWriteIndex];
    return target.ready ? &target.target : nullptr;
}

RenderTarget2D* GetTaaResolvedColorRenderTarget(TemporalResourceFrame frame) {
    TemporalResourceState* state = FindWritableState(frame);
    return state != nullptr && state->taaResolvedColor.ready ? &state->taaResolvedColor.target : nullptr;
}

RenderTarget2D* GetReactiveMaskRenderTarget(TemporalResourceFrame frame) {
    TemporalResourceState* state = FindWritableState(frame);
    return state != nullptr && state->reactiveMask.ready ? &state->reactiveMask.target : nullptr;
}

RenderTarget2D* GetTransparencyMaskRenderTarget(TemporalResourceFrame frame) {
    TemporalResourceState* state = FindWritableState(frame);
    return state != nullptr && state->transparencyMask.ready ? &state->transparencyMask.target : nullptr;
}

RenderTarget2D* GetInvalidDepthMotionMaskRenderTarget(TemporalResourceFrame frame) {
    TemporalResourceState* state = FindWritableState(frame);
    return state != nullptr && state->invalidDepthMotionMask.ready ? &state->invalidDepthMotionMask.target : nullptr;
}

RenderTarget2D* GetBiasCurrentColorMaskRenderTarget(TemporalResourceFrame frame) {
    TemporalResourceState* state = FindWritableState(frame);
    return state != nullptr && state->biasCurrentColorMask.ready ? &state->biasCurrentColorMask.target : nullptr;
}

RenderTarget2D* GetTemporalDebugRenderTarget(TemporalResourceFrame frame) {
    TemporalResourceState* state = FindActiveState(frame);
    return state != nullptr && state->debugOutput.ready ? &state->debugOutput.target : nullptr;
}

bool PrepareSceneColorInput(TemporalResourceFrame frame, RenderTarget2D& source) {
    TemporalResourceState* state = FindWritableState(frame);
    if (state == nullptr) {
        return false;
    }
    ID3D12Resource* resource = source.GetResource();
    const DXGI_FORMAT format = source.GetFormat();
    const uint32_t width = static_cast<uint32_t>((std::max)(1, source.GetWidth()));
    const uint32_t height = static_cast<uint32_t>((std::max)(1, source.GetHeight()));
    if (resource == nullptr || format == DXGI_FORMAT_UNKNOWN) {
        ReleaseExternalTexture(*state, state->sceneColor, TemporalResourceReleaseMode::DeferredUntilFence);
        state->sceneColorPrepared = {};
        RefreshStats(*state);
        return false;
    }

    const bool recreate = !state->sceneColor.ready || state->sceneColor.resource != resource ||
                          state->sceneColor.format != format || state->sceneColor.width != width ||
                          state->sceneColor.height != height || !state->sceneColor.srv.IsValid();
    if (recreate) {
        ReleaseExternalTexture(*state, state->sceneColor, TemporalResourceReleaseMode::DeferredUntilFence);
        state->sceneColor.srv = RENDER3D::AllocateTexture2DSrvDescriptor(resource, format, 0, 1);
        if (!state->sceneColor.srv.IsValid()) {
            ReleaseExternalTexture(*state, state->sceneColor, TemporalResourceReleaseMode::DeferredUntilFence);
            state->sceneColorPrepared = {};
            RefreshStats(*state);
            return false;
        }
        state->sceneColor.resource = resource;
        state->sceneColor.format = format;
        state->sceneColor.width = width;
        state->sceneColor.height = height;
        state->sceneColor.ready = true;
    }
    state->sceneColor.state = source.GetColorState();
    state->sceneColorPrepared = frame;
    RefreshStats(*state);
    return true;
}

bool SetTemporalCompositionBase(TemporalResourceFrame frame, D3D12_GPU_DESCRIPTOR_HANDLE sceneColorSrv) {
    TemporalResourceState* state = FindWritableState(frame);
    if (state == nullptr) {
        return false;
    }
    state->compositionBaseSrv = sceneColorSrv;
    state->compositionBaseWritten = sceneColorSrv.ptr != 0u ? frame : TemporalResourceFrame{};
    return sceneColorSrv.ptr != 0u;
}

D3D12_GPU_DESCRIPTOR_HANDLE GetTemporalCompositionBase(TemporalResourceFrame frame) {
    TemporalResourceState* state = FindActiveState(frame);
    return state != nullptr && StampMatches(state->compositionBaseWritten, frame) ? state->compositionBaseSrv
                                                                                  : D3D12_GPU_DESCRIPTOR_HANDLE{};
}

bool SetTemporalDebugView(TemporalResourceFrame frame, RenderDebugView view) {
    TemporalResourceState* state = FindWritableState(frame);
    if (state == nullptr) {
        return false;
    }
    state->debugView = view;
    RefreshStats(*state);
    return true;
}

RenderDebugView GetTemporalDebugView(TemporalResourceFrame frame) {
    TemporalResourceState* state = FindActiveState(frame);
    return state != nullptr ? state->debugView : RenderDebugView::None;
}

bool UpdateTemporalExposure(TemporalResourceFrame frame, const COLOR::ExposureFrameState& exposureState) {
    TemporalResourceState* state = FindWritableState(frame);
    if (state == nullptr) {
        return false;
    }
    state->exposureState = exposureState;
    state->exposureWritten = {};
    if (!state->exposure.ready || state->recordingContext.cmdList == nullptr) {
        RefreshStats(*state);
        return false;
    }
    const float safeExposure = COLOR::ResolveTemporalExposure(exposureState);
    state->exposure.target.TransitionColor(D3D12_RESOURCE_STATE_RENDER_TARGET);
    const float clearValue[4] = {safeExposure, safeExposure, safeExposure, safeExposure};
    state->recordingContext.cmdList->ClearRenderTargetView(state->exposure.target.GetRtvHandle(), clearValue, 0,
                                                           nullptr);
    state->exposure.target.TransitionColor(D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    state->exposureWritten = frame;
    RefreshStats(*state);
    return true;
}

bool MarkTemporalMasksWritten(TemporalResourceFrame frame, bool written, bool compositionDerived) {
    TemporalResourceState* state = FindWritableState(frame);
    if (state == nullptr) {
        return false;
    }
    const bool ready =
        written && state->reactiveMask.ready && state->transparencyMask.ready && state->invalidDepthMotionMask.ready &&
        state->biasCurrentColorMask.ready;
    state->masksWritten = ready ? frame : TemporalResourceFrame{};
    state->stats.masksCompositionDerived = ready && compositionDerived;
    RefreshStats(*state);
    return ready;
}

bool SetTemporalGeometryDrawCounts(TemporalResourceFrame frame, uint32_t rigid, uint32_t skinned,
                                   uint32_t alphaMasked) {
    TemporalResourceState* state = FindWritableState(frame);
    if (state == nullptr) {
        return false;
    }
    state->stats.rigidVelocityDrawCount = rigid;
    state->stats.skinnedVelocityDrawCount = skinned;
    state->stats.alphaMaskedVelocityDrawCount = alphaMasked;
    return true;
}

bool MarkMotionVectorsWritten(TemporalResourceFrame frame, bool written) {
    TemporalResourceState* state = FindWritableState(frame);
    if (state == nullptr) {
        return false;
    }
    const bool ready = written && state->motionVectors.ready && state->motionMetadata.ready;
    state->motionWritten = ready ? frame : TemporalResourceFrame{};
    RefreshStats(*state);
    return ready;
}

bool MarkTemporalAntiAliasing(TemporalResourceFrame frame, bool enabled, bool resolved) {
    TemporalResourceState* state = FindActiveState(frame);
    if (state == nullptr) {
        return false;
    }
    state->stats.taaEnabled = enabled;
    state->stats.taaResolved = resolved && state->historyColorValid && state->historyDepthValid;
    return true;
}

bool MarkTemporalHistoryOutputWritten(TemporalResourceFrame frame, bool colorWritten, bool depthWritten) {
    TemporalResourceState* state = FindWritableState(frame);
    if (state == nullptr) {
        return false;
    }
    const bool written = colorWritten && depthWritten;
    state->historyOutputWritten = written ? frame : TemporalResourceFrame{};
    return written;
}

bool CommitTemporalHistory(TemporalResourceFrame frame, bool written) {
    TemporalResourceState* state = FindWritableState(frame);
    if (state == nullptr) {
        return false;
    }
    if (!written) {
        state->historyColorValid = false;
        state->historyDepthValid = false;
        state->historyWritten = {};
        state->historyOutputWritten = {};
        RefreshStats(*state);
        return true;
    }
    if (!StampMatches(state->historyOutputWritten, frame) || !state->historyColor[state->historyWriteIndex].ready ||
        !state->historyDepth[state->historyWriteIndex].ready) {
        state->historyColorValid = false;
        state->historyDepthValid = false;
        state->historyWritten = {};
        state->historyOutputWritten = {};
        RefreshStats(*state);
        return false;
    }
    if (!CompleteTemporalFrame(frame.view, frame.publication, true)) {
        state->historyColorValid = false;
        state->historyDepthValid = false;
        state->historyWritten = {};
        state->historyOutputWritten = {};
        RefreshStats(*state);
        return false;
    }
    state->historyReadIndex = state->historyWriteIndex;
    state->historyWriteIndex = 1u - state->historyReadIndex;
    state->historyColorValid = true;
    state->historyDepthValid = true;
    state->historyWritten = frame;
    state->historyOutputWritten = {};
    RefreshStats(*state);
    return true;
}

bool TryBuildTemporalPassInputs(TemporalResourceFrame frame, const RenderTextureView& sceneDepth,
                                const RenderTextureView& sceneColor, TemporalInputs& outInputs) {
    outInputs = {};
    TemporalResourceState* state = FindActiveState(frame);
    if (state == nullptr) {
        return false;
    }
    outInputs = BuildResidentInputs(*state, sceneDepth, sceneColor);
    return outInputs.resourceFrame.Matches(frame) && outInputs.frame.publication.Matches(frame.publication);
}

bool TryBuildTemporalResolveInputs(TemporalResourceFrame frame, RenderTarget2D& sceneColorTarget,
                                   RenderTarget2D& sceneDepthTarget, TemporalInputs& outInputs) {
    outInputs = {};
    TemporalResourceState* state = FindActiveState(frame);
    if (state == nullptr) {
        return false;
    }

    RenderTextureView sceneDepth{};
    sceneDepth.resource = sceneDepthTarget.GetDepthResource();
    sceneDepth.srv = sceneDepthTarget.GetDepthSrvGpu();
    sceneDepth.format = DXGI_FORMAT_R32_FLOAT;
    sceneDepth.width = static_cast<uint32_t>((std::max)(1, sceneDepthTarget.GetWidth()));
    sceneDepth.height = static_cast<uint32_t>((std::max)(1, sceneDepthTarget.GetHeight()));
    sceneDepth.mipCount = 1;
    sceneDepth.state = sceneDepthTarget.GetDepthState();
    sceneDepth.valid = sceneDepth.resource != nullptr && sceneDepth.srv.ptr != 0u;

    RenderTextureView sceneColor = MakeTextureView(state->sceneColor);
    sceneColor.valid = sceneColor.valid && state->sceneColor.resource == sceneColorTarget.GetResource();
    outInputs = BuildResidentInputs(*state, sceneDepth, sceneColor);
    ApplyResolveReadValidity(*state, outInputs);
    state->sceneDepth = sceneDepth;
    state->sceneDepthOwner = &sceneDepthTarget;
    return outInputs.resourceFrame.Matches(frame) && outInputs.frame.publication.Matches(frame.publication);
}

bool TryGetTemporalResolveInputs(TemporalResourceFrame frame, TemporalInputs& outInputs) {
    outInputs = {};
    TemporalResourceState* state = FindActiveState(frame);
    if (state == nullptr) {
        return false;
    }
    RenderTextureView sceneDepth = state->sceneDepth;
    if (state->sceneDepthOwner != nullptr) {
        sceneDepth.state = state->sceneDepthOwner->GetDepthState();
    }
    outInputs = BuildResidentInputs(*state, sceneDepth, MakeTextureView(state->sceneColor));
    ApplyResolveReadValidity(*state, outInputs);
    return outInputs.resourceFrame.Matches(frame) && outInputs.frame.publication.Matches(frame.publication);
}

TemporalResourceStats GetLatestTemporalResourceStats(RenderViewId viewId) {
    if (viewId.value == 0u || !CheckOwnerThread()) {
        return {};
    }
    TemporalResourceSystemState& system = System();
    const auto found = system.views.find(viewId.value);
    if (found == system.views.end() || found->second == nullptr) {
        return {};
    }
    RefreshStats(*found->second);
    return found->second->stats;
}

} // namespace HIKARI::RENDER3D::TEMPORAL
