#include "HIKARI/Project/SpriteMaterialView.h"
#include "Core/Numeric/HIKARI_NonZero.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>

#include "Assets/Sprite/HIKARI_SpriteMaterialStore.h"
#include "Core/Text/HIKARI_Utf8.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectBindingConversion.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"
#include "Scene/Components/Rendering/WorldSprite/HIKARI_WorldSpriteComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {
namespace {

struct ResolvedSpriteMaterialTarget {
    World* world = nullptr;
    WorldSpriteComponent* component = nullptr;
};

template <size_t Capacity>
bool CopyText(std::string_view source, char (&destination)[Capacity]) noexcept {
    if (source.size() >= Capacity || source.find('\0') != std::string_view::npos || !TEXT::IsValidUtf8(source)) {
        return false;
    }
    std::copy(source.begin(), source.end(), destination);
    destination[source.size()] = '\0';
    return true;
}

} // namespace

struct GameProjectSpriteMaterialBridgeAccess {
    static GameProjectWorldResult MakeResult(const SpriteMaterialView& view, GameProjectWorldResultCode code) noexcept {
        return PROJECT::INTERNAL::MakeWorldResult(code, view.object_);
    }

    static GameProjectWorldResult Resolve(const SpriteMaterialView& view,
                                          ResolvedSpriteMaterialTarget& outTarget) noexcept {
        outTarget = {};
        try {
            RuntimeWorldAccessResult access{};
            World* world = World::ResolveRuntimeWorld(view.object_.world.value, &access);
            if (world == nullptr) {
                return PROJECT::INTERNAL::MakeWorldAccessFailure(access);
            }
            if (!world->IsOwnerThread()) {
                return MakeResult(view, GameProjectWorldResultCode::WrongThread);
            }
            if (!PROJECT::INTERNAL::IsObjectHandleForWorld(view.object_, *world)) {
                return MakeResult(view, GameProjectWorldResultCode::InvalidObject);
            }
            if (GameProjectSdkAccess::IsWorldClosing(*world)) {
                return MakeResult(view, GameProjectWorldResultCode::WorldClosing);
            }

            GameObject* object = world->FindObject(PROJECT::INTERNAL::DecodeRuntimeHandle(view.object_));
            if (object == nullptr) {
                return MakeResult(view, GameProjectWorldResultCode::InvalidObject);
            }
            WorldSpriteComponent* component = object->GetComponent<WorldSpriteComponent>();
            if (component == nullptr || component->IsCanvasSourceOnly()) {
                return MakeResult(view, GameProjectWorldResultCode::Unsupported);
            }
            outTarget = {world, component};
            return MakeResult(view, GameProjectWorldResultCode::Success);
        } catch (...) {
            outTarget = {};
            return MakeResult(view, GameProjectWorldResultCode::InternalError);
        }
    }
};

SpriteMaterialView ObjectView::GetSpriteMaterial() const noexcept { return SpriteMaterialView(handle_); }

bool SpriteMaterialView::IsValid() const noexcept {
    ResolvedSpriteMaterialTarget target{};
    return GameProjectSpriteMaterialBridgeAccess::Resolve(*this, target).Succeeded();
}

GameProjectWorldResult SpriteMaterialView::BindAsset(std::string_view materialAssetGuid) const noexcept {
    ResolvedSpriteMaterialTarget target{};
    GameProjectWorldResult result = GameProjectSpriteMaterialBridgeAccess::Resolve(*this, target);
    if (!result.Succeeded()) {
        return result;
    }
    if (materialAssetGuid.empty() || materialAssetGuid.size() >= kGameProjectSpriteMaterialTextCapacity ||
        materialAssetGuid.find('\0') != std::string_view::npos) {
        return GameProjectSpriteMaterialBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::InvalidRequest);
    }

    try {
        SPRITE::SpriteMaterialStore* store = target.world->Services().Find<SPRITE::SpriteMaterialStore>();
        if (store == nullptr) {
            return GameProjectSpriteMaterialBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::Unsupported);
        }
        const AssetGuid guid{std::string(materialAssetGuid)};
        if (!guid.IsValid()) {
            return GameProjectSpriteMaterialBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::InvalidRequest);
        }
        std::string error{};
        const std::shared_ptr<const SPRITE::SpriteMaterialBinding> binding = store->Resolve(guid, error);
        if (binding == nullptr || !target.component->BindMaterialAsset(guid, binding)) {
            return GameProjectSpriteMaterialBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::InvalidAsset);
        }
        return GameProjectSpriteMaterialBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::Success);
    } catch (...) {
        return GameProjectSpriteMaterialBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::InternalError);
    }
}

