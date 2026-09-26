#include "Scene/Terrain/HIKARI_TerrainSurfaceFollowerComponent.h"

#include <algorithm>

#include "Authoring/HIKARI_IInspectorBuilder.h"

namespace HIKARI {

void TerrainSurfaceFollowerComponent::BuildInspector(IInspectorBuilder& builder) {
    float radius = GetRadius();
    float heightOffset = GetHeightOffset();
    float smoothingTime = GetSmoothingTime();

    if (builder.Section("Ball Surface Fit", true)) {
        builder.FloatRange(AuthoringFieldDescriptor{"radius", "Radius",
                                                    "Ball radius in local meters. Zero resolves the sphere "
                                                    "collider or procedural sphere automatically."},
                           radius, 0.0f, 10.0f, 0.05f);
        builder.FloatRange(AuthoringFieldDescriptor{"heightOffset", "Height Offset",
                                                    "Extra vertical offset above the sampled sand height."},
                           heightOffset, -2.0f, 2.0f, 0.01f);
        builder.FloatRange(AuthoringFieldDescriptor{"smoothingTime", "Smoothing Time",
                                                    "How fast the visual ball settles onto the surface."},
                           smoothingTime, 0.001f, 1.0f, 0.01f);
    }

    if (radius != GetRadius()) {
        SetRadius(radius);
    }
    if (heightOffset != GetHeightOffset()) {
        SetHeightOffset(heightOffset);
    }
    if (smoothingTime != GetSmoothingTime()) {
        SetSmoothingTime(smoothingTime);
    }
}

} // namespace HIKARI
