#include "HIKARI/Project/WorldView.h"
#include "HIKARI/Project/TerrainView.h"
#include "HIKARI/Project/PresentationTypes.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Scene/Components/Lighting/HIKARI_LightComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/Components/Rendering/WorldSprite/HIKARI_WorldSpriteComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceQueryService.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

namespace {

GameProjectLightType ToProjectLightType(LightComponentType type) noexcept {
    switch (type) {
    case LightComponentType::Directional: return GameProjectLightType::Directional;
    case LightComponentType::Spot: return GameProjectLightType::Spot;
    case LightComponentType::Point:
    default: return GameProjectLightType::Point;
    }
}

bool HasNonZeroLightPatchReserved(const GameProjectLightPatch& patch) noexcept {
    if (patch.reserved0[0] != 0u || patch.reserved0[1] != 0u || patch.reserved0[2] != 0u) {
        return true;
    }
    return patch.reserved1 != 0u;
}

bool HasNonZeroWorldSpritePatchReserved(const GameProjectWorldSpritePatch& patch) noexcept {
    if (patch.reserved0 != 0u) {
        return true;
    }
    for (uint8_t value : patch.reserved1) {
        if (value != 0u) {
            return true;
        }
    }
    return false;
}

} // namespace

static_assert(static_cast<uint8_t>(WorldCommandKind::SetParent) ==
              static_cast<uint8_t>(GameProjectWorldCommandKind::SetParent));
static_assert(static_cast<uint8_t>(WorldCommandPhase::SystemPostRender) ==
              static_cast<uint8_t>(GameProjectWorldCommandPhase::SystemPostRender));
static_assert(static_cast<uint32_t>(GameProjectWorldSpriteFacingMode::FixedYAxis) ==
              static_cast<uint8_t>(WorldSpriteComponent::FacingMode::FixedYAxis));
static_assert(static_cast<uint32_t>(GameProjectWorldSpriteAlphaMode::Blend) ==
              static_cast<uint8_t>(WorldSpriteComponent::AlphaMode::Blend));

ObjectView GameProjectSdkAccess::MakeObjectView(GameObject& object) noexcept {
    World* world = object.GetWorld();
    return world != nullptr ? ObjectView(PROJECT::INTERNAL::EncodeRuntimeHandle(*world, object.GetRuntimeHandle()))
                            : ObjectView{};
}

WorldView GameProjectSdkAccess::MakeWorldView(World& world) noexcept {
    return WorldView(GameProjectWorldHandle{world.GetRuntimeIdentity()});
}

GameObject* GameProjectSdkAccess::GetObject(const ObjectView& view, RuntimeWorldAccessResult* outAccess) noexcept {
    World* world = World::ResolveRuntimeWorld(view.handle_.world.value, outAccess);
    return world != nullptr ? world->FindObject(PROJECT::INTERNAL::DecodeRuntimeHandle(view.handle_)) : nullptr;
}

World* GameProjectSdkAccess::GetWorld(const WorldView& view, RuntimeWorldAccessResult* outAccess) noexcept {
    return World::ResolveRuntimeWorld(view.world_.value, outAccess);
}

bool ObjectView::IsValid() const noexcept { return GameProjectSdkAccess::GetObject(*this) != nullptr; }

std::string_view ObjectView::GetName() const noexcept {
    const GameObject* object = GameProjectSdkAccess::GetObject(*this);
    return object != nullptr ? std::string_view(object->GetName()) : std::string_view{};
}

uint64_t ObjectView::GetSceneObjectId() const noexcept {
    const GameObject* object = GameProjectSdkAccess::GetObject(*this);
    return object != nullptr ? object->GetDocumentId().value : 0u;
}

ObjectView ObjectView::GetParent() const noexcept {
    GameObject* object = GameProjectSdkAccess::GetObject(*this);
    GameObject* parent = object != nullptr ? object->GetParent() : nullptr;
    return parent != nullptr ? GameProjectSdkAccess::MakeObjectView(*parent) : ObjectView{};
}

