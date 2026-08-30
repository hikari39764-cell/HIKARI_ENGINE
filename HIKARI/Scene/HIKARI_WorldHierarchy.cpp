#include "Scene/HIKARI_World.h"

#include <algorithm>
#include <cmath>

#include "Scene/HIKARI_GameObject.h"

namespace HIKARI {
    namespace {

        constexpr float kMatrixComparisonTolerance = 1.0e-4f;

        bool NearlyEqual(
            float lhs,
            float rhs,
            float tolerance = kMatrixComparisonTolerance) noexcept {
            if (!std::isfinite(lhs) || !std::isfinite(rhs)) {
                return false;
            }
            const float scale = (std::max)(
                1.0f,
                (std::max)(std::abs(lhs), std::abs(rhs)));
            return std::abs(lhs - rhs) <= tolerance * scale;
        }

        bool NearlyEqual(
            const MATH::Mat4& lhs,
            const MATH::Mat4& rhs) noexcept {
            for (int column = 0; column < 4; ++column) {
                for (int row = 0; row < 4; ++row) {
                    if (!NearlyEqual(
                            lhs.m[column][row],
                            rhs.m[column][row])) {
                        return false;
                    }
                }
            }
            return true;
        }

        bool TryInvert(
            const MATH::Mat4& value,
            MATH::Mat4& outInverse) noexcept {
            outInverse = MATH::Inverse(value);
            const MATH::Mat4 identity = MATH::Mat4::Identity();
            return NearlyEqual(value * outInverse, identity) &&
                NearlyEqual(outInverse * value, identity);
        }

        bool TryDecomposeLocalTransform(
            const MATH::Mat4& localMatrix,
            const Transform3D& current,
            Transform3D& outTransform) noexcept {
            MATH::Vec3 position{};
            MATH::Quat rotation{};
            MATH::Vec3 scale{};
            if (!MATH::DecomposeTRS(
                    localMatrix,
                    position,
                    rotation,
                    scale)) {
                return false;
            }

            // TRSで表現できない shear を暗黙に近似しない。
            if (!NearlyEqual(
                    MATH::Mat4::TRS(position, rotation, scale),
                    localMatrix)) {
                return false;
            }

            outTransform = current;
            outTransform.position = position;
            outTransform.rotation = rotation;
            outTransform.scale = scale;
            outTransform.useExplicitMatrix = false;
            return true;
        }

    } // namespace

    WorldMutationResult World::SetParent(
        RuntimeObjectHandle objectHandle,
        RuntimeObjectHandle parentHandle,
        WorldParentMode mode) {
        if (!IsOwnerThread()) {
            return WorldMutationResult::WrongThread;
        }
        if (!CanMutateStructure()) {
            return WorldMutationResult::MutationDisallowed;
        }
        GameObject* object = FindObject(objectHandle);
        if (object == nullptr) {
            return WorldMutationResult::InvalidObjectHandle;
        }

        GameObject* parent = nullptr;
        if (parentHandle.IsValid()) {
            parent = FindObject(parentHandle);
            if (parent == nullptr) {
                return WorldMutationResult::InvalidParentHandle;
            }
        }

        if (parent == object) {
            return WorldMutationResult::ParentCycle;
        }
        for (const GameObject* ancestor = parent;
            ancestor != nullptr;
            ancestor = ancestor->parent_) {
            if (ancestor == object) {
                return WorldMutationResult::ParentCycle;
            }
        }

        if (mode != WorldParentMode::KeepLocal &&
            mode != WorldParentMode::KeepWorld) {
            return WorldMutationResult::InvalidParentMode;
        }
        if (object->parent_ == parent) {
            return WorldMutationResult::Success;
        }

        if (mode == WorldParentMode::KeepLocal) {
            return object->SetParent(parent)
                ? WorldMutationResult::Success
                : WorldMutationResult::HierarchyRejected;
        }

        const MATH::Mat4 worldMatrix =
            object->transform_.GetWorldMatrix();
        MATH::Mat4 localMatrix = worldMatrix;
        if (parent != nullptr) {
            MATH::Mat4 inverseParent{};
            if (!TryInvert(
                    parent->transform_.GetWorldMatrix(),
                    inverseParent)) {
                return WorldMutationResult::TransformNotInvertible;
            }
            localMatrix = inverseParent * worldMatrix;
        }

        Transform3D localTransform{};
        if (!TryDecomposeLocalTransform(
                localMatrix,
                object->transform_,
                localTransform)) {
            return WorldMutationResult::TransformDecompositionFailed;
        }

        // ここまで成功してから階層とローカルTRSを変更する。
        if (!object->SetParent(parent)) {
            return WorldMutationResult::HierarchyRejected;
        }
        (void)object->SetLocalTransform(localTransform);
        return WorldMutationResult::Success;
    }