GameProjectWorldResult SpriteMaterialView::ClearBinding() const noexcept {
    ResolvedSpriteMaterialTarget target{};
    GameProjectWorldResult result = GameProjectSpriteMaterialBridgeAccess::Resolve(*this, target);
    if (!result.Succeeded()) {
        return result;
    }
    try {
        target.component->ClearMaterialAsset();
        return GameProjectSpriteMaterialBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::Success);
    } catch (...) {
        return GameProjectSpriteMaterialBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::InternalError);
    }
}

GameProjectWorldResult SpriteMaterialView::SetEnabled(bool enabled) const noexcept {
    ResolvedSpriteMaterialTarget target{};
    GameProjectWorldResult result = GameProjectSpriteMaterialBridgeAccess::Resolve(*this, target);
    if (!result.Succeeded()) {
        return result;
    }
    try {
        return GameProjectSpriteMaterialBridgeAccess::MakeResult(
            *this, target.component->SetMaterialEnabled(enabled) ? GameProjectWorldResultCode::Success
                                                                 : GameProjectWorldResultCode::InvalidRequest);
    } catch (...) {
        return GameProjectSpriteMaterialBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::InternalError);
    }
}

GameProjectWorldResult SpriteMaterialView::SetParameter(std::string_view key,
                                                        const GameProjectBindingValue& value) const noexcept {
    ResolvedSpriteMaterialTarget target{};
    GameProjectWorldResult result = GameProjectSpriteMaterialBridgeAccess::Resolve(*this, target);
    if (!result.Succeeded()) {
        return result;
    }
    if (key.empty() || key.size() > kGameProjectSpriteMaterialTextCapacity - 1u ||
        key.find('\0') != std::string_view::npos ||
        PROJECT::DETAIL::ToProjectBindingType(PROJECT::DETAIL::ToRuntimeBindingType(value.type)) != value.type) {
        return GameProjectSpriteMaterialBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::InvalidRequest);
    }
    try {
        const BINDING::Value runtimeValue = PROJECT::DETAIL::ToRuntimeValue(value);
        return GameProjectSpriteMaterialBridgeAccess::MakeResult(
            *this, target.component->SetMaterialParameter(key, runtimeValue)
                       ? GameProjectWorldResultCode::Success
                       : GameProjectWorldResultCode::InvalidRequest);
    } catch (...) {
        return GameProjectSpriteMaterialBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::InternalError);
    }
}

GameProjectWorldResult SpriteMaterialView::ResetParameter(std::string_view key) const noexcept {
    ResolvedSpriteMaterialTarget target{};
    GameProjectWorldResult result = GameProjectSpriteMaterialBridgeAccess::Resolve(*this, target);
    if (!result.Succeeded()) {
        return result;
    }
    if (key.empty() || key.size() > kGameProjectSpriteMaterialTextCapacity - 1u ||
        key.find('\0') != std::string_view::npos) {
        return GameProjectSpriteMaterialBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::InvalidRequest);
    }
    try {
        return GameProjectSpriteMaterialBridgeAccess::MakeResult(
            *this, target.component->ResetMaterialParameter(key) ? GameProjectWorldResultCode::Success
                                                                 : GameProjectWorldResultCode::InvalidRequest);
    } catch (...) {
        return GameProjectSpriteMaterialBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::InternalError);
    }
}

GameProjectWorldResult SpriteMaterialView::ResetParameters() const noexcept {
    ResolvedSpriteMaterialTarget target{};
    GameProjectWorldResult result = GameProjectSpriteMaterialBridgeAccess::Resolve(*this, target);
    if (!result.Succeeded()) {
        return result;
    }
    try {
        return GameProjectSpriteMaterialBridgeAccess::MakeResult(
            *this, target.component->ResetMaterialParameters() ? GameProjectWorldResultCode::Success
                                                               : GameProjectWorldResultCode::InvalidRequest);
    } catch (...) {
        return GameProjectSpriteMaterialBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::InternalError);
    }
}