bool ObjectView::TryGetRuntimeLightState(GameProjectLightState& outState) const noexcept {
    const uint32_t bytes = outState.structSize;
    const uint32_t version = outState.version;
    if (version != kGameProjectLightStateVersion || bytes != kGameProjectLightStateV2Size)
        return false;
    GameProjectLightState snapshot{};
    snapshot.structSize = bytes;
    snapshot.version = version;
    // 失敗時も一貫した空の値を返す。
    std::memcpy(&outState, &snapshot, bytes);
    try {
        GameObject* object = GameProjectSdkAccess::GetObject(*this);
        World* world = object != nullptr ? object->GetWorld() : nullptr;
        if (object == nullptr || world == nullptr || !world->IsOwnerThread() ||
            GameProjectSdkAccess::IsWorldClosing(*world)) {
            return false;
        }
        const LightComponent* light = object->GetComponent<LightComponent>();
        if (light == nullptr) {
            return false;
        }
        const MATH::Vec3& color = light->GetColor();
        snapshot.type = ToProjectLightType(light->GetLightType());
        snapshot.enabled = light->IsEnabled() ? 1u : 0u;
        snapshot.color = {color.x, color.y, color.z};
        snapshot.intensity = light->GetIntensity();
        snapshot.range = light->GetRange();
        snapshot.innerConeAngleDegrees = light->GetInnerConeAngleDegrees();
        snapshot.outerConeAngleDegrees = light->GetOuterConeAngleDegrees();
        snapshot.angularFalloff = light->GetAngularFalloff();
        snapshot.distanceFalloff = static_cast<GameProjectLightFalloff>(light->GetDistanceFalloff());
        snapshot.priority = light->GetPriority();
        snapshot.stateRevision = light->GetStateRevision();
        snapshot.sourceRadius = light->GetSourceRadius();
        snapshot.castsShadows = light->CastsShadows() ? 1u : 0u;
        std::memcpy(&outState, &snapshot, bytes);
        return true;
    } catch (...) {
        return false;
    }
}