    WorldMutationResult World::SetLocalTransform(
        RuntimeObjectHandle objectHandle,
        const Transform3D& transform) {
        if (!IsOwnerThread()) {
            return WorldMutationResult::WrongThread;
        }
        if (worldClosing_) {
            return WorldMutationResult::WorldClosing;
        }
        bool massObject = false;
        const WorldMutationResult massResult = SetMassTransformIfPresent(
            objectHandle,
            transform,
            massObject);
        if (massObject) {
            return massResult;
        }
        GameObject* object = FindObject(objectHandle);
        if (object == nullptr || object->destroying_) {
            return WorldMutationResult::InvalidObjectHandle;
        }
        try {
            (void)object->SetLocalTransform(transform);
            return WorldMutationResult::Success;
        }
        catch (...) {
            return WorldMutationResult::InternalError;
        }
    }

    WorldMutationResult World::SetWorldTransform(
        RuntimeObjectHandle objectHandle,
        const Transform3D& transform) {
        if (!IsOwnerThread()) {
            return WorldMutationResult::WrongThread;
        }
        if (worldClosing_) {
            return WorldMutationResult::WorldClosing;
        }
        bool massObject = false;
        const WorldMutationResult massResult = SetMassTransformIfPresent(
            objectHandle,
            transform,
            massObject);
        if (massObject) {
            return massResult;
        }
        GameObject* object = FindObject(objectHandle);
        if (object == nullptr || object->destroying_) {
            return WorldMutationResult::InvalidObjectHandle;
        }

        MATH::Mat4 localMatrix = transform.GetLocalMatrix();
        if (const GameObject* parent = object->GetParent()) {
            MATH::Mat4 inverseParent{};
            if (!TryInvert(
                    parent->GetTransform().GetWorldMatrix(),
                    inverseParent)) {
                return WorldMutationResult::TransformNotInvertible;
            }
            localMatrix = inverseParent * localMatrix;
        }

        Transform3D localTransform{};
        if (!TryDecomposeLocalTransform(
                localMatrix,
                object->GetTransform(),
                localTransform)) {
            return WorldMutationResult::TransformDecompositionFailed;
        }
        (void)object->SetLocalTransform(localTransform);
        return WorldMutationResult::Success;
    }

    bool World::TryGetLocalTransform(
        RuntimeObjectHandle objectHandle,
        Transform3D& outTransform) const noexcept {
        if (!IsOwnerThread()) {
            return false;
        }
        bool massObject = false;
        const bool massResult = TryGetMassTransformIfPresent(
            objectHandle,
            outTransform,
            massObject);
        if (massObject) {
            return massResult;
        }
        const GameObject* object = FindObject(objectHandle);
        return object != nullptr &&
            TryDecomposeLocalTransform(
                object->GetTransform().GetLocalMatrix(),
                object->GetTransform(),
                outTransform);
    }

    bool World::TryGetWorldTransform(
        RuntimeObjectHandle objectHandle,
        Transform3D& outTransform) const noexcept {
        if (!IsOwnerThread()) {
            return false;
        }
        bool massObject = false;
        const bool massResult = TryGetMassTransformIfPresent(
            objectHandle,
            outTransform,
            massObject);
        if (massObject) {
            return massResult;
        }
        const GameObject* object = FindObject(objectHandle);
        return object != nullptr &&
            TryDecomposeLocalTransform(
                object->GetTransform().GetWorldMatrix(),
                object->GetTransform(),
                outTransform);
    }

} // namespace HIKARI
