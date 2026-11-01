#include "Core/Math/HIKARI_Transform3D.h"

namespace HIKARI {

MATH::Mat4 Transform3D::GetLocalMatrix() const {
    if (useExplicitMatrix) {
        return explicitMatrix;
    }
    return MATH::Mat4::TRS(position, MATH::NormalizeQ(rotation), scale);
}

MATH::Mat4 Transform3D::GetWorldMatrix() const {
    if (useExplicitMatrix) {
        return explicitMatrix;
    }
    if (!parent_) {
        return GetLocalMatrix();
    }
    return parent_->GetWorldMatrix() * GetLocalMatrix();
}

const Transform3D* Transform3D::GetParent() const noexcept { return parent_; }

void Transform3D::SetParent(const Transform3D* parent) noexcept { parent_ = parent; }

bool Transform3D::HasSameLocalValue(const Transform3D& rhs) const noexcept {
    return position == rhs.position && rotation == rhs.rotation && scale == rhs.scale &&
           useExplicitMatrix == rhs.useExplicitMatrix && explicitMatrix == rhs.explicitMatrix;
}

} // namespace HIKARI
