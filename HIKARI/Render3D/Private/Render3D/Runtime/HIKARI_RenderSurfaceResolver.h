#pragma once

#include <vector>

#include "RenderContracts/Model/HIKARI_AnimationPose.h"
#include "RenderContracts/Model/HIKARI_ModelAsset.h"
#include "Render3D/Runtime/HIKARI_RenderModelAsset.h"

namespace HIKARI::RENDER3D::RUNTIME {

std::vector<MATH::Mat4> BuildRenderModelNodeGlobals(const RenderModelAsset& renderModel);

std::vector<MATH::Mat4> BuildRenderModelNodeGlobals(const RenderModelAsset& renderModel, const ModelAsset* model,
                                                    const ANIMATION::AnimationLocalPose& pose);

bool ResolveRenderSurfaceDrawWorldMatrix(const Transform3D& objectWorldTransform, const RenderSurfaceRecord& surface,
                                         const std::vector<MATH::Mat4>& nodeGlobals, MATH::Mat4& outDrawWorldMatrix);

bool ResolveClusterSurfaceWorldMatrix(const Transform3D& objectWorldTransform, const RenderSurfaceRecord& surface,
                                      const std::vector<MATH::Mat4>& posedNodeGlobals,
                                      const std::vector<MATH::Mat4>& bindNodeGlobals, bool hasRuntimeAnimation,
                                      MATH::Mat4& outClusterWorldMatrix);

Bounds ResolveRenderSurfaceWorldBounds(const Bounds& fallbackWorldBounds, const RenderSurfaceRecord& surface,
                                       const MATH::Mat4& drawWorldMatrix, bool hasDrawWorldMatrix);

bool HasValidRenderSurfacePrimitive(const ModelAsset* model, const RenderSurfaceRecord& surface);

} // namespace HIKARI::RENDER3D::RUNTIME