GameProjectWorldResult ObjectView::ApplyRuntimeLightPatch(const GameProjectLightPatch& patch) const noexcept {
    RuntimeWorldAccessResult access{};
    World* world = World::ResolveRuntimeWorld(handle_.world.value, &access);
    if (world == nullptr) {
        return PROJECT::INTERNAL::MakeWorldAccessFailure(access);
    }
    if (!world->IsOwnerThread()) {
        return PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::WrongThread, handle_);
    }
    if (!PROJECT::INTERNAL::IsObjectHandleForWorld(handle_, *world)) {
        return PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::InvalidObject, handle_);
    }
    if (GameProjectSdkAccess::IsWorldClosing(*world)) {
        return PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::WorldClosing, handle_);
    }
    const uint32_t mask = patch.fieldMask;
    if (patch.structSize != kGameProjectLightPatchV1Size || patch.version != kGameProjectLightPatchVersion ||
        mask == 0u || (mask & ~kGameProjectLightPatchKnownFields) != 0u || HasNonZeroLightPatchReserved(patch) ||
        ((mask & GameProjectLightPatchFields(GameProjectLightPatchField::Enabled)) != 0u && patch.enabled > 1u)) {
        return PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::InvalidRequest, handle_);
    }

    GameObject* object = world->FindObject(PROJECT::INTERNAL::DecodeRuntimeHandle(handle_));
    LightComponent* light = object != nullptr ? object->GetComponent<LightComponent>() : nullptr;
    if (light == nullptr) {
        return PROJECT::INTERNAL::MakeWorldResult(object == nullptr ? GameProjectWorldResultCode::InvalidObject
                                                                    : GameProjectWorldResultCode::Unsupported,
                                                  handle_);
    }

    const auto Selected = [mask](GameProjectLightPatchField field) {
        return (mask & GameProjectLightPatchFields(field)) != 0u;
    };
    if ((Selected(GameProjectLightPatchField::Color) &&
         (!std::isfinite(patch.color.x) || !std::isfinite(patch.color.y) || !std::isfinite(patch.color.z))) ||
        (Selected(GameProjectLightPatchField::Intensity) && !std::isfinite(patch.intensity)) ||
        (Selected(GameProjectLightPatchField::Range) && !std::isfinite(patch.range)) ||
        (Selected(GameProjectLightPatchField::SpotCone) &&
         (!std::isfinite(patch.innerConeAngleDegrees) || !std::isfinite(patch.outerConeAngleDegrees))) ||
        (Selected(GameProjectLightPatchField::AngularFalloff) && !std::isfinite(patch.angularFalloff)) ||
        (Selected(GameProjectLightPatchField::CastShadows) && patch.castsShadows > 1u) ||
        (Selected(GameProjectLightPatchField::SourceRadius) && !std::isfinite(patch.sourceRadius)) ||
        (Selected(GameProjectLightPatchField::DistanceFalloff) &&
         patch.distanceFalloff != GameProjectLightFalloff::Range &&
         patch.distanceFalloff != GameProjectLightFalloff::InverseSquare)) {
        return PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::InvalidRequest, handle_);
    }
    if (((Selected(GameProjectLightPatchField::Range) || Selected(GameProjectLightPatchField::DistanceFalloff) ||
          Selected(GameProjectLightPatchField::SourceRadius) || Selected(GameProjectLightPatchField::CastShadows)) &&
         light->GetLightType() == LightComponentType::Directional) ||
        ((Selected(GameProjectLightPatchField::SpotCone) || Selected(GameProjectLightPatchField::AngularFalloff)) &&
         light->GetLightType() != LightComponentType::Spot)) {
        return PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::Unsupported, handle_);
    }

    const MATH::Vec3 color{
        std::clamp(patch.color.x, 0.0f, 1.0f),
        std::clamp(patch.color.y, 0.0f, 1.0f),
        std::clamp(patch.color.z, 0.0f, 1.0f),
    };
    const float intensity = std::clamp(patch.intensity, 0.0f, 100.0f);
    const float range = std::clamp(patch.range, 0.05f, 1000.0f);
    const float innerCone = std::clamp(patch.innerConeAngleDegrees, 0.1f, 89.0f);
    const float outerCone = std::clamp(patch.outerConeAngleDegrees, innerCone, 89.0f);
    const float angularFalloff = std::clamp(patch.angularFalloff, 0.05f, 8.0f);

    try {
        if (Selected(GameProjectLightPatchField::Enabled)) {
            light->SetEnabled(patch.enabled != 0u);
        }
        if (Selected(GameProjectLightPatchField::Color)) {
            light->SetColor(color);
        }
        if (Selected(GameProjectLightPatchField::Intensity)) {
            light->SetIntensity(intensity);
        }
        if (Selected(GameProjectLightPatchField::Range)) {
            light->SetRange(range);
        }
        if (Selected(GameProjectLightPatchField::SpotCone)) {
            light->SetSpotConeAnglesDegrees(innerCone, outerCone);
        }
        if (Selected(GameProjectLightPatchField::AngularFalloff)) {
            light->SetAngularFalloff(angularFalloff);
        }
        if (Selected(GameProjectLightPatchField::CastShadows)) {
            light->SetCastsShadows(patch.castsShadows != 0u);
        }
        if (Selected(GameProjectLightPatchField::SourceRadius)) {
            light->SetSourceRadius(patch.sourceRadius);
        }
        if (Selected(GameProjectLightPatchField::DistanceFalloff)) {
            light->SetDistanceFalloff(static_cast<RENDER3D::LIGHTING::LocalLightFalloff>(patch.distanceFalloff));
        }
        return PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::Success, handle_);
    } catch (...) {
        return PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::InternalError, handle_);
    }
}

