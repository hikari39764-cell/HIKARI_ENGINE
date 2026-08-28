#include "HIKARI/Project/WorldView.h"

#include <cmath>

#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectTransformBridgeInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {
    namespace PROJECT::INTERNAL {

        static bool IsFinite(GameProjectFloat3 value) noexcept {
            return std::isfinite(value.x) &&
                std::isfinite(value.y) &&
                std::isfinite(value.z);
        }

        static double QuaternionLength(
            GameProjectQuaternion rotation) noexcept {
            return std::hypot(
                std::hypot(
                    static_cast<double>(rotation.x),
                    static_cast<double>(rotation.y)),
                std::hypot(
                    static_cast<double>(rotation.z),
                    static_cast<double>(rotation.w)));
        }

        bool IsValidProjectTransform(
            const GameProjectTransform& transform) noexcept {
            const double quaternionLength =
                QuaternionLength(transform.rotation);
            const bool fieldsValid = IsFinite(transform.position) &&
                IsFinite(transform.scale) &&
                std::isfinite(transform.rotation.x) &&
                std::isfinite(transform.rotation.y) &&
                std::isfinite(transform.rotation.z) &&
                std::isfinite(transform.rotation.w) &&
                std::isfinite(quaternionLength) &&
                quaternionLength > 1.0e-8 &&
                std::abs(transform.scale.x) > 1.0e-8f &&
                std::abs(transform.scale.y) > 1.0e-8f &&
                std::abs(transform.scale.z) > 1.0e-8f;
            if (!fieldsValid) {
                return false;
            }
            const MATH::Mat4 localMatrix =
                ToRuntimeTransform(transform).GetLocalMatrix();
            for (int column = 0; column < 4; ++column) {
                for (int row = 0; row < 4; ++row) {
                    if (!std::isfinite(localMatrix.m[column][row])) {
                        return false;
                    }
                }
            }
            return true;
        }

        Transform3D ToRuntimeTransform(
            const GameProjectTransform& transform) noexcept {
            const double inverseLength =
                1.0 / QuaternionLength(transform.rotation);
            Transform3D result{};
            result.position = {
                transform.position.x,
                transform.position.y,
                transform.position.z
            };
            result.rotation = {
                static_cast<float>(
                    static_cast<double>(transform.rotation.x) *
                    inverseLength),
                static_cast<float>(
                    static_cast<double>(transform.rotation.y) *
                    inverseLength),
                static_cast<float>(
                    static_cast<double>(transform.rotation.z) *
                    inverseLength),
                static_cast<float>(
                    static_cast<double>(transform.rotation.w) *
                    inverseLength)
            };
            result.scale = {
                transform.scale.x,
                transform.scale.y,
                transform.scale.z
            };
            return result;
        }

        GameProjectTransform ToProjectTransform(
            const Transform3D& transform) noexcept {
            const GameProjectQuaternion sourceRotation{
                transform.rotation.x,
                transform.rotation.y,
                transform.rotation.z,
                transform.rotation.w
            };
            const double quaternionLength =
                QuaternionLength(sourceRotation);
            GameProjectQuaternion rotation{};
            if (std::isfinite(quaternionLength) &&
                quaternionLength > 1.0e-8) {
                const double inverseLength = 1.0 / quaternionLength;
                rotation = {
                    static_cast<float>(
                        static_cast<double>(transform.rotation.x) *
                        inverseLength),
                    static_cast<float>(
                        static_cast<double>(transform.rotation.y) *
                        inverseLength),
                    static_cast<float>(
                        static_cast<double>(transform.rotation.z) *
                        inverseLength),
                    static_cast<float>(
                        static_cast<double>(transform.rotation.w) *
                        inverseLength)
                };
            }
            return GameProjectTransform{
                {
                    transform.position.x,
                    transform.position.y,
                    transform.position.z
                },
                rotation,
                {
                    transform.scale.x,
                    transform.scale.y,
                    transform.scale.z
                }
            };
        }

    } // namespace PROJECT::INTERNAL

    bool ObjectView::GetLocalTransform(
        GameProjectTransform& outTransform) const noexcept {
        World* world = World::ResolveRuntimeWorld(
            handle_.world.value);
        Transform3D transform{};
        if (world == nullptr ||
            !world->TryGetLocalTransform(
                PROJECT::INTERNAL::DecodeRuntimeHandle(handle_),
                transform)) {
            return false;
        }
        outTransform = PROJECT::INTERNAL::ToProjectTransform(transform);
        return true;
    }

    bool ObjectView::GetWorldTransform(
        GameProjectTransform& outTransform) const noexcept {
        World* world = World::ResolveRuntimeWorld(
            handle_.world.value);
        Transform3D transform{};
        if (world == nullptr ||
            !world->TryGetWorldTransform(
                PROJECT::INTERNAL::DecodeRuntimeHandle(handle_),
                transform)) {
            return false;
        }
        outTransform = PROJECT::INTERNAL::ToProjectTransform(transform);
        return true;
    }

    GameProjectWorldResult ObjectView::SetLocalTransform(
        const GameProjectTransform& transform) const noexcept {
        RuntimeWorldAccessResult access{};
        GameObject* object =
            GameProjectSdkAccess::GetObject(*this, &access);
        if (object == nullptr) {
            return access == RuntimeWorldAccessResult::WrongThread
                ? PROJECT::INTERNAL::MakeWorldAccessFailure(access)
                : PROJECT::INTERNAL::MakeWorldResult(
                    GameProjectWorldResultCode::InvalidObject);
        }
        if (!PROJECT::INTERNAL::IsValidProjectTransform(transform)) {
            return PROJECT::INTERNAL::MakeWorldResult(
                GameProjectWorldResultCode::InvalidTransform,
                handle_);
        }
        try {
            (void)object->SetLocalTransform(
                PROJECT::INTERNAL::ToRuntimeTransform(transform));
            return PROJECT::INTERNAL::MakeWorldResult(
                GameProjectWorldResultCode::Success,
                handle_);
        }
        catch (...) {
            return PROJECT::INTERNAL::MakeWorldResult(
                GameProjectWorldResultCode::InternalError,
                handle_);
        }
    }

    GameProjectWorldResult ObjectView::SetWorldTransform(
        const GameProjectTransform& transform) const noexcept {
        RuntimeWorldAccessResult access{};
        GameObject* object =
            GameProjectSdkAccess::GetObject(*this, &access);
        if (object == nullptr) {
            return access == RuntimeWorldAccessResult::WrongThread
                ? PROJECT::INTERNAL::MakeWorldAccessFailure(access)
                : PROJECT::INTERNAL::MakeWorldResult(
                    GameProjectWorldResultCode::InvalidObject);
        }
        if (!PROJECT::INTERNAL::IsValidProjectTransform(transform)) {
            return PROJECT::INTERNAL::MakeWorldResult(
                GameProjectWorldResultCode::InvalidTransform,
                handle_);
        }

        World* world = object->GetWorld();
        if (world == nullptr) {
            return PROJECT::INTERNAL::MakeWorldResult(
                GameProjectWorldResultCode::InvalidWorld);
        }
        const RuntimeObjectHandle handle = object->GetRuntimeHandle();
        try {
            return PROJECT::INTERNAL::ToProjectMutationResult(
                *world,
                world->SetWorldTransform(
                    handle,
                    PROJECT::INTERNAL::ToRuntimeTransform(transform)),
                handle);
        }
        catch (...) {
            return PROJECT::INTERNAL::MakeWorldResult(
                GameProjectWorldResultCode::InternalError,
                handle_);
        }
    }

} // namespace HIKARI
