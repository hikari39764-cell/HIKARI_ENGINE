#include "HIKARI/Project/WorldView.h"

#include <algorithm>

#include "Runtime/Binding/HIKARI_RuntimeBindingStore.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectBindingConversion.h"

namespace HIKARI {
    using namespace PROJECT::DETAIL;
    namespace {

        BINDING::RuntimeBindingStore* FindBindingStore(
            World* world) noexcept {
            return world != nullptr
                ? world->Services().Find<
                    BINDING::RuntimeBindingStore>()
                : nullptr;
        }

        bool TryDecodeEventSource(
            GameProjectObjectHandle handle,
            World& world,
            RuntimeObjectHandle& outHandle) noexcept {
            if (handle.IsNull()) {
                outHandle = {};
                return true;
            }
            if (!PROJECT::INTERNAL::IsObjectHandleForWorld(
                    handle,
                    world)) {
                return false;
            }
            outHandle = PROJECT::INTERNAL::DecodeRuntimeHandle(handle);
            return world.FindObject(outHandle) != nullptr;
        }

    } // namespace

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
                variableId,
                ToRuntimeValue(value),
                writerId);
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
        GameProjectObjectHandle sourceObjectHandle) const noexcept {
        World* world = GameProjectSdkAccess::GetWorld(*this);
        BINDING::RuntimeBindingStore* store = FindBindingStore(world);
        RuntimeObjectHandle source{};
        if (store == nullptr ||
            world == nullptr ||
            !TryDecodeEventSource(
                sourceObjectHandle,
                *world,
                source)) {
            return false;
        }
        try {
            return store->Emit(eventId, source);
        }
        catch (...) {
            return false;
        }
    }

    bool WorldView::EmitEvent(
        std::string_view eventId,
        GameProjectObjectHandle sourceObjectHandle,
        const GameProjectBindingValue& payload) const noexcept {
        World* world = GameProjectSdkAccess::GetWorld(*this);
        BINDING::RuntimeBindingStore* store = FindBindingStore(world);
        RuntimeObjectHandle source{};
        if (store == nullptr ||
            world == nullptr ||
            !TryDecodeEventSource(
                sourceObjectHandle,
                *world,
                source)) {
            return false;
        }
        const BINDING::Value runtimePayload =
            ToRuntimeValue(payload);
        try {
            return store->Emit(
                eventId,
                source,
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
        World* world = GameProjectSdkAccess::GetWorld(*this);
        const BINDING::RuntimeBindingStore* store =
            FindBindingStore(world);
        if (store == nullptr || world == nullptr) {
            return false;
        }
        uint32_t matchedIndex = 0u;
        for (const BINDING::EventRecord& event : store->GetEvents()) {
            if (event.eventId != eventId) {
                continue;
            }
            if (matchedIndex++ != index) {
                continue;
            }
            outEvent.sequence = event.sequence;
            outEvent.frameIndex = event.frameIndex;
            outEvent.sourceObjectHandle =
                PROJECT::INTERNAL::EncodeRuntimeHandle(
                    *world,
                    event.sourceObject);
            outEvent.hasPayload = event.hasPayload;
            if (event.hasPayload) {
                outEvent.payload = ToProjectValue(event.payload);
            }
            return true;
        }
        return false;
    }

} // namespace HIKARI