bool ObjectView::TryGetRuntimeModelAppearance(GameProjectModelAppearance& value) const noexcept {
    value = {};
    try {
        auto* object = GameProjectSdkAccess::GetObject(*this);
        auto* world = object ? object->GetWorld() : nullptr;
        if (!world || !world->IsOwnerThread() || GameProjectSdkAccess::IsWorldClosing(*world))
            return false;
        const auto* model = object->GetComponent<ModelComponent>();
        if (!model || model->GetAssetId().size() >= sizeof(value.assetId) ||
            model->GetMaterialSetAssetGuid().value.size() >= sizeof(value.materialSetAssetGuid))
            return false;
        std::copy(model->GetAssetId().begin(), model->GetAssetId().end(), value.assetId);
        const auto& material = model->GetMaterialSetAssetGuid().value;
        std::copy(material.begin(), material.end(), value.materialSetAssetGuid);
        return true;
    } catch (...) {
        return false;
    }
}
bool ObjectView::ApplyRuntimeModelAppearance(const GameProjectModelAppearance& value) const noexcept {
    try {
        auto* object = GameProjectSdkAccess::GetObject(*this);
        auto* world = object ? object->GetWorld() : nullptr;
        if (!world || !world->IsOwnerThread() || GameProjectSdkAccess::IsWorldClosing(*world) ||
            value.structSize != sizeof(value) || value.version != 1)
            return false;
        const auto* end = std::find(std::begin(value.assetId), std::end(value.assetId), '\0');
        const auto* matEnd =
            std::find(std::begin(value.materialSetAssetGuid), std::end(value.materialSetAssetGuid), '\0');
        if (end == std::end(value.assetId) || matEnd == std::end(value.materialSetAssetGuid) || end == value.assetId)
            return false;
        auto* model = object->GetComponent<ModelComponent>();
        if (!model)
            return false;
        std::string asset(value.assetId, end), material(value.materialSetAssetGuid, matEnd);
        if (model->GetAssetId() != asset)
            model->SetAssetId(std::move(asset));
        if (model->GetMaterialSetAssetGuid().value != material)
            model->SetMaterialSetAssetGuid(AssetGuid{std::move(material)});
        return true;
    } catch (...) {
        return false;
    }
}
bool ObjectView::SetRuntimeWorldSpriteTexture(std::string_view guid) const noexcept {
    try {
        auto* object = GameProjectSdkAccess::GetObject(*this);
        auto* world = object ? object->GetWorld() : nullptr;
        if (!world || !world->IsOwnerThread() || GameProjectSdkAccess::IsWorldClosing(*world) || guid.size() > 128 ||
            guid.find('\0') != std::string_view::npos)
            return false;
        auto* sprite = object->GetComponent<WorldSpriteComponent>();
        if (!sprite)
            return false;
        sprite->SetTextureAssetGuid(AssetGuid{std::string(guid)});
        return true;
    } catch (...) {
        return false;
    }
}

bool ObjectView::TryGetRuntimeWorldSpriteState(GameProjectWorldSpriteState& outState) const noexcept {
    outState = {};
    try {
        GameObject* object = GameProjectSdkAccess::GetObject(*this);
        World* world = object != nullptr ? object->GetWorld() : nullptr;
        if (object == nullptr || world == nullptr || !world->IsOwnerThread() ||
            GameProjectSdkAccess::IsWorldClosing(*world)) {
            return false;
        }
        const WorldSpriteComponent* sprite = object->GetComponent<WorldSpriteComponent>();
        if (sprite == nullptr) {
            return false;
        }
        const MATH::Vec2& size = sprite->GetSize();
        const MATH::Vec3& tint = sprite->GetTint();
        const MATH::Vec2 pivot = sprite->GetEffectivePivot();
        outState.stateRevision = sprite->GetStateRevision();
        outState.size = {size.x, size.y};
        outState.tint = {tint.x, tint.y, tint.z};
        outState.opacity = sprite->GetOpacity();
        outState.pivot = {pivot.x, pivot.y};
        outState.alphaCutoff = sprite->GetAlphaCutoff();
        outState.transparentSortPriority = sprite->GetTransparentSortPriority();
        outState.facing = static_cast<GameProjectWorldSpriteFacingMode>(sprite->GetFacingMode());
        outState.alphaMode = static_cast<GameProjectWorldSpriteAlphaMode>(sprite->GetAlphaMode());
        outState.visible = sprite->IsVisible() ? 1u : 0u;
        outState.preserveAspect = sprite->GetPreserveAspect() ? 1u : 0u;
        outState.flipX = sprite->GetFlipX() ? 1u : 0u;
        return true;
    } catch (...) {
        outState = {};
        return false;
    }
}

