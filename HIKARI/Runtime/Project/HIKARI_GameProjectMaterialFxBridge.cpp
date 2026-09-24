#include "HIKARI/Project/MaterialFxView.h"

#include <cmath>
#include <cstring>
#include <string>

#include "HIKARI/Project/PresentationViews.h"
#include "HIKARI/Project/WorldView.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"
#include "Scene/Components/Rendering/MaterialFx/HIKARI_MaterialFxComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_PresentationModelService.h"
#include "Scene/HIKARI_World.h"
#include "Vfx/MaterialFx/HIKARI_MaterialFxProfile.h"

namespace HIKARI {
namespace {
struct ResolvedMaterialFxTarget {
    World* world = nullptr;
    MaterialFxComponent* component = nullptr;
    PresentationModelService* presentation = nullptr;
    uint64_t ownerObjectHandle = 0u;
    uint64_t instanceId = 0u;
};

bool IsValidKey(std::string_view key) noexcept {
    return !key.empty() && key.size() <= kGameProjectMaterialFxParameterKeyMaximumBytes;
}

const std::string* FindProfileId(const ResolvedMaterialFxTarget& target) noexcept {
    if (target.component != nullptr) {
        return &target.component->GetProfileId();
    }
    return target.presentation != nullptr
               ? target.presentation->FindMaterialFxProfileId(target.ownerObjectHandle, target.instanceId)
               : nullptr;
}

bool IsValidProfileId(std::string_view profileId) noexcept {
    return !profileId.empty() && profileId.size() <= kGameProjectMaterialFxProfileIdMaximumBytes;
}

bool IsResolvedProfile(std::string_view profileId) {
    if (!IsValidProfileId(profileId)) {
        return false;
    }
    MaterialFxProfile profile{};
    return MaterialFxProfile::LoadById(std::string(profileId), profile);
}
} // namespace

struct GameProjectMaterialFxBridgeAccess {
    static bool Resolve(const MaterialFxView& view, ResolvedMaterialFxTarget& outTarget) noexcept {
        outTarget = {};
        try {
            if (view.reserved_ != 0u || !view.world_.IsValid()) {
                return false;
            }
            World* world = World::ResolveRuntimeWorld(view.world_.value);
            if (world == nullptr || !world->IsOwnerThread() || GameProjectSdkAccess::IsWorldClosing(*world)) {
                return false;
            }
            outTarget.world = world;
            switch (view.targetKind_) {
            case MaterialFxView::TargetKind::SceneObject: {
                if (!PROJECT::INTERNAL::IsObjectHandleForWorld(view.object_, *world)) {
                    return false;
                }
                GameObject* object = world->FindObject(PROJECT::INTERNAL::DecodeRuntimeHandle(view.object_));
                outTarget.component = object != nullptr ? object->GetComponent<MaterialFxComponent>() : nullptr;
                return outTarget.component != nullptr;
            }
            case MaterialFxView::TargetKind::PresentationModel: {
                if (view.ownerObjectHandle_ == 0u || view.instanceId_ == 0u) {
                    return false;
                }
                PresentationModelService* service = world->Services().Find<PresentationModelService>();
                if (service == nullptr || !service->HasRecord(view.ownerObjectHandle_, view.instanceId_)) {
                    return false;
                }
                outTarget.presentation = service;
                outTarget.ownerObjectHandle = view.ownerObjectHandle_;
                outTarget.instanceId = view.instanceId_;
                return true;
            }
            case MaterialFxView::TargetKind::None:
            default: return false;
            }
        } catch (...) {
            outTarget = {};
            return false;
        }
    }
};

MaterialFxView ObjectView::GetMaterialFx() const noexcept { return MaterialFxView(handle_); }

MaterialFxView PresentationModelView::GetMaterialFx(uint64_t ownerObjectHandle, uint64_t instanceId) const noexcept {
    World* world = GameProjectSdkAccess::GetWorld(*this);
    return world != nullptr
               ? MaterialFxView(GameProjectWorldHandle{world->GetRuntimeIdentity()}, ownerObjectHandle, instanceId)
               : MaterialFxView{};
}

bool MaterialFxView::IsValid() const noexcept {
    ResolvedMaterialFxTarget target{};
    return GameProjectMaterialFxBridgeAccess::Resolve(*this, target);
}

size_t MaterialFxView::GetProfileIdSize() const noexcept {
    ResolvedMaterialFxTarget target{};
    if (!GameProjectMaterialFxBridgeAccess::Resolve(*this, target)) {
        return 0u;
    }
    const std::string* profileId = FindProfileId(target);
    return profileId != nullptr ? profileId->size() : 0u;
}

bool MaterialFxView::CopyProfileId(char* destination, size_t capacity) const noexcept {
    if (destination != nullptr && capacity > 0u) {
        destination[0] = '\0';
    }
    try {
        ResolvedMaterialFxTarget target{};
        if (!GameProjectMaterialFxBridgeAccess::Resolve(*this, target)) {
            return false;
        }
        const std::string* profileId = FindProfileId(target);
        if (profileId == nullptr || destination == nullptr || capacity <= profileId->size()) {
            return false;
        }
        if (!profileId->empty()) {
            std::memcpy(destination, profileId->data(), profileId->size());
        }
        destination[profileId->size()] = '\0';
        return true;
    } catch (...) {
        if (destination != nullptr && capacity > 0u) {
            destination[0] = '\0';
        }
        return false;
    }
}

bool MaterialFxView::SetProfileId(std::string_view profileId) const noexcept {
    try {
        ResolvedMaterialFxTarget target{};
        if (!GameProjectMaterialFxBridgeAccess::Resolve(*this, target)) {
            return false;
        }
        // Target/world validation must precede profile resolution: the
        // latter may touch the process cache or perform asset I/O.
        if (!IsResolvedProfile(profileId)) {
            return false;
        }
        if (target.component != nullptr) {
            return target.component->SetProfileId(std::string(profileId));
        }
        return target.presentation != nullptr &&
               target.presentation->SetMaterialFxProfile(target.ownerObjectHandle, target.instanceId, profileId);
    } catch (...) {
        return false;
    }
}

bool MaterialFxView::ClearProfile() const noexcept {
    try {
        ResolvedMaterialFxTarget target{};
        if (!GameProjectMaterialFxBridgeAccess::Resolve(*this, target)) {
            return false;
        }
        if (target.component != nullptr) {
            return target.component->SetProfileId({});
        }
        return target.presentation != nullptr &&
               target.presentation->ClearMaterialFx(target.ownerObjectHandle, target.instanceId);
    } catch (...) {
        return false;
    }
}

bool MaterialFxView::ResetToProfileDefaults() const noexcept {
    try {
        ResolvedMaterialFxTarget target{};
        if (!GameProjectMaterialFxBridgeAccess::Resolve(*this, target)) {
            return false;
        }
        const std::string* profileId = FindProfileId(target);
        if (profileId == nullptr || !IsValidProfileId(*profileId)) {
            return false;
        }
        if (target.component != nullptr) {
            return target.component->ResetToProfileDefaults();
        }
        return target.presentation != nullptr &&
               target.presentation->ResetMaterialFxToProfileDefaults(target.ownerObjectHandle, target.instanceId);
    } catch (...) {
        return false;
    }
}

bool MaterialFxView::SetFloat(std::string_view key, float value) const noexcept {
    try {
        if (!IsValidKey(key) || !std::isfinite(value)) {
            return false;
        }
        ResolvedMaterialFxTarget target{};
        if (!GameProjectMaterialFxBridgeAccess::Resolve(*this, target)) {
            return false;
        }
        return target.component != nullptr ? target.component->SetFloat(std::string(key), value)
                                           : target.presentation != nullptr &&
                                                 target.presentation->SetMaterialFxFloat(target.ownerObjectHandle,
                                                                                         target.instanceId, key, value);
    } catch (...) {
        return false;
    }
}

bool MaterialFxView::SetFloat2(std::string_view key, GameProjectFloat2 value) const noexcept {
    try {
        if (!IsValidKey(key) || !std::isfinite(value.x) || !std::isfinite(value.y)) {
            return false;
        }
        ResolvedMaterialFxTarget target{};
        if (!GameProjectMaterialFxBridgeAccess::Resolve(*this, target)) {
            return false;
        }
        const DirectX::XMFLOAT2 native{value.x, value.y};
        return target.component != nullptr
                   ? target.component->SetFloat2(std::string(key), native)
                   : target.presentation != nullptr && target.presentation->SetMaterialFxFloat2(
                                                           target.ownerObjectHandle, target.instanceId, key, native);
    } catch (...) {
        return false;
    }
}

bool MaterialFxView::SetFloat3(std::string_view key, GameProjectFloat3 value) const noexcept {
    try {
        if (!IsValidKey(key) || !std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z)) {
            return false;
        }
        ResolvedMaterialFxTarget target{};
        if (!GameProjectMaterialFxBridgeAccess::Resolve(*this, target)) {
            return false;
        }
        const DirectX::XMFLOAT3 native{value.x, value.y, value.z};
        return target.component != nullptr
                   ? target.component->SetFloat3(std::string(key), native)
                   : target.presentation != nullptr && target.presentation->SetMaterialFxFloat3(
                                                           target.ownerObjectHandle, target.instanceId, key, native);
    } catch (...) {
        return false;
    }
}

