#include "Scene/HIKARI_SystemScheduler.h"

#include <cmath>
#include <string_view>

#include "HIKARI/Project/RenderFrame.h"
#include "HIKARI/Project/RenderPass.h"
#include "Scene/HIKARI_ISystem.h"
#include "Scene/HIKARI_SystemTypeRegistry.h"

namespace HIKARI {
namespace {

template <size_t Count>
bool HasNonZero(const uint32_t (&values)[Count]) noexcept {
    for (const uint32_t value : values) {
        if (value != 0u) {
            return true;
        }
    }
    return false;
}

bool IsFiniteRenderCamera(const GameProjectRenderCameraFrame& camera) noexcept {
    const auto finite = [](const float* values, size_t count) noexcept {
        for (size_t index = 0u; index < count; ++index) {
            if (!std::isfinite(values[index])) {
                return false;
            }
        }
        return true;
    };
    return finite(camera.view, 16u) && finite(camera.projection, 16u) && finite(camera.viewProjection, 16u) &&
           finite(camera.inverseViewProjection, 16u) && finite(camera.unjitteredViewProjection, 16u) &&
           finite(camera.previousViewProjection, 16u) && finite(camera.position, 3u) &&
           finite(camera.previousPosition, 3u) && finite(camera.jitterPixels, 2u) &&
           finite(camera.previousJitterPixels, 2u) && std::isfinite(camera.nearZ) && std::isfinite(camera.farZ) &&
           std::isfinite(camera.fovYRadians) && std::isfinite(camera.aspect);
}

bool IsValidRenderFrameView(const GameProjectRenderFrameView& frame, uint64_t expectedWorldIdentity) noexcept {
    constexpr uint32_t kReadAccessMask = static_cast<uint32_t>(GameProjectRenderSurfaceAccess::Sampled) |
                                         static_cast<uint32_t>(GameProjectRenderSurfaceAccess::ReadDepth) |
                                         static_cast<uint32_t>(GameProjectRenderSurfaceAccess::ReadUav) |
                                         static_cast<uint32_t>(GameProjectRenderSurfaceAccess::CopySource);
    constexpr uint32_t kExposureCapability = static_cast<uint32_t>(GameProjectRenderCapability::Exposure);
    if (frame.structSize != kGameProjectRenderFrameViewV1Size || frame.version != kGameProjectRenderFrameVersion ||
        !frame.frame.IsValid() || frame.stage >= GameProjectRenderObserverStage::Count ||
        frame.purpose >= GameProjectRenderViewPurpose::Count || frame.viewGeneration == 0u ||
        frame.worldIdentity == 0u || frame.worldIdentity != expectedWorldIdentity ||
        frame.surfaceCount > kGameProjectRenderMaximumSurfaceCount ||
        (frame.surfaceCount != 0u && frame.surfaces == nullptr) || frame.camera == nullptr ||
        frame.camera->structSize != kGameProjectRenderCameraFrameV1Size ||
        frame.camera->version != kGameProjectRenderFrameVersion || frame.camera->renderWidth == 0u ||
        frame.camera->renderHeight == 0u || frame.camera->outputWidth == 0u || frame.camera->outputHeight == 0u ||
        frame.camera->reserved0 != 0u || HasNonZero(frame.camera->reserved) || frame.reserved0 != 0u ||
        HasNonZero(frame.reserved) || !IsFiniteRenderCamera(*frame.camera) ||
        (frame.availableCapabilityMask & ~frame.supportedCapabilityMask) != 0u ||
        (frame.validCapabilityMask & ~frame.availableCapabilityMask) != 0u || frame.exposureValid > 1u ||
        frame.exposureMode >= GameProjectRenderExposureMode::Count ||
        (frame.exposureValid != 0u && (!std::isfinite(frame.exposureMultiplier) || !std::isfinite(frame.preExposure) ||
                                       (frame.validCapabilityMask & kExposureCapability) == 0u)) ||
        (frame.exposureValid == 0u && (frame.validCapabilityMask & kExposureCapability) != 0u)) {
        return false;
    }
    for (uint32_t index = 0u; index < frame.surfaceCount; ++index) {
        const GameProjectRenderSurfaceDesc& surface = frame.surfaces[index];
        if (surface.structSize != kGameProjectRenderSurfaceDescV1Size ||
            surface.version != kGameProjectRenderFrameVersion || !surface.handle.IsValid() ||
            !surface.handle.frame.Matches(frame.frame) || surface.handle.reserved0 != 0u ||
            surface.handle.reserved1 != 0u || surface.kind >= GameProjectRenderSurfaceKind::Count ||
            surface.availability >= GameProjectRenderSurfaceAvailability::Count ||
            surface.format >= GameProjectRenderSurfaceFormat::Count ||
            surface.lifetime >= GameProjectRenderSurfaceLifetime::Count ||
            surface.fallback >= GameProjectRenderSurfaceFallback::Count || surface.sampleable > 1u ||
            surface.reserved0 != 0u || surface.width == 0u || surface.height == 0u || surface.mipCount == 0u ||
            (surface.readAccessMask & ~kReadAccessMask) != 0u || HasNonZero(surface.reserved)) {
            return false;
        }
    }
    return true;
}

} // namespace

uint64_t SystemScheduler::GetRenderPassFailureCount() const noexcept {
    return IsOwnerThread() ? renderPassFailureCount_ : 0u;
}

bool SystemScheduler::ObserveRenderFrame(const GameProjectRenderFrameView& frame) noexcept {
    if (!IsOwnerThread()) {
        return false;
    }
    if (!IsValidRenderFrameView(frame, attachedWorldIdentity_)) {
        return false;
    }
    for (const SystemEntry& entry : systems_) {
        const auto* observer = dynamic_cast<const IRenderFrameObserverSystem*>(entry.system.get());
        if (observer == nullptr) {
            continue;
        }
        const GameProjectRenderObserverDesc& desc = observer->GetRenderObserverDesc();
        if (desc.structSize == 0u) {
            continue;
        }
        if (!IsValidRenderObserverDesc(desc)) {
            continue;
        }
        if (desc.stage != frame.stage) {
            continue;
        }
        (void)observer->ObserveRenderFrame(frame);
    }
    return true;
}

bool SystemScheduler::GetRenderPassCount(uint32_t& outCount) const noexcept {
    outCount = 0u;
    if (!IsOwnerThread() || attachedWorldIdentity_ == 0u) {
        return false;
    }
    for (const SystemEntry& entry : systems_) {
        const auto* pass = dynamic_cast<const IRenderFramePassSystem*>(entry.system.get());
        if (pass != nullptr && pass->GetRenderPassDesc().structSize != 0u) {
            ++outCount;
        }
    }
    return true;
}

bool SystemScheduler::DescribeRenderPass(uint32_t index, GameProjectRenderPassDesc& outDescriptor) const noexcept {
    outDescriptor = {};
    if (!IsOwnerThread() || attachedWorldIdentity_ == 0u) {
        return false;
    }
    uint32_t current = 0u;
    for (const SystemEntry& entry : systems_) {
        const auto* pass = dynamic_cast<const IRenderFramePassSystem*>(entry.system.get());
        if (pass == nullptr || pass->GetRenderPassDesc().structSize == 0u) {
            continue;
        }
        if (current++ != index) {
            continue;
        }
        const GameProjectRenderPassDesc& descriptor = pass->GetRenderPassDesc();
        if (!IsValidRenderPassDesc(descriptor)) {
            return false;
        }
        outDescriptor = descriptor;
        return true;
    }
    return false;
}

bool SystemScheduler::ExecuteRenderPass(const char* stableId, const GameProjectRenderPassContext& context,
                                        bool* outCallbackEntered) noexcept {
    if (outCallbackEntered != nullptr) {
        *outCallbackEntered = false;
    }
    constexpr uint32_t kClearCapability = static_cast<uint32_t>(GameProjectRenderCommandCapability::ClearWorkingColor);
    constexpr uint32_t kShaderCapability = static_cast<uint32_t>(GameProjectRenderCommandCapability::ShaderCommands);
    constexpr uint32_t kComputeCapability = static_cast<uint32_t>(GameProjectRenderCommandCapability::ComputeCommands);
    constexpr uint32_t kKnownCapabilities = kClearCapability | kShaderCapability | kComputeCapability;
    if (!IsOwnerThread()) {
        return false;
    }
    uint32_t stableIdLength = 0u;
    while (stableId != nullptr && stableIdLength < kGameProjectRenderPassIdCapacity &&
           stableId[stableIdLength] != '\0') {
        ++stableIdLength;
    }
    if (stableId == nullptr || stableIdLength == 0u || stableIdLength >= kGameProjectRenderPassIdCapacity ||
        context.structSize != kGameProjectRenderPassContextV1Size || context.version != kGameProjectRenderPassVersion ||
        context.frame == nullptr || context.frame->frame.viewId != kGameProjectPrimaryRenderViewId ||
        (context.frame->purpose != GameProjectRenderViewPurpose::Game &&
         context.frame->purpose != GameProjectRenderViewPurpose::EditorScene) ||
        !IsValidRenderFrameView(*context.frame, attachedWorldIdentity_) ||
        context.encoder.structSize != kGameProjectRenderCommandEncoderV1Size ||
        (context.encoder.version != kGameProjectRenderPassVersion &&
         context.encoder.version != kGameProjectRenderCommandEncoderVersion &&
         context.encoder.version != kGameProjectRenderComputeCommandEncoderVersion) ||
        context.encoder.context == nullptr || context.encoder.commandGeneration == 0u ||
        (context.encoder.capabilityMask & ~kKnownCapabilities) != 0u || context.encoder.reservedV1 != 0u ||
        HasNonZero(context.reserved)) {
        ++renderPassFailureCount_;
        return false;
    }
    const std::string_view requested(stableId, stableIdLength);
    for (const SystemEntry& entry : systems_) {
        const auto* pass = dynamic_cast<const IRenderFramePassSystem*>(entry.system.get());
        if (pass == nullptr) {
            continue;
        }
        const GameProjectRenderPassDesc& descriptor = pass->GetRenderPassDesc();
        if (descriptor.structSize == 0u || std::string_view(descriptor.stableId) != requested) {
            continue;
        }
        const bool expectsShaderCommands = (descriptor.flags & GameProjectRenderPassFlag::ShaderCommands) != 0u;
        const bool expectsComputeCommands = (descriptor.flags & GameProjectRenderPassFlag::ComputeCommands) != 0u;
        bool encoderMatches = false;
        if (expectsComputeCommands) {
            encoderMatches = context.encoder.version == kGameProjectRenderComputeCommandEncoderVersion &&
                             context.encoder.clearWorkingColor == nullptr &&
                             context.encoder.drawFullscreen == nullptr && context.encoder.dispatchCompute != nullptr &&
                             context.encoder.capabilityMask == kComputeCapability;
        } else if (expectsShaderCommands) {
            encoderMatches = context.encoder.version == kGameProjectRenderCommandEncoderVersion &&
                             context.encoder.clearWorkingColor != nullptr &&
                             context.encoder.drawFullscreen != nullptr && context.encoder.dispatchCompute == nullptr &&
                             context.encoder.capabilityMask == (kClearCapability | kShaderCapability);
        } else {
            encoderMatches = context.encoder.version == kGameProjectRenderPassVersion &&
                             context.encoder.clearWorkingColor != nullptr &&
                             context.encoder.drawFullscreen == nullptr && context.encoder.dispatchCompute == nullptr &&
                             context.encoder.capabilityMask == kClearCapability;
        }
        if (!encoderMatches || !IsValidRenderPassDesc(descriptor)) {
            ++renderPassFailureCount_;
            return false;
        }
        if (outCallbackEntered != nullptr) {
            *outCallbackEntered = true;
        }
        if (!pass->ExecuteRenderPass(context)) {
            ++renderPassFailureCount_;
            return false;
        }
        return true;
    }
    ++renderPassFailureCount_;
    return false;
}

} // namespace HIKARI
