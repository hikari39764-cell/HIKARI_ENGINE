#include "HIKARI/Project/WorldView.h"

#include <algorithm>

#include "Runtime/Binding/HIKARI_RuntimeBindingStore.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"
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

    } // namespace

    ObjectView GameProjectSdkAccess::MakeObjectView(
        GameObject& object) noexcept {
        return ObjectView(&object);
    }

    WorldView GameProjectSdkAccess::MakeWorldView(
        World& world) noexcept {
        return WorldView(&world);
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