GameProjectWorldResult ObjectView::ApplyRuntimeWorldSpritePatch(
    const GameProjectWorldSpritePatch& patch) const noexcept {
    RuntimeWorldAccessResult access{};
    World* world = World::ResolveRuntimeWorld(handle_.world.value, &access);
    if (world == nullptr) {
        return PROJECT::INTERNAL::MakeWorldAccessFailure(access);
    }
    if (!world->IsOwnerThread()) {
        return PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::WrongThread, handle_);
    }
    if (!PROJECT::INTERNAL::IsObjectHandleForWorld(handle_, *world)) {
        return PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::InvalidObject, handle_);
    }
    if (GameProjectSdkAccess::IsWorldClosing(*world)) {
        return PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::WorldClosing, handle_);
    }

    const uint32_t mask = patch.fieldMask;
    const auto Selected = [mask](GameProjectWorldSpritePatchField field) noexcept {
        return (mask & GameProjectWorldSpritePatchFields(field)) != 0u;
    };
    if (patch.structSize != kGameProjectWorldSpritePatchV1Size ||
        patch.version != kGameProjectWorldSpritePatchVersion || mask == 0u ||
        (mask & ~kGameProjectWorldSpritePatchKnownFields) != 0u || HasNonZeroWorldSpritePatchReserved(patch) ||
        (Selected(GameProjectWorldSpritePatchField::Visible) && patch.visible > 1u) ||
        (Selected(GameProjectWorldSpritePatchField::PreserveAspect) && patch.preserveAspect > 1u) ||
        (Selected(GameProjectWorldSpritePatchField::Facing) &&
         patch.facing >= GameProjectWorldSpriteFacingMode::Count) ||
        (Selected(GameProjectWorldSpritePatchField::AlphaMode) &&
         patch.alphaMode >= GameProjectWorldSpriteAlphaMode::Count) ||
        (Selected(GameProjectWorldSpritePatchField::FlipX) && patch.flipX > 1u) ||
        (Selected(GameProjectWorldSpritePatchField::Size) &&
         (!std::isfinite(patch.size.x) || !std::isfinite(patch.size.y))) ||
        (Selected(GameProjectWorldSpritePatchField::Tint) &&
         (!std::isfinite(patch.tint.x) || !std::isfinite(patch.tint.y) || !std::isfinite(patch.tint.z))) ||
        (Selected(GameProjectWorldSpritePatchField::Opacity) && !std::isfinite(patch.opacity)) ||
        (Selected(GameProjectWorldSpritePatchField::Pivot) &&
         (!std::isfinite(patch.pivot.x) || !std::isfinite(patch.pivot.y))) ||
        (Selected(GameProjectWorldSpritePatchField::AlphaCutoff) && !std::isfinite(patch.alphaCutoff))) {
        return PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::InvalidRequest, handle_);
    }

    GameObject* object = world->FindObject(PROJECT::INTERNAL::DecodeRuntimeHandle(handle_));
    WorldSpriteComponent* sprite = object != nullptr ? object->GetComponent<WorldSpriteComponent>() : nullptr;
    if (sprite == nullptr) {
        return PROJECT::INTERNAL::MakeWorldResult(object == nullptr ? GameProjectWorldResultCode::InvalidObject
                                                                    : GameProjectWorldResultCode::Unsupported,
                                                  handle_);
    }
    if (sprite->IsCanvasSourceOnly() && Selected(GameProjectWorldSpritePatchField::FlipX)) {
        return PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::Unsupported, handle_);
    }

    try {
        WorldSpriteComponent::RuntimeState next = sprite->GetRuntimeState();
        if (Selected(GameProjectWorldSpritePatchField::Visible)) {
            next.visible = patch.visible != 0u;
        }
        if (Selected(GameProjectWorldSpritePatchField::FlipX)) {
            next.flipX = patch.flipX != 0u;
        }
        if (Selected(GameProjectWorldSpritePatchField::Size)) {
            next.size = {patch.size.x, patch.size.y};
            next.sizeExplicit = true;
        }
        if (Selected(GameProjectWorldSpritePatchField::PreserveAspect)) {
            next.preserveAspect = patch.preserveAspect != 0u;
        }
        if (Selected(GameProjectWorldSpritePatchField::Facing)) {
            next.facing = static_cast<WorldSpriteComponent::FacingMode>(patch.facing);
        }
        if (Selected(GameProjectWorldSpritePatchField::Tint)) {
            next.tint = {patch.tint.x, patch.tint.y, patch.tint.z};
        }
        if (Selected(GameProjectWorldSpritePatchField::Opacity)) {
            next.opacity = patch.opacity;
        }
        if (Selected(GameProjectWorldSpritePatchField::Pivot)) {
            next.pivot = {patch.pivot.x, patch.pivot.y};
            next.pivotExplicit = true;
        }
        if (Selected(GameProjectWorldSpritePatchField::AlphaMode)) {
            next.alphaMode = static_cast<WorldSpriteComponent::AlphaMode>(patch.alphaMode);
        }
        if (Selected(GameProjectWorldSpritePatchField::AlphaCutoff)) {
            next.alphaCutoff = patch.alphaCutoff;
        }
        if (Selected(GameProjectWorldSpritePatchField::TransparentSortPriority)) {
            next.transparentSortPriority = patch.transparentSortPriority;
        }
        (void)sprite->ApplyRuntimeState(next);
        return PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::Success, handle_);
    } catch (...) {
        return PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::InternalError, handle_);
    }
}

