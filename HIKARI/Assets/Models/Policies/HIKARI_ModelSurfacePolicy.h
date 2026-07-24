#pragma once

#include <algorithm>
#include <cmath>
#include <string_view>

#include "Assets/Models/HIKARI_ModelAssetTypes.h"
#include "Assets/Models/Policies/HIKARI_ModelMaterialPolicy.h"
#include "Core/Math/HIKARI_MathValidation.h"

namespace HIKARI::SURFACE_POLICY {

    inline bool HasThinSurfaceCue(std::string_view text) {
        return
            MATERIAL_POLICY::HasThinTransparentCue(text) ||
            MATERIAL_POLICY::ContainsLowerAscii(text, "fence") ||
            MATERIAL_POLICY::ContainsLowerAscii(text, "grate") ||
            MATERIAL_POLICY::ContainsLowerAscii(text, "grille") ||
            MATERIAL_POLICY::ContainsLowerAscii(text, "leaf") ||
            MATERIAL_POLICY::ContainsLowerAscii(text, "leaves") ||
            MATERIAL_POLICY::ContainsLowerAscii(text, "foliage") ||
            MATERIAL_POLICY::ContainsLowerAscii(text, "grass") ||
            MATERIAL_POLICY::ContainsLowerAscii(text, "plant") ||
            MATERIAL_POLICY::ContainsLowerAscii(text, "curtain") ||
            MATERIAL_POLICY::ContainsLowerAscii(text, "cloth") ||
            MATERIAL_POLICY::ContainsLowerAscii(text, "fabric") ||
            MATERIAL_POLICY::ContainsLowerAscii(text, "paper") ||
            MATERIAL_POLICY::ContainsLowerAscii(text, "decal");
    }

    inline bool HasThinSurfaceMaterialCue(const MaterialAsset& material) {
        return
            MATERIAL_POLICY::HasThinTransparentSurfaceHint(material) ||
            HasThinSurfaceCue(material.name) ||
            HasThinSurfaceCue(material.shaderProfileId) ||
            HasThinSurfaceCue(material.defaultMaterialFxProfileId);
    }

    inline MATH::Vec3 BoundsExtent(const Bounds& bounds) {
        return {
            std::abs(bounds.max.x - bounds.min.x),
            std::abs(bounds.max.y - bounds.min.y),
            std::abs(bounds.max.z - bounds.min.z)
        };
    }

    inline bool IsThinPrimitivePlane(const MeshPrimitive& primitive) {
        const MATH::Vec3 extent = BoundsExtent(primitive.bounds);
        if (!MATH::IsFinite(extent)) {
            return false;
        }

        const float largest = (std::max)({ extent.x, extent.y, extent.z });
        const float smallest = (std::min)({ extent.x, extent.y, extent.z });
        const float middle = extent.x + extent.y + extent.z - largest - smallest;
        if (largest <= 1.0e-5f || middle <= 1.0e-5f) {
            return false;
        }

        const float thinRatio = smallest / largest;
        return thinRatio <= 0.025f || smallest <= middle * 0.035f;
    }

    inline bool ShouldRenderDoubleSided(
        const MaterialAsset& material,
        const MeshPrimitive& primitive) {

        if (!material.doubleSided) {
            return false;
        }

        if (MATERIAL_POLICY::HasBlendedSurface(material)) {
            return true;
        }

        if (MATERIAL_POLICY::HasAlphaMaskedSurface(material) ||
            MATERIAL_POLICY::HasExplicitThinTransparentSurface(material)) {
            return
                HasThinSurfaceMaterialCue(material) ||
                HasThinSurfaceCue(primitive.name) ||
                IsThinPrimitivePlane(primitive);
        }

        return false;
    }

} // namespace HIKARI::SURFACE_POLICY
