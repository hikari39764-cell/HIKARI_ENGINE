#include "Render3D/HIKARI_Transform3D.h"

namespace HIKARI {

    namespace {
        bool Equal(const MATH::Vec3& lhs, const MATH::Vec3& rhs) noexcept {
            return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
        }

        bool Equal(const MATH::Quat& lhs, const MATH::Quat& rhs) noexcept {
            return lhs.x == rhs.x && lhs.y == rhs.y &&
                lhs.z == rhs.z && lhs.w == rhs.w;
        }

        bool Equal(const MATH::Mat4& lhs, const MATH::Mat4& rhs) noexcept {
            for (int column = 0; column < 4; ++column) {
                for (int row = 0; row < 4; ++row) {
                    if (lhs.m[column][row] != rhs.m[column][row]) {
                        return false;
                    }
                }
            }
            return true;
        }
    }

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

    const Transform3D* Transform3D::GetParent() const noexcept {
        return parent_;
    }

    void Transform3D::SetParent(const Transform3D* parent) noexcept {
        parent_ = parent;
    }

    bool Transform3D::HasSameLocalValue(
        const Transform3D& rhs) const noexcept {

        return Equal(position, rhs.position) &&
            Equal(rotation, rhs.rotation) &&
            Equal(scale, rhs.scale) &&
            useExplicitMatrix == rhs.useExplicitMatrix &&
            Equal(explicitMatrix, rhs.explicitMatrix);
    }

} // namespace HIKARI