bool MaterialFxView::SetFloat4(std::string_view key, GameProjectFloat4 value) const noexcept {
    try {
        if (!IsValidKey(key) || !std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z) ||
            !std::isfinite(value.w)) {
            return false;
        }
        ResolvedMaterialFxTarget target{};
        if (!GameProjectMaterialFxBridgeAccess::Resolve(*this, target)) {
            return false;
        }
        const DirectX::XMFLOAT4 native{value.x, value.y, value.z, value.w};
        return target.component != nullptr
                   ? target.component->SetFloat4(std::string(key), native)
                   : target.presentation != nullptr && target.presentation->SetMaterialFxFloat4(
                                                           target.ownerObjectHandle, target.instanceId, key, native);
    } catch (...) {
        return false;
    }
}

bool MaterialFxView::SetToggle(std::string_view key, uint8_t value) const noexcept {
    try {
        if (!IsValidKey(key) || value > 1u) {
            return false;
        }
        ResolvedMaterialFxTarget target{};
        if (!GameProjectMaterialFxBridgeAccess::Resolve(*this, target)) {
            return false;
        }
        return target.component != nullptr ? target.component->SetToggle(std::string(key), value != 0u)
                                           : target.presentation != nullptr &&
                                                 target.presentation->SetMaterialFxToggle(
                                                     target.ownerObjectHandle, target.instanceId, key, value != 0u);
    } catch (...) {
        return false;
    }
}