bool ObjectView::SetRuntimeMaterialColorOverride(uint32_t materialIndex, GameProjectFloat3 baseColor, float opacity,
                                                 GameProjectFloat3 emissiveColor,
                                                 float emissiveStrength) const noexcept {
    try {
        GameObject* object = GameProjectSdkAccess::GetObject(*this);
        World* world = object != nullptr ? object->GetWorld() : nullptr;
        if (object == nullptr || world == nullptr || !world->IsOwnerThread() ||
            GameProjectSdkAccess::IsWorldClosing(*world) || !std::isfinite(baseColor.x) ||
            !std::isfinite(baseColor.y) || !std::isfinite(baseColor.z) || !std::isfinite(opacity) ||
            !std::isfinite(emissiveColor.x) || !std::isfinite(emissiveColor.y) || !std::isfinite(emissiveColor.z) ||
            !std::isfinite(emissiveStrength)) {
            return false;
        }
        ModelComponent* model = object->GetComponent<ModelComponent>();
        Material* material = model != nullptr ? model->GetRuntimeMaterialOverride(materialIndex) : nullptr;
        if (material == nullptr) {
            return false;
        }
        const uint64_t revisionBefore = material->GetRevision();
        const auto Color = [](float value) { return std::clamp(value, 0.0f, 64.0f); };
        material->SetBaseColor({
            Color(baseColor.x),
            Color(baseColor.y),
            Color(baseColor.z),
            std::clamp(opacity, 0.0f, 1.0f),
        });
        material->SetEmissiveFactor({
            Color(emissiveColor.x),
            Color(emissiveColor.y),
            Color(emissiveColor.z),
        });
        material->SetEmissiveStrength(std::clamp(emissiveStrength, 0.0f, 64.0f));
        if (material->GetRevision() != revisionBefore) {
            world->MarkRenderObjectDirty(object);
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool ObjectView::SetModelNodeVisibility(std::string_view nodeName, uint8_t visible,
                                        GameProjectModelNodeVisibilityScope scope) const noexcept {
    try {
        GameObject* object = GameProjectSdkAccess::GetObject(*this);
        World* world = object != nullptr ? object->GetWorld() : nullptr;
        if (object == nullptr || world == nullptr || !world->IsOwnerThread() ||
            GameProjectSdkAccess::IsWorldClosing(*world) || nodeName.empty() ||
            nodeName.size() > kGameProjectModelNodeNameMaximumBytes || visible > 1u ||
            scope >= GameProjectModelNodeVisibilityScope::Count) {
            return false;
        }
        ModelComponent* model = object->GetComponent<ModelComponent>();
        return model != nullptr &&
               model->SetRuntimeNodeVisibilityOverride(
                   nodeName, visible != 0u, scope == GameProjectModelNodeVisibilityScope::NodeAndDescendants);
    } catch (...) {
        return false;
    }
}

bool ObjectView::ClearModelNodeVisibilityOverride(std::string_view nodeName) const noexcept {
    try {
        GameObject* object = GameProjectSdkAccess::GetObject(*this);
        World* world = object != nullptr ? object->GetWorld() : nullptr;
        if (object == nullptr || world == nullptr || !world->IsOwnerThread() ||
            GameProjectSdkAccess::IsWorldClosing(*world) || nodeName.empty() ||
            nodeName.size() > kGameProjectModelNodeNameMaximumBytes) {
            return false;
        }
        ModelComponent* model = object->GetComponent<ModelComponent>();
        if (model == nullptr) {
            return false;
        }
        (void)model->ClearRuntimeNodeVisibilityOverride(nodeName);
        return true;
    } catch (...) {
        return false;
    }
}

bool ObjectView::ClearModelNodeVisibilityOverrides() const noexcept {
    try {
        GameObject* object = GameProjectSdkAccess::GetObject(*this);
        World* world = object != nullptr ? object->GetWorld() : nullptr;
        if (object == nullptr || world == nullptr || !world->IsOwnerThread() ||
            GameProjectSdkAccess::IsWorldClosing(*world)) {
            return false;
        }
        ModelComponent* model = object->GetComponent<ModelComponent>();
        if (model == nullptr) {
            return false;
        }
        model->ClearRuntimeNodeVisibilityOverrides();
        return true;
    } catch (...) {
        return false;
    }
}

bool WorldView::IsValid() const noexcept { return GameProjectSdkAccess::GetWorld(*this) != nullptr; }

ObjectView WorldView::FindObject(GameProjectObjectHandle handle) const noexcept {
    World* world = GameProjectSdkAccess::GetWorld(*this);
    if (world == nullptr || !PROJECT::INTERNAL::IsObjectHandleForWorld(handle, *world)) {
        return {};
    }
    GameObject* object = world->FindObject(PROJECT::INTERNAL::DecodeRuntimeHandle(handle));
    return object != nullptr ? GameProjectSdkAccess::MakeObjectView(*object) : ObjectView{};
}

ObjectView WorldView::FindSceneObject(uint64_t sceneObjectId) const noexcept {
    World* world = GameProjectSdkAccess::GetWorld(*this);
    if (world == nullptr || sceneObjectId == 0u) {
        return {};
    }
    GameObject* object = world->FindObject(SceneObjectId{sceneObjectId});
    return object != nullptr ? GameProjectSdkAccess::MakeObjectView(*object) : ObjectView{};
}

uint64_t WorldView::GetObjectCount() const noexcept {
    const World* world = GameProjectSdkAccess::GetWorld(*this);
    return world != nullptr ? static_cast<uint64_t>(world->GetTotalObjectCount()) : 0u;
}

bool WorldView::CanMutateStructure() const noexcept {
    const World* world = GameProjectSdkAccess::GetWorld(*this);
    return world != nullptr && world->CanMutateStructure();
}

GameProjectWorldResult WorldView::DestroyObject(GameProjectObjectHandle object) const noexcept {
    RuntimeWorldAccessResult access{};
    World* world = GameProjectSdkAccess::GetWorld(*this, &access);
    if (world == nullptr) {
        return PROJECT::INTERNAL::MakeWorldAccessFailure(access);
    }
    if (!PROJECT::INTERNAL::IsObjectHandleForWorld(object, *world)) {
        return PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::InvalidObject);
    }
    const RuntimeObjectHandle runtimeHandle = PROJECT::INTERNAL::DecodeRuntimeHandle(object);
    try {
        return PROJECT::INTERNAL::ToProjectCommandSubmission(*world, world->SubmitDestroy(runtimeHandle));
    } catch (...) {
        return PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::InternalError, object);
    }
}

GameProjectWorldResult WorldView::SetParent(GameProjectObjectHandle object, GameProjectObjectHandle parent,
                                            GameProjectParentMode mode) const noexcept {
    RuntimeWorldAccessResult access{};
    World* world = GameProjectSdkAccess::GetWorld(*this, &access);
    if (world == nullptr) {
        return PROJECT::INTERNAL::MakeWorldAccessFailure(access);
    }
    if (!PROJECT::INTERNAL::IsObjectHandleForWorld(object, *world)) {
        return PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::InvalidObject);
    }
    if (!parent.IsNull() && !PROJECT::INTERNAL::IsObjectHandleForWorld(parent, *world)) {
        return PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::InvalidParent, object);
    }

    WorldParentMode runtimeMode{};
    switch (mode) {
    case GameProjectParentMode::KeepLocal: runtimeMode = WorldParentMode::KeepLocal; break;
    case GameProjectParentMode::KeepWorld: runtimeMode = WorldParentMode::KeepWorld; break;
    default: return PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::InvalidRequest, object);
    }

    const RuntimeObjectHandle objectHandle = PROJECT::INTERNAL::DecodeRuntimeHandle(object);
    try {
        return PROJECT::INTERNAL::ToProjectCommandSubmission(
            *world, world->SubmitSetParent(objectHandle, PROJECT::INTERNAL::DecodeRuntimeHandle(parent), runtimeMode));
    } catch (...) {
        return PROJECT::INTERNAL::MakeWorldResult(GameProjectWorldResultCode::InternalError, object);
    }
}

