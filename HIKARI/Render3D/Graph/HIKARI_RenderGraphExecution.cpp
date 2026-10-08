#include "Render3D/Graph/HIKARI_RenderGraph.h"

#include <algorithm>

namespace HIKARI::RENDER3D::GRAPH {

bool RenderGraph::Compile() noexcept {
    if (executing_ || executionState_.status != RenderGraphExecutionStatus::NotStarted) {
        if (!executing_ && executionState_.status == RenderGraphExecutionStatus::Failed) {
            return false;
        }
        SetDiagnostic(executing_ ? RenderGraphResultCode::MutationDuringExecution
                                 : RenderGraphResultCode::InvalidExecutionState,
                      "RenderGraph compile rejected after execution began.");
        return false;
    }
    if (!frame_.IsValid()) {
        SetDiagnostic(RenderGraphResultCode::InvalidFrame, "RenderGraph compile requires a valid frame token.");
        return false;
    }

    compiled_ = false;
    compiledPassCount_ = 0u;
    executionState_ = {};
    outgoingEdges_.fill(0u);
    compiledOrder_.fill(kInvalidRenderGraphIndex);
    for (uint32_t resourceIndex = 0u; resourceIndex < resourceCount_; ++resourceIndex) {
        RenderGraphResourceInfo& info = resources_[resourceIndex].info;
        resources_[resourceIndex].binding = {};
        info.writerPassIndex = kInvalidRenderGraphIndex;
        info.firstUseOrder = kInvalidRenderGraphIndex;
        info.lastUseOrder = kInvalidRenderGraphIndex;
    }
    if (!PrepareImportedSurfaceBindings()) {
        return false;
    }

    for (uint32_t passIndex = 0u; passIndex < passCount_; ++passIndex) {
        const PassRecord& pass = passes_[passIndex];
        for (uint32_t offset = 0u; offset < pass.writeCount; ++offset) {
            const RenderGraphResourceUse& use = uses_[pass.writeBegin + offset];
            RenderGraphResourceInfo& info = resources_[use.resource.index].info;
            if (info.writerPassIndex != kInvalidRenderGraphIndex) {
                SetDiagnostic(RenderGraphResultCode::DuplicateWriter, "RenderGraph resource has more than one writer.",
                              passIndex, use.resource.index);
                return false;
            }
            info.writerPassIndex = passIndex;
        }
    }

    for (uint32_t resourceIndex = 0u; resourceIndex < resourceCount_; ++resourceIndex) {
        const RenderGraphResourceInfo& info = resources_[resourceIndex].info;
        if (!info.imported && info.kind != RenderGraphResourceKind::Internal &&
            info.writerPassIndex == kInvalidRenderGraphIndex) {
            SetDiagnostic(RenderGraphResultCode::MissingProducer,
                          "RenderGraph surface or completion marker has no producer.", kInvalidRenderGraphIndex,
                          resourceIndex);
            return false;
        }
    }

    std::array<uint32_t, kRenderGraphMaximumPassCount> indegrees{};
    for (uint32_t passIndex = 0u; passIndex < passCount_; ++passIndex) {
        const PassRecord& pass = passes_[passIndex];
        for (uint32_t offset = 0u; offset < pass.readCount; ++offset) {
            const RenderGraphResourceUse& use = uses_[pass.readBegin + offset];
            const RenderGraphResourceInfo& info = resources_[use.resource.index].info;
            if (info.writerPassIndex == kInvalidRenderGraphIndex) {
                if (!info.imported) {
                    SetDiagnostic(RenderGraphResultCode::MissingProducer,
                                  "RenderGraph read has no producer or imported source.", passIndex,
                                  use.resource.index);
                    return false;
                }
                continue;
            }
            const uint64_t consumerBit = 1ull << passIndex;
            uint64_t& outgoing = outgoingEdges_[info.writerPassIndex];
            if ((outgoing & consumerBit) == 0u) {
                outgoing |= consumerBit;
                ++indegrees[passIndex];
            }
        }
    }

    uint64_t selectedMask = 0u;
    for (uint32_t order = 0u; order < passCount_; ++order) {
        uint32_t selected = kInvalidRenderGraphIndex;
        for (uint32_t candidate = 0u; candidate < passCount_; ++candidate) {
            const uint64_t bit = 1ull << candidate;
            if ((selectedMask & bit) == 0u && indegrees[candidate] == 0u) {
                selected = candidate;
                break;
            }
        }
        if (selected == kInvalidRenderGraphIndex) {
            SetDiagnostic(RenderGraphResultCode::CycleDetected, "RenderGraph pass dependency cycle detected.");
            return false;
        }
        compiledOrder_[order] = selected;
        selectedMask |= 1ull << selected;
        uint64_t outgoing = outgoingEdges_[selected];
        while (outgoing != 0u) {
            uint32_t target = 0u;
            while ((outgoing & (1ull << target)) == 0u) {
                ++target;
            }
            outgoing &= ~(1ull << target);
            if (indegrees[target] > 0u) {
                --indegrees[target];
            }
        }
    }

    for (uint32_t order = 0u; order < passCount_; ++order) {
        const PassRecord& pass = passes_[compiledOrder_[order]];
        const auto recordUse = [&](const RenderGraphResourceUse& use) {
            RenderGraphResourceInfo& info = resources_[use.resource.index].info;
            info.firstUseOrder =
                info.firstUseOrder == kInvalidRenderGraphIndex ? order : (std::min)(info.firstUseOrder, order);
            info.lastUseOrder =
                info.lastUseOrder == kInvalidRenderGraphIndex ? order : (std::max)(info.lastUseOrder, order);
        };
        for (uint32_t offset = 0u; offset < pass.readCount; ++offset) {
            recordUse(uses_[pass.readBegin + offset]);
        }
        for (uint32_t offset = 0u; offset < pass.writeCount; ++offset) {
            recordUse(uses_[pass.writeBegin + offset]);
        }
    }

    compiledPassCount_ = passCount_;
    executionState_ = {};
    compiled_ = true;
    ClearDiagnostic();
    return true;
}

bool RenderGraph::ValidateResourceDesc(const RenderGraphResourceDesc& desc) noexcept {
    if (desc.semantic >= RenderGraphSemantic::Count || desc.kind >= RenderGraphResourceKind::Count ||
        desc.requirement >= RenderGraphResourceRequirement::Count ||
        desc.expectedProducer >= SURFACE::SceneSurfaceProducer::Count ||
        desc.phase >= RenderGraphSemanticPhase::Count) {
        SetDiagnostic(RenderGraphResultCode::InvalidResource,
                      "RenderGraph resource metadata contains an invalid enum value.");
        return false;
    }

    if (desc.kind == RenderGraphResourceKind::CompletionMarker) {
        if (desc.semantic != RenderGraphSemantic::None || desc.imported || desc.declaredAccessMask != 0u ||
            desc.phase != RenderGraphSemanticPhase::None ||
            desc.requirement != RenderGraphResourceRequirement::Required ||
            desc.expectedProducer != SURFACE::SceneSurfaceProducer::None) {
            SetDiagnostic(RenderGraphResultCode::SemanticContractMismatch,
                          "Completion markers cannot declare surface metadata.");
            return false;
        }
        return true;
    }

    if (desc.kind == RenderGraphResourceKind::Internal) {
        if (desc.semantic != RenderGraphSemantic::None || desc.phase != RenderGraphSemanticPhase::None ||
            desc.requirement != RenderGraphResourceRequirement::Required ||
            desc.expectedProducer != SURFACE::SceneSurfaceProducer::None || desc.declaredAccessMask == 0u ||
            (desc.declaredAccessMask & ~kRenderGraphPhysicalAccessMask) != 0u) {
            SetDiagnostic(RenderGraphResultCode::SemanticContractMismatch,
                          "Internal resources require an explicit physical access contract.");
            return false;
        }
        return true;
    }

    const SURFACE::SceneSurfaceSemanticDesc& canonicalContract = SURFACE::GetSceneSurfaceSemanticDesc(desc.semantic);
    if (desc.semantic == RenderGraphSemantic::None || desc.declaredAccessMask != 0u ||
        desc.phase == RenderGraphSemanticPhase::None) {
        SetDiagnostic(RenderGraphResultCode::SemanticContractMismatch,
                      "RenderGraph resource does not match the semantic catalog.");
        return false;
    }
    if (!SURFACE::SceneSurfaceProducerAllowed(canonicalContract, desc.expectedProducer)) {
        SetDiagnostic(RenderGraphResultCode::ProducerNotAllowed,
                      "RenderGraph resource producer is not allowed by the semantic.");
        return false;
    }
    if (desc.requirement == RenderGraphResourceRequirement::Optional &&
        canonicalContract.fallback == SURFACE::SceneSurfaceFallbackPolicy::RequiredFail) {
        SetDiagnostic(RenderGraphResultCode::SemanticContractMismatch,
                      "Required-fail semantics cannot be optional imports.");
        return false;
    }
    if (!SURFACE::IsSceneSurfaceKeyAllowed(RenderGraphSurfaceKey{desc.semantic, desc.phase})) {
        SetDiagnostic(RenderGraphResultCode::SemanticContractMismatch,
                      "RenderGraph phase is not allowed by the semantic catalog.");
        return false;
    }
    return true;
}

bool RenderGraph::IsReadAccess(RenderGraphAccess access) const noexcept {
    switch (access) {
    case RenderGraphAccess::ReadSampled:
    case RenderGraphAccess::ReadDepth:
    case RenderGraphAccess::ReadUav:
    case RenderGraphAccess::CopySource:
    case RenderGraphAccess::ReadDependency: return true;
    default: return false;
    }
}

bool RenderGraph::IsWriteAccess(RenderGraphAccess access) const noexcept {
    switch (access) {
    case RenderGraphAccess::WriteColor:
    case RenderGraphAccess::WriteDepth:
    case RenderGraphAccess::WriteUav:
    case RenderGraphAccess::CopyDestination:
    case RenderGraphAccess::WriteDependency: return true;
    default: return false;
    }
}

bool RenderGraph::IsAccessAllowed(const RenderGraphResourceInfo& resource, RenderGraphAccess access) const noexcept {
    const uint32_t accessBit = RenderGraphAccessBit(access);
    if (accessBit == 0u) {
        return false;
    }
    if (resource.kind == RenderGraphResourceKind::CompletionMarker) {
        return (kRenderGraphDependencyAccessMask & accessBit) != 0u;
    }
    if (resource.kind == RenderGraphResourceKind::Internal) {
        return (resource.declaredAccessMask & accessBit) != 0u;
    }
    if ((kRenderGraphDependencyAccessMask & accessBit) != 0u) {
        return false;
    }
    SURFACE::SceneSurfaceAccess semanticAccess = SURFACE::SceneSurfaceAccess::None;
    switch (access) {
    case RenderGraphAccess::ReadSampled: semanticAccess = SURFACE::SceneSurfaceAccess::Sampled; break;
    case RenderGraphAccess::ReadDepth: semanticAccess = SURFACE::SceneSurfaceAccess::ReadDepth; break;
    case RenderGraphAccess::ReadUav: semanticAccess = SURFACE::SceneSurfaceAccess::ReadUav; break;
    case RenderGraphAccess::CopySource: semanticAccess = SURFACE::SceneSurfaceAccess::CopySource; break;
    case RenderGraphAccess::WriteColor: semanticAccess = SURFACE::SceneSurfaceAccess::WriteColor; break;
    case RenderGraphAccess::WriteDepth: semanticAccess = SURFACE::SceneSurfaceAccess::WriteDepth; break;
    case RenderGraphAccess::WriteUav: semanticAccess = SURFACE::SceneSurfaceAccess::WriteUav; break;
    case RenderGraphAccess::CopyDestination: semanticAccess = SURFACE::SceneSurfaceAccess::CopyDestination; break;
    case RenderGraphAccess::ReadDependency:
    case RenderGraphAccess::WriteDependency:
    case RenderGraphAccess::Count: return false;
    }
    const SURFACE::SceneSurfaceSemanticDesc& semanticContract = SURFACE::GetSceneSurfaceSemanticDesc(resource.semantic);
    return (semanticContract.allowedAccessMask & SURFACE::SceneSurfaceAccessBit(semanticAccess)) != 0u;
}

const char* ToString(RenderGraphSemantic semantic) noexcept {
    switch (semantic) {
    case RenderGraphSemantic::None: return "None";
    case RenderGraphSemantic::SceneDepth: return "SceneDepth";
    case RenderGraphSemantic::SceneColorOpaque: return "SceneColorOpaque";
    case RenderGraphSemantic::SceneColorPostOpaque: return "SceneColorPostOpaque";
    case RenderGraphSemantic::SceneColorFinal: return "SceneColorFinal";
    case RenderGraphSemantic::PackedSurface: return "PackedSurface";
    case RenderGraphSemantic::SurfaceFeatureMask: return "SurfaceFeatureMask";
    case RenderGraphSemantic::MotionVectors: return "MotionVectors";
    case RenderGraphSemantic::MotionMetadata: return "MotionMetadata";
    case RenderGraphSemantic::CurrentDepthPyramid: return "CurrentDepthPyramid";
    case RenderGraphSemantic::VisibilityDepthPyramid: return "VisibilityDepthPyramid";
    case RenderGraphSemantic::HistoryColor: return "HistoryColor";
    case RenderGraphSemantic::HistoryDepth: return "HistoryDepth";
    case RenderGraphSemantic::StylizedOutlineStyle: return "StylizedOutlineStyle";
    case RenderGraphSemantic::AmbientOcclusion: return "AmbientOcclusion";
    case RenderGraphSemantic::ReactiveMask: return "ReactiveMask";
    case RenderGraphSemantic::TransparencyMask: return "TransparencyMask";
    case RenderGraphSemantic::InvalidDepthMotionMask: return "InvalidDepthMotionMask";
    case RenderGraphSemantic::ProjectDispatchOutput: return "ProjectDispatchOutput";
    case RenderGraphSemantic::ReflectionFallbackSpecular: return "ReflectionFallbackSpecular";
    case RenderGraphSemantic::ReflectionSpecularResponse: return "ReflectionSpecularResponse";
    case RenderGraphSemantic::ReflectionMaterial: return "ReflectionMaterial";
    case RenderGraphSemantic::SubsurfaceDiffuse: return "SubsurfaceDiffuse";
    case RenderGraphSemantic::Count: break;
    }
    return "Unknown";
}

const char* ToString(RenderGraphSemanticPhase phase) noexcept {
    switch (phase) {
    case RenderGraphSemanticPhase::None: return "None";
    case RenderGraphSemanticPhase::Imported: return "Imported";
    case RenderGraphSemanticPhase::PreOpaque: return "PreOpaque";
    case RenderGraphSemanticPhase::Opaque: return "Opaque";
    case RenderGraphSemanticPhase::PostOpaque: return "PostOpaque";
    case RenderGraphSemanticPhase::Temporal: return "Temporal";
    case RenderGraphSemanticPhase::Final: return "Final";
    case RenderGraphSemanticPhase::HistoryRead: return "HistoryRead";
    case RenderGraphSemanticPhase::HistoryWrite: return "HistoryWrite";
    case RenderGraphSemanticPhase::Count: break;
    }
    return "Unknown";
}

const char* ToString(RenderGraphResultCode code) noexcept {
    switch (code) {
    case RenderGraphResultCode::Success: return "Success";
    case RenderGraphResultCode::InvalidFrame: return "InvalidFrame";
    case RenderGraphResultCode::InvalidIdentifier: return "InvalidIdentifier";
    case RenderGraphResultCode::CapacityExceeded: return "CapacityExceeded";
    case RenderGraphResultCode::DuplicateResourceId: return "DuplicateResourceId";
    case RenderGraphResultCode::DuplicatePassId: return "DuplicatePassId";
    case RenderGraphResultCode::DuplicateSemantic: return "DuplicateSemantic";
    case RenderGraphResultCode::InvalidResource: return "InvalidResource";
    case RenderGraphResultCode::InvalidAccess: return "InvalidAccess";
    case RenderGraphResultCode::AccessNotAllowed: return "AccessNotAllowed";
    case RenderGraphResultCode::SemanticContractMismatch: return "SemanticContractMismatch";
    case RenderGraphResultCode::ProducerNotAllowed: return "ProducerNotAllowed";
    case RenderGraphResultCode::DuplicateUse: return "DuplicateUse";
    case RenderGraphResultCode::ReadWriteConflict: return "ReadWriteConflict";
    case RenderGraphResultCode::DuplicateWriter: return "DuplicateWriter";
    case RenderGraphResultCode::MissingProducer: return "MissingProducer";
    case RenderGraphResultCode::CycleDetected: return "CycleDetected";
    case RenderGraphResultCode::ExtensionRejected: return "ExtensionRejected";
    case RenderGraphResultCode::NotCompiled: return "NotCompiled";
    case RenderGraphResultCode::ExecuteFailed: return "ExecuteFailed";
    case RenderGraphResultCode::MissingSurfacePublication: return "MissingSurfacePublication";
    case RenderGraphResultCode::SurfacePublicationMismatch: return "SurfacePublicationMismatch";
    case RenderGraphResultCode::MutationDuringExecution: return "MutationDuringExecution";
    case RenderGraphResultCode::InvalidExecutionState: return "InvalidExecutionState";
    case RenderGraphResultCode::PassNotFound: return "PassNotFound";
    case RenderGraphResultCode::ExecutionAborted: return "ExecutionAborted";
    }
    return "Unknown";
}

} // namespace HIKARI::RENDER3D::GRAPH