bool MaterialFxView::TryGetFloat(std::string_view key, float& outValue) const noexcept {
    outValue = 0.0f;
    try {
        if (!IsValidKey(key)) {
            return false;
        }
        ResolvedMaterialFxTarget target{};
        if (!GameProjectMaterialFxBridgeAccess::Resolve(*this, target)) {
            return false;
        }
        return target.component != nullptr
                   ? target.component->GetFloat(std::string(key), outValue)
                   : target.presentation != nullptr && target.presentation->TryGetMaterialFxFloat(
                                                           target.ownerObjectHandle, target.instanceId, key, outValue);
    } catch (...) {
        outValue = 0.0f;
        return false;
    }
}

bool MaterialFxView::TryGetFloat2(std::string_view key, GameProjectFloat2& outValue) const noexcept {
    outValue = {};
    try {
        if (!IsValidKey(key)) {
            return false;
        }
        ResolvedMaterialFxTarget target{};
        if (!GameProjectMaterialFxBridgeAccess::Resolve(*this, target)) {
            return false;
        }
        DirectX::XMFLOAT2 native{};
        const bool succeeded = target.component != nullptr
                                   ? target.component->GetFloat2(std::string(key), native)
                                   : target.presentation != nullptr &&
                                         target.presentation->TryGetMaterialFxFloat2(target.ownerObjectHandle,
                                                                                     target.instanceId, key, native);
        if (succeeded) {
            outValue = {native.x, native.y};
        }
        return succeeded;
    } catch (...) {
        outValue = {};
        return false;
    }
}

bool MaterialFxView::TryGetFloat3(std::string_view key, GameProjectFloat3& outValue) const noexcept {
    outValue = {};
    try {
        if (!IsValidKey(key)) {
            return false;
        }
        ResolvedMaterialFxTarget target{};
        if (!GameProjectMaterialFxBridgeAccess::Resolve(*this, target)) {
            return false;
        }
        DirectX::XMFLOAT3 native{};
        const bool succeeded = target.component != nullptr
                                   ? target.component->GetFloat3(std::string(key), native)
                                   : target.presentation != nullptr &&
                                         target.presentation->TryGetMaterialFxFloat3(target.ownerObjectHandle,
                                                                                     target.instanceId, key, native);
        if (succeeded) {
            outValue = {native.x, native.y, native.z};
        }
        return succeeded;
    } catch (...) {
        outValue = {};
        return false;
    }
}

bool MaterialFxView::TryGetFloat4(std::string_view key, GameProjectFloat4& outValue) const noexcept {
    outValue = {};
    try {
        if (!IsValidKey(key)) {
            return false;
        }
        ResolvedMaterialFxTarget target{};
        if (!GameProjectMaterialFxBridgeAccess::Resolve(*this, target)) {
            return false;
        }
        DirectX::XMFLOAT4 native{};
        const bool succeeded = target.component != nullptr
                                   ? target.component->GetFloat4(std::string(key), native)
                                   : target.presentation != nullptr &&
                                         target.presentation->TryGetMaterialFxFloat4(target.ownerObjectHandle,
                                                                                     target.instanceId, key, native);
        if (succeeded) {
            outValue = {native.x, native.y, native.z, native.w};
        }
        return succeeded;
    } catch (...) {
        outValue = {};
        return false;
    }
}

bool MaterialFxView::TryGetToggle(std::string_view key, uint8_t& outValue) const noexcept {
    outValue = 0u;
    try {
        if (!IsValidKey(key)) {
            return false;
        }
        ResolvedMaterialFxTarget target{};
        if (!GameProjectMaterialFxBridgeAccess::Resolve(*this, target)) {
            return false;
        }
        bool native = false;
        const bool succeeded = target.component != nullptr
                                   ? target.component->GetToggle(std::string(key), native)
                                   : target.presentation != nullptr &&
                                         target.presentation->TryGetMaterialFxToggle(target.ownerObjectHandle,
                                                                                     target.instanceId, key, native);
        if (succeeded) {
            outValue = native ? 1u : 0u;
        }
        return succeeded;
    } catch (...) {
        outValue = 0u;
        return false;
    }
}

} // namespace HIKARI