uint32_t WorldView::GetCommandFailureCount() const noexcept {
    const World* world = GameProjectSdkAccess::GetWorld(*this);
    return world != nullptr ? world->GetCommandFailureCount() : 0u;
}

bool WorldView::ReadCommandFailure(uint32_t index, GameProjectWorldCommandFailure& outFailure) const noexcept {
    World* world = GameProjectSdkAccess::GetWorld(*this);
    if (world == nullptr) {
        return false;
    }
    WorldCommandFailure failure{};
    if (!world->ReadCommandFailure(index, failure)) {
        return false;
    }
    outFailure.commandToken = failure.token;
    outFailure.kind = static_cast<GameProjectWorldCommandKind>(failure.kind);
    outFailure.code = PROJECT::INTERNAL::ToProjectMutationResult(*world, failure.result, failure.object).code;
    outFailure.phase = static_cast<GameProjectWorldCommandPhase>(failure.phase);
    outFailure.reserved = 0u;
    outFailure.object = PROJECT::INTERNAL::EncodeRuntimeHandle(*world, failure.object);
    outFailure.parent = PROJECT::INTERNAL::EncodeRuntimeHandle(*world, failure.parent);
    return true;
}

uint32_t WorldView::GetCommandFailureOverflowCount() const noexcept {
    const World* world = GameProjectSdkAccess::GetWorld(*this);
    return world != nullptr ? world->GetCommandFailureOverflowCount() : 0u;
}

