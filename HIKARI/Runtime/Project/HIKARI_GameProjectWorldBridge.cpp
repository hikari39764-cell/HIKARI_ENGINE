#include "HIKARI/Project/WorldView.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "Core/Math/HIKARI_MathValidation.h"
#include "Assets/HIKARI_AssetGuid.h"
#include "Runtime/Binding/HIKARI_RuntimeBindingStore.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Components/HIKARI_ColliderComponent.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"
#include "Scene/HIKARI_RuntimeObjectActivityService.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Scene/HIKARI_SceneRuntimeBuilder.h"
#include "Scene/Sand/HIKARI_SandSurfaceQueryService.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {
    namespace {

        RuntimeObjectHandle DecodeRuntimeHandle(
            uint64_t handle) noexcept {
            if (handle == 0u) {
                return {};
            }
            RuntimeObjectHandle result{};
            result.slot = static_cast<uint32_t>(
                (handle & 0xFFFFFFFFu) - 1u);
            result.generation =
                static_cast<uint32_t>(handle >> 32u);
            return result;
        }

        BINDING::ValueType ToRuntimeType(
            GameProjectBindingValueType type) noexcept {
            switch (type) {
            case GameProjectBindingValueType::Bool:
                return BINDING::ValueType::Bool;
            case GameProjectBindingValueType::Int:
                return BINDING::ValueType::Int;
            case GameProjectBindingValueType::Float2:
                return BINDING::ValueType::Float2;
            case GameProjectBindingValueType::Float3:
                return BINDING::ValueType::Float3;
            case GameProjectBindingValueType::Float4:
                return BINDING::ValueType::Float4;
            case GameProjectBindingValueType::Color:
                return BINDING::ValueType::Color;
            case GameProjectBindingValueType::Float:
            default:
                return BINDING::ValueType::Float;
            }
        }

        GameProjectBindingValueType ToProjectType(
            BINDING::ValueType type) noexcept {
            switch (type) {
            case BINDING::ValueType::Bool:
                return GameProjectBindingValueType::Bool;
            case BINDING::ValueType::Int:
                return GameProjectBindingValueType::Int;
            case BINDING::ValueType::Float2:
                return GameProjectBindingValueType::Float2;
            case BINDING::ValueType::Float3:
                return GameProjectBindingValueType::Float3;
            case BINDING::ValueType::Float4:
                return GameProjectBindingValueType::Float4;
            case BINDING::ValueType::Color:
                return GameProjectBindingValueType::Color;
            case BINDING::ValueType::Float:
            default:
                return GameProjectBindingValueType::Float;
            }
        }

        BINDING::Value ToRuntimeValue(
            const GameProjectBindingValue& value) noexcept {
            BINDING::Value result{};
            result.type = ToRuntimeType(value.type);
            result.boolean = value.boolean;
            result.integer = value.integer;
            result.numeric = {
                value.numeric[0],
                value.numeric[1],
                value.numeric[2],
                value.numeric[3]
            };
            return result;
        }

        GameProjectBindingValue ToProjectValue(
            const BINDING::Value& value) noexcept {
            GameProjectBindingValue result{};
            result.type = ToProjectType(value.type);
            result.boolean = value.boolean;
            result.integer = value.integer;
            result.numeric[0] = value.numeric.x;
            result.numeric[1] = value.numeric.y;
            result.numeric[2] = value.numeric.z;
            result.numeric[3] = value.numeric.w;
            return result;
        }

        BINDING::RuntimeBindingStore* FindBindingStore(
            World* world) noexcept {
            return world != nullptr
                ? world->Services().Find<
                    BINDING::RuntimeBindingStore>()
                : nullptr;
        }

        bool IsFinite(const GameProjectFloat3& value) noexcept {
            return std::isfinite(value.x) && std::isfinite(value.y) &&
                std::isfinite(value.z);
        }

        bool IsFinite(const GameProjectQuat& value) noexcept {
            return std::isfinite(value.x) && std::isfinite(value.y) &&
                std::isfinite(value.z) && std::isfinite(value.w);
        }

        bool ToRuntimeColliderShape(
            GameProjectPhysicsShapeType type,
            PHYSICS::PhysicsShapeType& outType) noexcept {
            switch (type) {
            case GameProjectPhysicsShapeType::Box:
                outType = PHYSICS::PhysicsShapeType::Box;
                return true;
            case GameProjectPhysicsShapeType::Sphere:
                outType = PHYSICS::PhysicsShapeType::Sphere;
                return true;
            case GameProjectPhysicsShapeType::Capsule:
                outType = PHYSICS::PhysicsShapeType::Capsule;
                return true;
            default:
                return false;
            }
        }

        bool ToProjectColliderShape(
            PHYSICS::PhysicsShapeType type,
            GameProjectPhysicsShapeType& outType) noexcept {
            switch (type) {
            case PHYSICS::PhysicsShapeType::Box:
                outType = GameProjectPhysicsShapeType::Box;
                return true;
            case PHYSICS::PhysicsShapeType::Sphere:
                outType = GameProjectPhysicsShapeType::Sphere;
                return true;
            case PHYSICS::PhysicsShapeType::Capsule:
                outType = GameProjectPhysicsShapeType::Capsule;
                return true;
            default:
                return false;
            }
        }

    } // namespace

    ObjectView GameProjectSdkAccess::MakeObjectView(
        GameObject& object) noexcept {
        return ObjectView(&object);
    }

    WorldView GameProjectSdkAccess::MakeWorldView(
        World& world) noexcept {
        return WorldView(&world);
    }

    TerrainView GameProjectSdkAccess::MakeTerrainView(
        World& world) noexcept {
        return TerrainView(&world);
    }

    // 公開済みの World bridge から action bridge を参照し、
    // static library 内の SDK export 群を host link に確実に取り込む。
    InputView WorldView::GetInput() const noexcept {
        try {
            World* world = GameProjectSdkAccess::GetWorld(*this);
            return world != nullptr
                ? GameProjectSdkAccess::MakeInputView(*world)
                : InputView{};
        }
        catch (...) {
            return {};
        }
    }

    AnimationView WorldView::GetAnimation() const noexcept {
        try {
            World* world = GameProjectSdkAccess::GetWorld(*this);
            return world != nullptr
                ? GameProjectSdkAccess::MakeAnimationView(*world)
                : AnimationView{};
        }
        catch (...) {
            return {};
        }
    }

    MotionView WorldView::GetMotion() const noexcept {
        try {
            World* world = GameProjectSdkAccess::GetWorld(*this);
            return world != nullptr
                ? GameProjectSdkAccess::MakeMotionView(*world)
                : MotionView{};
        }
        catch (...) {
            return {};
        }
    }

    PhysicsQueryView WorldView::GetPhysicsQueries() const noexcept {
        try {
            World* world = GameProjectSdkAccess::GetWorld(*this);
            return world != nullptr
                ? GameProjectSdkAccess::MakePhysicsQueryView(*world)
                : PhysicsQueryView{};
        }
        catch (...) {
            return {};
        }
    }

    AudioView WorldView::GetAudio() const noexcept {
        try {
            World* world = GameProjectSdkAccess::GetWorld(*this);
            return world != nullptr
                ? GameProjectSdkAccess::MakeAudioView(*world)
                : AudioView{};
        }
        catch (...) {
            return {};
        }
    }

    NavigationView WorldView::GetNavigation() const noexcept {
        try {
            World* world = GameProjectSdkAccess::GetWorld(*this);
            return world != nullptr
                ? GameProjectSdkAccess::MakeNavigationView(*world)
                : NavigationView{};
        }
        catch (...) {
            return {};
        }
    }

    TerrainView WorldView::GetTerrain() const noexcept {
        try {
            World* world = GameProjectSdkAccess::GetWorld(*this);
            return world != nullptr
                ? GameProjectSdkAccess::MakeTerrainView(*world)
                : TerrainView{};
        }
        catch (...) {
            return {};
        }
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
            World* world = static_cast<World*>(world_);
            SAND::SandSurfaceQueryService* sand =
                world->Services().Find<SAND::SandSurfaceQueryService>();
            if (sand == nullptr) {
                return false;
            }
            float height = 0.0f;
            MATH::Vec3 normal{};
            if (!sand->SampleWorldHeight(
                    { worldPosition.x, worldPosition.y, worldPosition.z },
                    height,
                    normal)) {
                return false;
            }
            return std::abs(worldPosition.y - height) <=
                maximumVerticalDistance;
        }
        catch (...) {
            return false;
        }
    }

    CameraView WorldView::GetCamera() const noexcept {
        try {
            World* world = GameProjectSdkAccess::GetWorld(*this);
            return world != nullptr
                ? GameProjectSdkAccess::MakeCameraView(*world)
                : CameraView{};
        }
        catch (...) {
            return {};
        }
    }

    TimeDilationView WorldView::GetTimeDilation() const noexcept {
        try {
            World* world = GameProjectSdkAccess::GetWorld(*this);
            return world != nullptr
                ? GameProjectSdkAccess::MakeTimeDilationView(*world)
                : TimeDilationView{};
        }
        catch (...) {
            return {};
        }
    }

    PresentationModelView WorldView::GetPresentationModels() const noexcept {
        try {
            World* world = GameProjectSdkAccess::GetWorld(*this);
            return world != nullptr
                ? GameProjectSdkAccess::MakePresentationModelView(*world)
                : PresentationModelView{};
        }
        catch (...) {
            return {};
        }
    }

    PresentationVfxView WorldView::GetPresentationVfx() const noexcept {
        try {
            World* world = GameProjectSdkAccess::GetWorld(*this);
            return world != nullptr
                ? GameProjectSdkAccess::MakePresentationVfxView(*world)
                : PresentationVfxView{};
        }
        catch (...) {
            return {};
        }
    }

    VfxView WorldView::GetVfx() const noexcept {
        try {
            World* world = GameProjectSdkAccess::GetWorld(*this);
            return world != nullptr
                ? GameProjectSdkAccess::MakeVfxView(*world)
                : VfxView{};
        }
        catch (...) {
            return {};
        }
    }

    UiView WorldView::GetUi() const noexcept {
        try {
            World* world = GameProjectSdkAccess::GetWorld(*this);
            return world != nullptr
                ? GameProjectSdkAccess::MakeUiView(*world)
                : UiView{};
        }
        catch (...) {
            return {};
        }
    }

    GameObject* GameProjectSdkAccess::GetObject(
        const ObjectView& view) noexcept {
        return static_cast<GameObject*>(view.object_);
    }

    World* GameProjectSdkAccess::GetWorld(
        const WorldView& view) noexcept {
        return static_cast<World*>(view.world_);
    }

    uint64_t ObjectView::GetHandle() const noexcept {
        const GameObject* object =
            GameProjectSdkAccess::GetObject(*this);
        return object != nullptr
            ? object->GetRuntimeHandle().ToValue()
            : 0u;
    }

    std::string_view ObjectView::GetName() const noexcept {
        const GameObject* object =
            GameProjectSdkAccess::GetObject(*this);
        return object != nullptr
            ? std::string_view(object->GetName())
            : std::string_view{};
    }

    GameProjectFloat3 ObjectView::GetLocalPosition() const noexcept {
        const GameObject* object =
            GameProjectSdkAccess::GetObject(*this);
        if (object == nullptr) {
            return {};
        }
        const MATH::Vec3& position =
            object->GetTransform().position;
        return { position.x, position.y, position.z };
    }

    bool ObjectView::GetWorldTransform(
        GameProjectTransform& outTransform) const noexcept {
        outTransform = {};
        try {
            const GameObject* object =
                GameProjectSdkAccess::GetObject(*this);
            if (object == nullptr) {
                return false;
            }
            MATH::Vec3 position{};
            MATH::Quat rotation = MATH::Quat::Identity();
            MATH::Vec3 scale{ 1.0f, 1.0f, 1.0f };
            if (!MATH::DecomposeTRS(
                    object->GetTransform().GetWorldMatrix(),
                    position,
                    rotation,
                    scale) ||
                !MATH::IsFinite(position) ||
                !MATH::IsFinite(rotation) ||
                !MATH::IsFinite(scale)) {
                return false;
            }
            outTransform.position = { position.x, position.y, position.z };
            outTransform.rotation = {
                rotation.x, rotation.y, rotation.z, rotation.w
            };
            outTransform.scale = { scale.x, scale.y, scale.z };
            return true;
        }
        catch (...) {
            outTransform = {};
            return false;
        }
    }

    bool ObjectView::SetLocalPosition(
        GameProjectFloat3 position) const noexcept {
        GameObject* object =
            GameProjectSdkAccess::GetObject(*this);
        return object != nullptr &&
            object->SetLocalPosition({
                position.x,
                position.y,
                position.z
            });
    }

    bool ObjectView::SetRuntimeActivity(
        GameProjectRuntimeActivity activity) const noexcept {
        try {
            GameObject* object = GameProjectSdkAccess::GetObject(*this);
            World* world = object != nullptr ? object->GetWorld() : nullptr;
            RuntimeObjectActivityService* service = world != nullptr
                ? world->Services().Find<RuntimeObjectActivityService>()
                : nullptr;
            if (service == nullptr) {
                return false;
            }
            const RuntimeObjectHandle handle = object->GetRuntimeHandle();
            const GameProjectRuntimeActivity previous =
                service->Get(handle);
            if (!service->Set(handle, activity)) {
                return false;
            }
            if (previous != activity) {
                // activity は authored visibility を変更せず、render cache
                // の descriptor だけを次の patch sync で更新する。
                object->MarkRenderStateDirty();
            }
            return true;
        }
        catch (...) {
            return false;
        }
    }

    GameProjectRuntimeActivity ObjectView::GetRuntimeActivity()
        const noexcept {
        try {
            const GameObject* object =
                GameProjectSdkAccess::GetObject(*this);
            const World* world = object != nullptr
                ? object->GetWorld()
                : nullptr;
            const RuntimeObjectActivityService* service = world != nullptr
                ? world->Services().Find<RuntimeObjectActivityService>()
                : nullptr;
            return service != nullptr
                ? service->Get(object->GetRuntimeHandle())
                : GameProjectRuntimeActivity::Active;
        }
        catch (...) {
            return GameProjectRuntimeActivity::Active;
        }
    }

    ObjectView WorldView::FindObject(
        uint64_t handle) const noexcept {
        World* world = GameProjectSdkAccess::GetWorld(*this);
        if (world == nullptr || handle == 0u) {
            return {};
        }
        const RuntimeObjectHandle runtimeHandle =
            DecodeRuntimeHandle(handle);
        GameObject* object = world->FindObject(runtimeHandle);
        return object != nullptr
            ? GameProjectSdkAccess::MakeObjectView(*object)
            : ObjectView{};
    }

    SceneFlowView WorldView::GetSceneFlow() const noexcept {
        try {
            World* world = GameProjectSdkAccess::GetWorld(*this);
            return world != nullptr
                ? GameProjectSdkAccess::MakeSceneFlowView(*world)
                : SceneFlowView{};
        }
        catch (...) {
            return {};
        }
    }

    bool ObjectView::SetModelAssetGuid(
        std::string_view assetGuid) const noexcept {
        try {
            GameObject* object = GameProjectSdkAccess::GetObject(*this);
            ModelComponent* model = object != nullptr
                ? object->GetComponent<ModelComponent>()
                : nullptr;
            if (model == nullptr || !IsValidAssetGuid(assetGuid)) {
                return false;
            }
            if (model->GetAssetId() != assetGuid) {
                model->SetAssetId(std::string(assetGuid));
                World* world = object->GetWorld();
                RuntimeAssetContextService* assets = world != nullptr
                    ? world->Services().Find<RuntimeAssetContextService>()
                    : nullptr;
                ModelAsset* resolved = assets != nullptr &&
                        assets->assetRegistry != nullptr &&
                        assets->modelManager != nullptr
                    ? ResolveModelAssetNow(
                        *assets->modelManager,
                        *assets->assetRegistry,
                        model->GetAssetId())
                    : nullptr;
                // Never leave the prior model pointer paired with the newly
                // authored identity. A configured world resolves immediately;
                // an early construction pass resolves through SceneRuntimeBuilder.
                model->SetModelAsset(resolved);
            }
            return true;
        }
        catch (...) {
            return false;
        }
    }

    bool ObjectView::SetModelMaterialSetAssetGuid(
        std::string_view materialSetAssetGuid) const noexcept {
        try {
            GameObject* object = GameProjectSdkAccess::GetObject(*this);
            ModelComponent* model = object != nullptr
                ? object->GetComponent<ModelComponent>()
                : nullptr;
            if (model == nullptr ||
                !IsValidAssetGuid(materialSetAssetGuid)) {
                return false;
            }
            const AssetGuid guid{ std::string(materialSetAssetGuid) };
            if (!model->HasAuthoredMaterialSetSelection() ||
                model->GetMaterialSetAssetGuid() != guid) {
                model->SetMaterialSetAssetGuid(guid);
            }
            return true;
        }
        catch (...) {
            return false;
        }
    }

    bool ObjectView::UseImportedModelMaterials() const noexcept {
        try {
            GameObject* object = GameProjectSdkAccess::GetObject(*this);
            ModelComponent* model = object != nullptr
                ? object->GetComponent<ModelComponent>()
                : nullptr;
            if (model == nullptr) {
                return false;
            }
            model->UseImportedModelMaterials();
            return true;
        }
        catch (...) {
            return false;
        }
    }

    bool ObjectView::GetPrimaryColliderShape(
        GameProjectPhysicsShape& outShape) const noexcept {
        outShape = {};
        try {
            const GameObject* object =
                GameProjectSdkAccess::GetObject(*this);
            const ColliderComponent* collider = object != nullptr
                ? object->GetComponent<ColliderComponent>()
                : nullptr;
            if (collider == nullptr || !collider->IsEnabled()) {
                return false;
            }
            const ResolvedColliderShape resolved =
                collider->ResolveShape();
            if (!ToProjectColliderShape(
                    resolved.type, outShape.type)) {
                return false;
            }
            constexpr float kDegreesToRadians =
                std::numbers::pi_v<float> / 180.0f;
            const MATH::Quat rotation = MATH::Quat::FromEulerXYZ(
                resolved.rotationEulerDegrees.x * kDegreesToRadians,
                resolved.rotationEulerDegrees.y * kDegreesToRadians,
                resolved.rotationEulerDegrees.z * kDegreesToRadians);
            outShape.center = {
                resolved.center.x,
                resolved.center.y,
                resolved.center.z };
            outShape.rotation = {
                rotation.x, rotation.y, rotation.z, rotation.w };
            outShape.halfExtents = {
                resolved.size.x * 0.5f,
                resolved.size.y * 0.5f,
                resolved.size.z * 0.5f };
            outShape.radius = resolved.radius;
            outShape.height = resolved.height;
            outShape.isTrigger = collider->IsTrigger() ? 1u : 0u;
            return true;
        }
        catch (...) {
            outShape = {};
            return false;
        }
    }

    bool ObjectView::ConfigureDefaultCollider(
        const GameProjectPhysicsShape& shape) const noexcept {
        try {
            GameObject* object = GameProjectSdkAccess::GetObject(*this);
            ColliderComponent* collider = object != nullptr
                ? object->GetComponent<ColliderComponent>()
                : nullptr;
            PHYSICS::PhysicsShapeType runtimeType{};
            if (collider == nullptr || !IsFinite(shape.center) ||
                !IsFinite(shape.rotation) || shape.isTrigger > 1u ||
                shape.reserved[0] != 0u || shape.reserved[1] != 0u ||
                !ToRuntimeColliderShape(shape.type, runtimeType)) {
                return false;
            }
            const MATH::Quat requestedRotation{
                shape.rotation.x,
                shape.rotation.y,
                shape.rotation.z,
                shape.rotation.w };
            const float rotationLengthSquared =
                requestedRotation.x * requestedRotation.x +
                requestedRotation.y * requestedRotation.y +
                requestedRotation.z * requestedRotation.z +
                requestedRotation.w * requestedRotation.w;
            if (!std::isfinite(rotationLengthSquared) ||
                rotationLengthSquared <= 0.000001f) {
                return false;
            }
            const MATH::Quat rotation = MATH::NormalizeQ(
                requestedRotation);
            const MATH::Vec3 center{
                shape.center.x, shape.center.y, shape.center.z };
            const MATH::Vec3 rotationDegrees =
                MATH::EulerXYZDegreesFromQuat(rotation);
            MATH::Vec3 size{ 1.0f, 1.0f, 1.0f };
            float radius = shape.radius;
            float height = shape.height;
            switch (shape.type) {
            case GameProjectPhysicsShapeType::Box:
                if (!IsFinite(shape.halfExtents) ||
                    shape.halfExtents.x <= 0.0f ||
                    shape.halfExtents.y <= 0.0f ||
                    shape.halfExtents.z <= 0.0f) {
                    return false;
                }
                size = {
                    shape.halfExtents.x * 2.0f,
                    shape.halfExtents.y * 2.0f,
                    shape.halfExtents.z * 2.0f };
                break;
            case GameProjectPhysicsShapeType::Sphere:
                if (!std::isfinite(radius) || radius <= 0.0f) {
                    return false;
                }
                size = { radius * 2.0f, radius * 2.0f,
                    radius * 2.0f };
                height = radius * 2.0f;
                break;
            case GameProjectPhysicsShapeType::Capsule:
                if (!std::isfinite(radius) || radius <= 0.0f ||
                    !std::isfinite(height) ||
                    height < radius * 2.0f) {
                    return false;
                }
                size = { radius * 2.0f, height, radius * 2.0f };
                break;
            default:
                return false;
            }
            return collider->ConfigureIfPristineManualShape(
                runtimeType,
                center,
                rotationDegrees,
                size,
                radius,
                height,
                shape.isTrigger != 0u);
        }
        catch (...) {
            return false;
        }
    }

    DialogueView WorldView::GetDialogue() const noexcept {
        try {
            World* world = GameProjectSdkAccess::GetWorld(*this);
            return world != nullptr
                ? GameProjectSdkAccess::MakeDialogueView(*world)
                : DialogueView{};
        }
        catch (...) {
            return {};
        }
    }

    SaveGameView WorldView::GetSaveGame() const noexcept {
        try {
            World* world = GameProjectSdkAccess::GetWorld(*this);
            return world != nullptr
                ? GameProjectSdkAccess::MakeSaveGameView(*world)
                : SaveGameView{};
        }
        catch (...) {
            return {};
        }
    }

    RuntimeSessionView WorldView::GetRuntimeSession() const noexcept {
        try {
            World* world = GameProjectSdkAccess::GetWorld(*this);
            return world != nullptr
                ? GameProjectSdkAccess::MakeRuntimeSessionView(*world)
                : RuntimeSessionView{};
        }
        catch (...) {
            return {};
        }
    }

    bool WorldView::ReadVariable(
        std::string_view variableId,
        GameProjectBindingValue& outValue) const noexcept {
        BINDING::RuntimeBindingStore* store = FindBindingStore(
            GameProjectSdkAccess::GetWorld(*this));
        BINDING::Value value{};
        if (store == nullptr ||
            !store->ReadVariable(variableId, value)) {
            return false;
        }
        outValue = ToProjectValue(value);
        return true;
    }

    bool WorldView::WriteVariable(
        std::string_view variableId,
        const GameProjectBindingValue& value,
        std::string_view writerId) const noexcept {
        BINDING::RuntimeBindingStore* store = FindBindingStore(
            GameProjectSdkAccess::GetWorld(*this));
        if (store == nullptr) {
            return false;
        }
        try {
            return store->WriteVariable(
                variableId, ToRuntimeValue(value), writerId);
        }
        catch (...) {
            return false;
        }
    }

    bool WorldView::ReleaseVariableWriter(
        std::string_view variableId,
        std::string_view writerId) const noexcept {
        BINDING::RuntimeBindingStore* store = FindBindingStore(
            GameProjectSdkAccess::GetWorld(*this));
        return store != nullptr &&
            store->ReleaseWriter(variableId, writerId);
    }

    bool WorldView::EmitEvent(
        std::string_view eventId,
        uint64_t sourceObjectHandle) const noexcept {
        BINDING::RuntimeBindingStore* store = FindBindingStore(
            GameProjectSdkAccess::GetWorld(*this));
        if (store == nullptr) {
            return false;
        }
        try {
            return store->Emit(
                eventId,
                DecodeRuntimeHandle(sourceObjectHandle));
        }
        catch (...) {
            return false;
        }
    }

    bool WorldView::EmitEvent(
        std::string_view eventId,
        uint64_t sourceObjectHandle,
        const GameProjectBindingValue& payload) const noexcept {
        BINDING::RuntimeBindingStore* store = FindBindingStore(
            GameProjectSdkAccess::GetWorld(*this));
        if (store == nullptr) {
            return false;
        }
        const BINDING::Value runtimePayload =
            ToRuntimeValue(payload);
        try {
            return store->Emit(
                eventId,
                DecodeRuntimeHandle(sourceObjectHandle),
                &runtimePayload);
        }
        catch (...) {
            return false;
        }
    }

    uint32_t WorldView::GetEventCount(
        std::string_view eventId) const noexcept {
        const BINDING::RuntimeBindingStore* store = FindBindingStore(
            GameProjectSdkAccess::GetWorld(*this));
        if (store == nullptr) {
            return 0u;
        }
        return static_cast<uint32_t>(std::count_if(
            store->GetEvents().begin(),
            store->GetEvents().end(),
            [eventId](const BINDING::EventRecord& event) {
                return event.eventId == eventId;
            }));
    }

    bool WorldView::ReadEvent(
        std::string_view eventId,
        uint32_t index,
        GameProjectEventRecord& outEvent) const noexcept {
        const BINDING::RuntimeBindingStore* store = FindBindingStore(
            GameProjectSdkAccess::GetWorld(*this));
        if (store == nullptr) {
            return false;
        }
        uint32_t matchedIndex = 0u;
        for (const BINDING::EventRecord& event :
                store->GetEvents()) {
            if (event.eventId != eventId) {
                continue;
            }
            if (matchedIndex++ != index) {
                continue;
            }
            outEvent.sequence = event.sequence;
            outEvent.frameIndex = event.frameIndex;
            outEvent.sourceObjectHandle =
                event.sourceObject.ToValue();
            outEvent.hasPayload = event.hasPayload;
            if (event.hasPayload) {
                outEvent.payload = ToProjectValue(event.payload);
            }
            return true;
        }
        return false;
    }

    bool WorldView::VisitComponents(
        std::string_view typeName,
        void* context,
        VisitorFn visitor) const noexcept {
        World* world = GameProjectSdkAccess::GetWorld(*this);
        if (world == nullptr ||
            typeName.empty() ||
            visitor == nullptr) {
            return false;
        }
        for (const auto& object : world->GetObjects()) {
            if (!object) {
                continue;
            }
            for (const auto& component :
                    object->GetComponents()) {
                auto* adapter = dynamic_cast<
                    GameProjectComponentAdapter*>(
                        component.get());
                if (adapter == nullptr ||
                    adapter->GetTypeName() != typeName) {
                    continue;
                }
                if (!visitor(
                        context,
                        GameProjectSdkAccess::MakeObjectView(*object),
                        adapter->GetGameComponent())) {
                    return false;
                }
            }
        }
        return true;
    }

} // namespace HIKARI
