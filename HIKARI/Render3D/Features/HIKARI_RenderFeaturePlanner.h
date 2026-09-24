#pragma once

#include "Render3D/Features/HIKARI_RenderFeatureTypes.h"

namespace HIKARI::RENDER3D::FEATURES {

RenderFeaturePlan BuildRenderFeaturePlan(const RenderFeatureRequestSet& requests,
                                         const RenderFeatureAvailability& availability);

} // namespace HIKARI::RENDER3D::FEATURES
