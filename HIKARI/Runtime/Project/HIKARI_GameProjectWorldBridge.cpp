#include "HIKARI/Project/WorldView.h"

#include <cstring>
#include <string>

#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectTransformBridgeInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {
    namespace {

        static_assert(
            static_cast<uint8_t>(WorldCommandKind::SetParent) ==
            static_cast<uint8_t>(
                GameProjectWorldCommandKind::SetParent));
        static_assert(
            static_cast<uint8_t>(WorldCommandPhase::SystemPostRender) ==
            static_cast<uint8_t>(
                GameProjectWorldCommandPhase::SystemPostRender));

        bool TryReadObjectName(
            const GameProjectSpawnObjectDesc& desc,
            std::string& outName) {
            const void* terminator = std::memchr(
                desc.name,
                '\0',
                kGameProjectObjectNameCapacity);
            if (terminator == nullptr) {
                return false;
            }
            const auto* end = static_cast<const char*>(terminator);
            if (end == desc.name) {
                return false;
            }
            outName.assign(desc.name, end);
            return true;
        }

        bool HasNonZeroReserved(
            const GameProjectSpawnObjectDesc& desc) noexcept {
            for (uint32_t value : desc.reserved) {
                if (value != 0u) {
                    return true;
                }
            }
            return false;
        }

    } // namespace

    ObjectView GameProjectSdkAccess::MakeObjectView(
        GameObject& object) noexcept {
        World* world = object.GetWorld();
        return world != nullptr
            ? ObjectView(PROJECT::INTERNAL::EncodeRuntimeHandle(
                *world,
                object.GetRuntimeHandle()))
            : ObjectView{};
    }

    WorldView GameProjectSdkAccess::MakeWorldView(
        World& world) noexcept {
        return WorldView(GameProjectWorldHandle{
            world.GetRuntimeIdentity()
        });
    }

    GameObject* GameProjectSdkAccess::GetObject(
        const ObjectView& view,
        RuntimeWorldAccessResult* outAccess) noexcept {
        World* world = World::ResolveRuntimeWorld(
            view.handle_.world.value,
            outAccess);
        return world != nullptr
            ? world->FindObject(
                PROJECT::INTERNAL::DecodeRuntimeHandle(
                    view.handle_))
            : nullptr;
    }

    World* GameProjectSdkAccess::GetWorld(
        const WorldView& view,
        RuntimeWorldAccessResult* outAccess) noexcept {
        return World::ResolveRuntimeWorld(
            view.world_.value,
            outAccess);
    }

    bool ObjectView::IsValid() const noexcept {
        return GameProjectSdkAccess::GetObject(*this) != nullptr;
    }

    std::string_view ObjectView::GetName() const noexcept {
        const GameObject* object =
            GameProjectSdkAccess::GetObject(*this);
        return object != nullptr
            ? std::string_view(object->GetName())
            : std::string_view{};
    }

    ObjectView ObjectView::GetParent() const noexcept {
        GameObject* object =
            GameProjectSdkAccess::GetObject(*this);
        GameObject* parent = object != nullptr
            ? object->GetParent()
            : nullptr;
        return parent != nullptr
            ? GameProjectSdkAccess::MakeObjectView(*parent)
            : ObjectView{};
    }

    bool WorldView::IsValid() const noexcept {
        return GameProjectSdkAccess::GetWorld(*this) != nullptr;
    }

    ObjectView WorldView::FindObject(
        GameProjectObjectHandle handle) const noexcept {
        World* world = GameProjectSdkAccess::GetWorld(*this);
        if (world == nullptr ||
            !PROJECT::INTERNAL::IsObjectHandleForWorld(
                handle,
                *world)) {
            return {};
        }
        GameObject* object = world->FindObject(
            PROJECT::INTERNAL::DecodeRuntimeHandle(handle));
        return object != nullptr
            ? GameProjectSdkAccess::MakeObjectView(*object)
            : ObjectView{};
    }

    uint64_t WorldView::GetObjectCount() const noexcept {
        const World* world = GameProjectSdkAccess::GetWorld(*this);
        return world != nullptr
            ? static_cast<uint64_t>(world->GetObjects().size())
            : 0u;
    }

    bool WorldView::CanMutateStructure() const noexcept {
        const World* world = GameProjectSdkAccess::GetWorld(*this);
        return world != nullptr && world->CanMutateStructure();
    }

    GameProjectWorldResult WorldView::SpawnObject(
        const GameProjectSpawnObjectDesc& desc) const noexcept {
        RuntimeWorldAccessResult access{};
        World* world = GameProjectSdkAccess::GetWorld(
            *this,
            &access);
        if (world == nullptr) {
            return PROJECT::INTERNAL::MakeWorldAccessFailure(access);
        }
        if (HasNonZeroReserved(desc)) {
            return PROJECT::INTERNAL::MakeWorldResult(
                GameProjectWorldResultCode::InvalidRequest);
        }
        std::string name{};
        try {
            if (!TryReadObjectName(desc, name)) {
                return PROJECT::INTERNAL::MakeWorldResult(
                    GameProjectWorldResultCode::InvalidName);
            }
        }
        catch (...) {
            return PROJECT::INTERNAL::MakeWorldResult(
                GameProjectWorldResultCode::InternalError);
        }
        if (!PROJECT::INTERNAL::IsValidProjectTransform(
                desc.localTransform)) {
            return PROJECT::INTERNAL::MakeWorldResult(
                GameProjectWorldResultCode::InvalidTransform);
        }

        RuntimeObjectHandle parent{};
        if (!desc.parent.IsNull()) {
            if (!PROJECT::INTERNAL::IsObjectHandleForWorld(
                    desc.parent,
                    *world)) {
                return PROJECT::INTERNAL::MakeWorldResult(
                    GameProjectWorldResultCode::InvalidParent);
            }
            parent = PROJECT::INTERNAL::DecodeRuntimeHandle(desc.parent);
        }

        try {
            return PROJECT::INTERNAL::ToProjectCommandSubmission(
                *world,
                world->SubmitSpawn(
                    name,
                    PROJECT::INTERNAL::ToRuntimeTransform(
                        desc.localTransform),
                    parent));
        }
        catch (...) {
            return PROJECT::INTERNAL::MakeWorldResult(
                GameProjectWorldResultCode::InternalError);
        }
    }

    GameProjectWorldResult WorldView::DestroyObject(
        GameProjectObjectHandle object) const noexcept {
        RuntimeWorldAccessResult access{};
        World* world = GameProjectSdkAccess::GetWorld(
            *this,
            &access);
        if (world == nullptr) {
            return PROJECT::INTERNAL::MakeWorldAccessFailure(access);
        }
        if (!PROJECT::INTERNAL::IsObjectHandleForWorld(
                object,
                *world)) {
            return PROJECT::INTERNAL::MakeWorldResult(
                GameProjectWorldResultCode::InvalidObject);
        }
        const RuntimeObjectHandle runtimeHandle =
            PROJECT::INTERNAL::DecodeRuntimeHandle(object);
        try {
            return PROJECT::INTERNAL::ToProjectCommandSubmission(
                *world,
                world->SubmitDestroy(runtimeHandle));
        }
        catch (...) {
            return PROJECT::INTERNAL::MakeWorldResult(
                GameProjectWorldResultCode::InternalError,
                object);
        }
    }

    GameProjectWorldResult WorldView::SetParent(
        GameProjectObjectHandle object,
        GameProjectObjectHandle parent,
        GameProjectParentMode mode) const noexcept {
        RuntimeWorldAccessResult access{};
        World* world = GameProjectSdkAccess::GetWorld(
            *this,
            &access);
        if (world == nullptr) {
            return PROJECT::INTERNAL::MakeWorldAccessFailure(access);
        }
        if (!PROJECT::INTERNAL::IsObjectHandleForWorld(
                object,
                *world)) {
            return PROJECT::INTERNAL::MakeWorldResult(
                GameProjectWorldResultCode::InvalidObject);
        }
        if (!parent.IsNull() &&
            !PROJECT::INTERNAL::IsObjectHandleForWorld(
                parent,
                *world)) {
            return PROJECT::INTERNAL::MakeWorldResult(
                GameProjectWorldResultCode::InvalidParent,
                object);
        }

        WorldParentMode runtimeMode{};
        switch (mode) {
        case GameProjectParentMode::KeepLocal:
            runtimeMode = WorldParentMode::KeepLocal;
            break;
        case GameProjectParentMode::KeepWorld:
            runtimeMode = WorldParentMode::KeepWorld;
            break;
        default:
            return PROJECT::INTERNAL::MakeWorldResult(
                GameProjectWorldResultCode::InvalidRequest,
                object);
        }

        const RuntimeObjectHandle objectHandle =
            PROJECT::INTERNAL::DecodeRuntimeHandle(object);
        try {
            return PROJECT::INTERNAL::ToProjectCommandSubmission(
                *world,
                world->SubmitSetParent(
                    objectHandle,
                    PROJECT::INTERNAL::DecodeRuntimeHandle(parent),
                    runtimeMode));
        }
        catch (...) {
            return PROJECT::INTERNAL::MakeWorldResult(
                GameProjectWorldResultCode::InternalError,
                object);
        }
    }

    uint32_t WorldView::GetCommandFailureCount() const noexcept {
        const World* world = GameProjectSdkAccess::GetWorld(*this);
        return world != nullptr
            ? world->GetCommandFailureCount()
            : 0u;
    }

    bool WorldView::ReadCommandFailure(
        uint32_t index,
        GameProjectWorldCommandFailure& outFailure) const noexcept {
        World* world = GameProjectSdkAccess::GetWorld(*this);
        if (world == nullptr) {
            return false;
        }
        WorldCommandFailure failure{};
        if (!world->ReadCommandFailure(index, failure)) {
            return false;
        }
        outFailure.commandToken = failure.token;
        outFailure.kind = static_cast<GameProjectWorldCommandKind>(
            failure.kind);
        outFailure.code = PROJECT::INTERNAL::ToProjectMutationResult(
            *world,
            failure.result,
            failure.object).code;
        outFailure.phase =
            static_cast<GameProjectWorldCommandPhase>(failure.phase);
        outFailure.reserved = 0u;
        outFailure.object = PROJECT::INTERNAL::EncodeRuntimeHandle(
            *world,
            failure.object);
        outFailure.parent = PROJECT::INTERNAL::EncodeRuntimeHandle(
            *world,
            failure.parent);
        return true;
    }

    uint32_t WorldView::GetCommandFailureOverflowCount() const noexcept {
        const World* world = GameProjectSdkAccess::GetWorld(*this);
        return world != nullptr
            ? world->GetCommandFailureOverflowCount()
            : 0u;
    }

} // namespace HIKARI
