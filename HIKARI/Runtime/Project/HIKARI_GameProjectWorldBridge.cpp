#include "HIKARI/Project/WorldView.h"
#include "HIKARI/Project/TerrainView.h"

#include <algorithm>
#include <cmath>

#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Scene/Components/Lighting/HIKARI_LightComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Sand/HIKARI_SandSurfaceQueryService.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

    namespace {

        GameProjectLightType ToProjectLightType(
            LightComponentType type) noexcept {
            switch (type) {
            case LightComponentType::Directional:
                return GameProjectLightType::Directional;
            case LightComponentType::Spot:
                return GameProjectLightType::Spot;
            case LightComponentType::Point:
            default:
                return GameProjectLightType::Point;
            }
        }

        bool HasNonZeroLightPatchReserved(
            const GameProjectLightPatch& patch) noexcept {
            if (patch.reserved0[0] != 0u || patch.reserved0[1] != 0u ||
                patch.reserved0[2] != 0u) {
                return true;
            }
            for (uint32_t value : patch.reserved1) {
                if (value != 0u) {
                    return true;
                }
            }
            return false;
        }

    } // namespace

    static_assert(
        static_cast<uint8_t>(WorldCommandKind::SetParent) ==
        static_cast<uint8_t>(GameProjectWorldCommandKind::SetParent));
    static_assert(
        static_cast<uint8_t>(WorldCommandPhase::SystemPostRender) ==
        static_cast<uint8_t>(
            GameProjectWorldCommandPhase::SystemPostRender));

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

    bool ObjectView::TryGetRuntimeLightState(
        GameProjectLightState& outState) const noexcept {
        outState = {};
        try {
            GameObject* object = GameProjectSdkAccess::GetObject(*this);
            World* world = object != nullptr ? object->GetWorld() : nullptr;
            if (object == nullptr || world == nullptr ||
                !world->IsOwnerThread() ||
                GameProjectSdkAccess::IsWorldClosing(*world)) {
                return false;
            }
            const LightComponent* light =
                object->GetComponent<LightComponent>();
            if (light == nullptr) {
                return false;
            }
            const MATH::Vec3& color = light->GetColor();
            outState.type = ToProjectLightType(light->GetLightType());
            outState.enabled = light->IsEnabled() ? 1u : 0u;
            outState.color = { color.x, color.y, color.z };
            outState.intensity = light->GetIntensity();
            outState.range = light->GetRange();
            outState.innerConeAngleDegrees =
                light->GetInnerConeAngleDegrees();
            outState.outerConeAngleDegrees =
                light->GetOuterConeAngleDegrees();
            outState.angularFalloff = light->GetAngularFalloff();
            outState.priority = light->GetPriority();
            outState.stateRevision = light->GetStateRevision();
            return true;
        }
        catch (...) {
            outState = {};
            return false;
        }
    }

    GameProjectWorldResult ObjectView::ApplyRuntimeLightPatch(
        const GameProjectLightPatch& patch) const noexcept {
        RuntimeWorldAccessResult access{};
        World* world = World::ResolveRuntimeWorld(
            handle_.world.value,
            &access);
        if (world == nullptr) {
            return PROJECT::INTERNAL::MakeWorldAccessFailure(access);
        }
        if (!world->IsOwnerThread()) {
            return PROJECT::INTERNAL::MakeWorldResult(
                GameProjectWorldResultCode::WrongThread,
                handle_);
        }
        if (!PROJECT::INTERNAL::IsObjectHandleForWorld(handle_, *world)) {
            return PROJECT::INTERNAL::MakeWorldResult(
                GameProjectWorldResultCode::InvalidObject,
                handle_);
        }
        if (GameProjectSdkAccess::IsWorldClosing(*world)) {
            return PROJECT::INTERNAL::MakeWorldResult(
                GameProjectWorldResultCode::WorldClosing,
                handle_);
        }
        const uint32_t mask = patch.fieldMask;
        if (patch.structSize != kGameProjectLightPatchV1Size ||
            patch.version != kGameProjectLightPatchVersion ||
            mask == 0u ||
            (mask & ~kGameProjectLightPatchKnownFields) != 0u ||
            HasNonZeroLightPatchReserved(patch) ||
            ((mask & GameProjectLightPatchFields(
                GameProjectLightPatchField::Enabled)) != 0u &&
                patch.enabled > 1u)) {
            return PROJECT::INTERNAL::MakeWorldResult(
                GameProjectWorldResultCode::InvalidRequest,
                handle_);
        }

        GameObject* object = world->FindObject(
            PROJECT::INTERNAL::DecodeRuntimeHandle(handle_));
        LightComponent* light = object != nullptr
            ? object->GetComponent<LightComponent>()
            : nullptr;
        if (light == nullptr) {
            return PROJECT::INTERNAL::MakeWorldResult(
                object == nullptr
                    ? GameProjectWorldResultCode::InvalidObject
                    : GameProjectWorldResultCode::Unsupported,
                handle_);
        }

        const auto Selected = [mask](GameProjectLightPatchField field) {
            return (mask & GameProjectLightPatchFields(field)) != 0u;
        };
        if ((Selected(GameProjectLightPatchField::Color) &&
             (!std::isfinite(patch.color.x) ||
              !std::isfinite(patch.color.y) ||
              !std::isfinite(patch.color.z))) ||
            (Selected(GameProjectLightPatchField::Intensity) &&
             !std::isfinite(patch.intensity)) ||
            (Selected(GameProjectLightPatchField::Range) &&
             !std::isfinite(patch.range)) ||
            (Selected(GameProjectLightPatchField::SpotCone) &&
             (!std::isfinite(patch.innerConeAngleDegrees) ||
              !std::isfinite(patch.outerConeAngleDegrees))) ||
            (Selected(GameProjectLightPatchField::AngularFalloff) &&
             !std::isfinite(patch.angularFalloff))) {
            return PROJECT::INTERNAL::MakeWorldResult(
                GameProjectWorldResultCode::InvalidRequest,
                handle_);
        }
        if ((Selected(GameProjectLightPatchField::Range) &&
             light->GetLightType() == LightComponentType::Directional) ||
            ((Selected(GameProjectLightPatchField::SpotCone) ||
              Selected(GameProjectLightPatchField::AngularFalloff)) &&
             light->GetLightType() != LightComponentType::Spot)) {
            return PROJECT::INTERNAL::MakeWorldResult(
                GameProjectWorldResultCode::Unsupported,
                handle_);
        }

        const MATH::Vec3 color{
            std::clamp(patch.color.x, 0.0f, 1.0f),
            std::clamp(patch.color.y, 0.0f, 1.0f),
            std::clamp(patch.color.z, 0.0f, 1.0f),
        };
        const float intensity = std::clamp(
            patch.intensity,
            0.0f,
            100.0f);
        const float range = std::clamp(
            patch.range,
            0.05f,
            1000.0f);
        const float innerCone = std::clamp(
            patch.innerConeAngleDegrees,
            0.1f,
            89.0f);
        const float outerCone = std::clamp(
            patch.outerConeAngleDegrees,
            innerCone,
            89.0f);
        const float angularFalloff = std::clamp(
            patch.angularFalloff,
            0.05f,
            8.0f);

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

    bool ObjectView::SetRuntimeMaterialColorOverride(
        uint32_t materialIndex,
        GameProjectFloat3 baseColor,
        float opacity,
        GameProjectFloat3 emissiveColor,
        float emissiveStrength) const noexcept {
        try {
            GameObject* object = GameProjectSdkAccess::GetObject(*this);
            World* world = object != nullptr ? object->GetWorld() : nullptr;
            if (object == nullptr || world == nullptr ||
                !world->IsOwnerThread() ||
                GameProjectSdkAccess::IsWorldClosing(*world) ||
                !std::isfinite(baseColor.x) ||
                !std::isfinite(baseColor.y) ||
                !std::isfinite(baseColor.z) ||
                !std::isfinite(opacity) ||
                !std::isfinite(emissiveColor.x) ||
                !std::isfinite(emissiveColor.y) ||
                !std::isfinite(emissiveColor.z) ||
                !std::isfinite(emissiveStrength)) {
                return false;
            }
            ModelComponent* model =
                object->GetComponent<ModelComponent>();
            Material* material = model != nullptr
                ? model->GetRuntimeMaterialOverride(materialIndex)
                : nullptr;
            if (material == nullptr) {
                return false;
            }
            const uint64_t revisionBefore = material->GetRevision();
            const auto Color = [](float value) {
                return std::clamp(value, 0.0f, 64.0f);
            };
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
            material->SetEmissiveStrength(
                std::clamp(emissiveStrength, 0.0f, 64.0f));
            if (material->GetRevision() != revisionBefore) {
                world->MarkRenderObjectDirty(object);
            }
            return true;
        }
        catch (...) {
            return false;
        }
    }

    bool ObjectView::SetModelNodeVisibility(
        std::string_view nodeName,
        uint8_t visible,
        GameProjectModelNodeVisibilityScope scope) const noexcept {
        try {
            GameObject* object = GameProjectSdkAccess::GetObject(*this);
            World* world = object != nullptr ? object->GetWorld() : nullptr;
            if (object == nullptr || world == nullptr ||
                !world->IsOwnerThread() ||
                GameProjectSdkAccess::IsWorldClosing(*world) ||
                nodeName.empty() ||
                nodeName.size() >
                    kGameProjectModelNodeNameMaximumBytes ||
                visible > 1u ||
                scope >= GameProjectModelNodeVisibilityScope::Count) {
                return false;
            }
            ModelComponent* model =
                object->GetComponent<ModelComponent>();
            return model != nullptr &&
                model->SetRuntimeNodeVisibilityOverride(
                    nodeName,
                    visible != 0u,
                    scope == GameProjectModelNodeVisibilityScope::
                        NodeAndDescendants);
        }
        catch (...) {
            return false;
        }
    }

    bool ObjectView::ClearModelNodeVisibilityOverride(
        std::string_view nodeName) const noexcept {
        try {
            GameObject* object = GameProjectSdkAccess::GetObject(*this);
            World* world = object != nullptr ? object->GetWorld() : nullptr;
            if (object == nullptr || world == nullptr ||
                !world->IsOwnerThread() ||
                GameProjectSdkAccess::IsWorldClosing(*world) ||
                nodeName.empty() ||
                nodeName.size() >
                    kGameProjectModelNodeNameMaximumBytes) {
                return false;
            }
            ModelComponent* model =
                object->GetComponent<ModelComponent>();
            if (model == nullptr) {
                return false;
            }
            (void)model->ClearRuntimeNodeVisibilityOverride(nodeName);
            return true;
        }
        catch (...) {
            return false;
        }
    }

    bool ObjectView::ClearModelNodeVisibilityOverrides() const noexcept {
        try {
            GameObject* object = GameProjectSdkAccess::GetObject(*this);
            World* world = object != nullptr ? object->GetWorld() : nullptr;
            if (object == nullptr || world == nullptr ||
                !world->IsOwnerThread() ||
                GameProjectSdkAccess::IsWorldClosing(*world)) {
                return false;
            }
            ModelComponent* model =
                object->GetComponent<ModelComponent>();
            if (model == nullptr) {
                return false;
            }
            model->ClearRuntimeNodeVisibilityOverrides();
            return true;
        }
        catch (...) {
            return false;
        }
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
            ? static_cast<uint64_t>(world->GetTotalObjectCount())
            : 0u;
    }

    bool WorldView::CanMutateStructure() const noexcept {
        const World* world = GameProjectSdkAccess::GetWorld(*this);
        return world != nullptr && world->CanMutateStructure();
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

    TerrainView GameProjectSdkAccess::MakeTerrainView(
        World& world) noexcept {
        return TerrainView(&world);
    }

    TerrainView WorldView::GetTerrain() const noexcept {
        World* world = GameProjectSdkAccess::GetWorld(*this);
        return world != nullptr
            ? GameProjectSdkAccess::MakeTerrainView(*world)
            : TerrainView{};
    }

    bool TerrainView::IsSandSurfaceAt(
        GameProjectFloat3 worldPosition,
        float maximumVerticalDistance) const noexcept {
        try {
            if (world_ == nullptr || !std::isfinite(worldPosition.x) ||
                !std::isfinite(worldPosition.y) ||
                !std::isfinite(worldPosition.z) ||
                !std::isfinite(maximumVerticalDistance) ||
                maximumVerticalDistance < 0.0f ||
                maximumVerticalDistance > 10.0f) {
                return false;
            }
            auto* world = static_cast<World*>(world_);
            SAND::SandSurfaceQueryService* sand =
                world->Services().Find<SAND::SandSurfaceQueryService>();
            if (sand == nullptr) {
                return false;
            }
            float height = 0.0f;
            MATH::Vec3 normal{};
            return sand->SampleWorldHeight(
                    { worldPosition.x, worldPosition.y, worldPosition.z },
                    height,
                    normal) &&
                std::abs(worldPosition.y - height) <=
                    maximumVerticalDistance;
        }
        catch (...) {
            return false;
        }
    }

} // namespace HIKARI
