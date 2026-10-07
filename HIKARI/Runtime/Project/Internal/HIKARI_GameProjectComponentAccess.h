#pragma once

#include <utility>

#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI::PROJECT::INTERNAL {

// 公開ビューが指すオブジェクトの組み込みコンポーネントを解決する。
// World・所有スレッド・世代・終了中の確認をここに集め、失敗理由は結果コードで返す。
template <class Component>
GameProjectWorldResult ResolveComponent(GameProjectObjectHandle handle, Component*& outComponent) noexcept {
    outComponent = nullptr;
    try {
        RuntimeWorldAccessResult access{};
        World* world = World::ResolveRuntimeWorld(handle.world.value, &access);
        if (world == nullptr) {
            return MakeWorldAccessFailure(access);
        }
        if (!world->IsOwnerThread()) {
            return MakeWorldResult(GameProjectWorldResultCode::WrongThread, handle);
        }
        if (!IsObjectHandleForWorld(handle, *world)) {
            return MakeWorldResult(GameProjectWorldResultCode::InvalidObject, handle);
        }
        if (GameProjectSdkAccess::IsWorldClosing(*world)) {
            return MakeWorldResult(GameProjectWorldResultCode::WorldClosing, handle);
        }
        GameObject* object = world->FindObject(DecodeRuntimeHandle(handle));
        if (object == nullptr) {
            return MakeWorldResult(GameProjectWorldResultCode::InvalidObject, handle);
        }
        outComponent = object->GetComponent<Component>();
        return MakeWorldResult(outComponent != nullptr ? GameProjectWorldResultCode::Success
                                                       : GameProjectWorldResultCode::Unsupported,
                               handle);
    } catch (...) {
        outComponent = nullptr;
        return MakeWorldResult(GameProjectWorldResultCode::InternalError, handle);
    }
}

// 解決したコンポーネントへ変更を適用する。変更関数は結果コードを返し、例外は InternalError になる。
template <class Component, class Mutation>
GameProjectWorldResult MutateComponent(GameProjectObjectHandle handle, Mutation&& mutation) noexcept {
    Component* component = nullptr;
    const GameProjectWorldResult resolved = ResolveComponent(handle, component);
    if (component == nullptr) {
        return resolved;
    }
    try {
        return MakeWorldResult(std::forward<Mutation>(mutation)(*component), handle);
    } catch (...) {
        return MakeWorldResult(GameProjectWorldResultCode::InternalError, handle);
    }
}

// 読み取り用。失敗時は出力を変えずに false。
template <class Component, class Read>
bool ReadComponent(GameProjectObjectHandle handle, Read&& read) noexcept {
    Component* component = nullptr;
    if (!ResolveComponent(handle, component).Succeeded() || component == nullptr) {
        return false;
    }
    try {
        return std::forward<Read>(read)(*component);
    } catch (...) {
        return false;
    }
}

} // namespace HIKARI::PROJECT::INTERNAL