GameProjectWorldResult SpriteMaterialView::TryGetState(GameProjectSpriteMaterialState& outState) const noexcept {
    if (outState.structSize != kGameProjectSpriteMaterialStateV1Size ||
        outState.version != kGameProjectSpriteMaterialStateVersion || NUMERIC::AnyNonZero(outState.reserved)) {
        return GameProjectSpriteMaterialBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::InvalidRequest);
    }
    ResolvedSpriteMaterialTarget target{};
    GameProjectWorldResult result = GameProjectSpriteMaterialBridgeAccess::Resolve(*this, target);
    if (!result.Succeeded()) {
        return result;
    }
    try {
        GameProjectSpriteMaterialState state{};
        const AssetGuid& guid = target.component->GetMaterialAssetGuid();
        if (guid.IsValid() && !CopyText(guid.value, state.materialAssetGuid)) {
            return GameProjectSpriteMaterialBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::InternalError);
        }
        const size_t parameterCount = target.component->GetMaterialParameterCount();
        if (parameterCount > ASSETS::SHADER::kShaderParameterSlotCount) {
            return GameProjectSpriteMaterialBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::InternalError);
        }
        state.enabled = target.component->IsMaterialEnabled() ? 1u : 0u;
        state.parameterCount = static_cast<uint32_t>(parameterCount);
        state.stateRevision = target.component->GetMaterialStateRevision();
        outState = state;
        return result;
    } catch (...) {
        return GameProjectSpriteMaterialBridgeAccess::MakeResult(*this, GameProjectWorldResultCode::InternalError);
    }
}

bool SpriteMaterialView::TryGetParameter(std::string_view key, GameProjectBindingValue& outValue) const noexcept {
    if (key.empty() || key.size() > kGameProjectSpriteMaterialTextCapacity - 1u ||
        key.find('\0') != std::string_view::npos) {
        return false;
    }
    ResolvedSpriteMaterialTarget target{};
    if (!GameProjectSpriteMaterialBridgeAccess::Resolve(*this, target).Succeeded()) {
        return false;
    }
    try {
        BINDING::Value value{};
        if (!target.component->TryGetMaterialParameter(key, value)) {
            return false;
        }
        const GameProjectBindingValue converted = PROJECT::DETAIL::ToProjectValue(value);
        outValue = converted;
        return true;
    } catch (...) {
        return false;
    }
}

bool SpriteMaterialView::TryGetParameterInfo(uint32_t index,
                                             GameProjectSpriteMaterialParameterInfo& outInfo) const noexcept {
    if (outInfo.structSize != kGameProjectSpriteMaterialParameterInfoV1Size ||
        outInfo.version != kGameProjectSpriteMaterialParameterInfoVersion || NUMERIC::AnyNonZero(outInfo.reserved) ||
        outInfo.reserved2 != 0u) {
        return false;
    }
    ResolvedSpriteMaterialTarget target{};
    if (!GameProjectSpriteMaterialBridgeAccess::Resolve(*this, target).Succeeded()) {
        return false;
    }
    try {
        ASSETS::SHADER::ShaderParameterDefinition definition{};
        if (index >= target.component->GetMaterialParameterCount() ||
            !target.component->TryGetMaterialParameterInfo(index, definition) ||
            definition.slot >= ASSETS::SHADER::kShaderParameterSlotCount) {
            return false;
        }

        GameProjectSpriteMaterialParameterInfo info{};
        if (!CopyText(definition.key, info.key) || !CopyText(definition.label, info.label) ||
            !CopyText(definition.group, info.group)) {
            return false;
        }
        info.slot = definition.slot;
        info.type = PROJECT::DETAIL::ToProjectBindingType(definition.defaultValue.type);
        info.hasRange = definition.hasRange ? 1u : 0u;
        info.minimum = definition.minimum;
        info.maximum = definition.maximum;
        info.step = definition.step;
        info.defaultValue = PROJECT::DETAIL::ToProjectValue(definition.defaultValue);
        outInfo = info;
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace HIKARI