TerrainView GameProjectSdkAccess::MakeTerrainView(World& world) noexcept { return TerrainView(&world); }

TerrainView WorldView::GetTerrain() const noexcept {
    World* world = GameProjectSdkAccess::GetWorld(*this);
    return world != nullptr ? GameProjectSdkAccess::MakeTerrainView(*world) : TerrainView{};
}

bool TerrainView::IsTerrainSurfaceAt(GameProjectFloat3 worldPosition, float maximumVerticalDistance) const noexcept {
    try {
        if (world_ == nullptr || !std::isfinite(worldPosition.x) || !std::isfinite(worldPosition.y) ||
            !std::isfinite(worldPosition.z) || !std::isfinite(maximumVerticalDistance) ||
            maximumVerticalDistance < 0.0f || maximumVerticalDistance > 10.0f) {
            return false;
        }
        auto* world = static_cast<World*>(world_);
        TERRAIN::TerrainSurfaceQueryService* query = world->Services().Find<TERRAIN::TerrainSurfaceQueryService>();
        if (query == nullptr) {
            return false;
        }
        float height = 0.0f;
        MATH::Vec3 normal{};
        return query->SampleWorldHeight({worldPosition.x, worldPosition.y, worldPosition.z}, height, normal) &&
               std::abs(worldPosition.y - height) <= maximumVerticalDistance;
    } catch (...) {
        return false;
    }
}

} // namespace HIKARI
