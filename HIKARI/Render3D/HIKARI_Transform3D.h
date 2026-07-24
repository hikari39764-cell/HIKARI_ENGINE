#pragma once
#include "Render3D/HIKARI_Math3D.h"

namespace HIKARI {

    class GameObject;

    struct Transform3D {
        MATH::Vec3 position{ 0.0f, 0.0f, 0.0f };
        MATH::Quat rotation = MATH::Quat::Identity();
        MATH::Vec3 scale{ 1.0f, 1.0f, 1.0f };

        // For glTF node hierarchy and future animation/skinning paths.
        // When this flag is true, explicitMatrix is treated as this transform's final world matrix.
        // The TRS fields are still kept for legacy paths and simple editor display.
        bool useExplicitMatrix = false;
        MATH::Mat4 explicitMatrix{};

        MATH::Mat4 GetLocalMatrix() const;
        MATH::Mat4 GetWorldMatrix() const;
        const Transform3D* GetParent() const noexcept;
        bool HasSameLocalValue(const Transform3D& rhs) const noexcept;

    private:
        friend class GameObject;

        void SetParent(const Transform3D* parent) noexcept;
        const Transform3D* parent_ = nullptr;
    };

} // namespace HIKARI
