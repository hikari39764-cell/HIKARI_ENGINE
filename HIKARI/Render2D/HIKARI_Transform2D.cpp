#include "Render2D/HIKARI_Transform2D.h"

namespace HIKARI {

    Matrix3x3 Transform2D::ToWorld(float /*width*/, float /*height*/) const {

        Matrix3x3 tNegPivot = Matrix3x3::MakeTranslate(-pivotPx.x, -pivotPx.y);

        Matrix3x3 s = Matrix3x3::MakeScale(scale.x, scale.y);

        Matrix3x3 r = Matrix3x3::MakeRotate(rotation);

        Matrix3x3 tPos = Matrix3x3::MakeTranslate(position.x, position.y);

        return tNegPivot * s * r * tPos;
    }

    MATH::Mat4 Transform2D::GetWorldMatrix(float zForSort) const {
        const MATH::Vec3 t{ position.x - pivotPx.x, position.y - pivotPx.y, zForSort };
        const MATH::Quat r = MATH::Quat::FromEulerXYZ(0.0f, 0.0f, rotation);
        const MATH::Vec3 s{ scale.x, scale.y, 1.0f };
        return MATH::Mat4::TRS(t, r, s);
    }

} // namespace HIKARI
