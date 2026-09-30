#include "HIKARI/Project/GameFlowViews.h"
#include "HIKARI/Project/WorldView.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <limits>
#include <span>
#include <string>

#include "Assets/HIKARI_AssetGuid.h"
#include "Core/HIKARI_Logger.h"
#include "Gameplay/Dialogue/HIKARI_DialogueSessionService.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectSessionRuntimeExtension.h"
#include "Runtime/Services/HIKARI_GameProjectSaveStorageService.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Scene/Components/HIKARI_DialogueTriggerComponent.h"
#include "Scene/HIKARI_World.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectWorldBridgeInternal.h"

namespace HIKARI {
using PROJECT::INTERNAL::CurrentWorld;
namespace {

struct OneShotTriggers final {
    struct Entry final {
        uint64_t handle = 0u;
        DialogueTriggerComponent* trigger = nullptr;
    };
    std::array<Entry, 64u> entries{};
    size_t count = 0u;

    explicit OneShotTriggers(World& world) {
        world.ForEachObjectWith<DialogueTriggerComponent>([&](GameObject& object, DialogueTriggerComponent& trigger) {
            if (count < entries.size() && trigger.IsEnabled() && trigger.IsOneShot()) {
                entries[count++] = {object.GetRuntimeHandle().ToValue(), &trigger};
            }
        });
        std::sort(entries.begin(), entries.begin() + count,
                  [](const Entry& lhs, const Entry& rhs) { return lhs.handle < rhs.handle; });
    }
};

World* ResolveDialogueWorld(void* world) noexcept {
    World* runtimeWorld = CurrentWorld(world);
    return runtimeWorld != nullptr && !GameProjectSdkAccess::IsWorldClosing(*runtimeWorld) ? runtimeWorld : nullptr;
}

size_t DialogueUtf8CodepointBytes(std::string_view source, size_t offset) noexcept {
    const auto byte = [&source](size_t index) noexcept { return static_cast<unsigned char>(source[index]); };
    const auto continuation = [&byte](size_t index) noexcept { return (byte(index) & 0xC0u) == 0x80u; };

    const unsigned char leading = byte(offset);
    if (leading <= 0x7Fu)
        return 1u;
    if (leading >= 0xC2u && leading <= 0xDFu) {
        return offset + 2u <= source.size() && continuation(offset + 1u) ? 2u : 0u;
    }
    if (leading >= 0xE0u && leading <= 0xEFu) {
        if (offset + 3u > source.size() || !continuation(offset + 1u) || !continuation(offset + 2u)) {
            return 0u;
        }
        const unsigned char second = byte(offset + 1u);
        if ((leading == 0xE0u && second < 0xA0u) || (leading == 0xEDu && second > 0x9Fu)) {
            return 0u;
        }
        return 3u;
    }
    if (leading >= 0xF0u && leading <= 0xF4u) {
        if (offset + 4u > source.size() || !continuation(offset + 1u) || !continuation(offset + 2u) ||
            !continuation(offset + 3u)) {
            return 0u;
        }
        const unsigned char second = byte(offset + 1u);
        if ((leading == 0xF0u && second < 0x90u) || (leading == 0xF4u && second > 0x8Fu)) {
            return 0u;
        }
        return 4u;
    }
    return 0u;
}

template <size_t Capacity>
bool CopyDialogueUtf8Truncated(std::string_view source, char (&destination)[Capacity]) noexcept {
    static_assert(Capacity > 0u);
    std::fill(std::begin(destination), std::end(destination), '\0');

    const size_t embeddedNull = source.find('\0');
    const bool containsEmbeddedNull = embeddedNull != std::string_view::npos;
    const std::string_view visible = containsEmbeddedNull ? source.substr(0u, embeddedNull) : source;
    const size_t byteLimit = (std::min)(visible.size(), Capacity - 1u);
    size_t copiedBytes = 0u;
    while (copiedBytes < byteLimit) {
        const size_t codepointBytes = DialogueUtf8CodepointBytes(visible, copiedBytes);
        if (codepointBytes == 0u || codepointBytes > byteLimit - copiedBytes) {
            break;
        }
        copiedBytes += codepointBytes;
    }
    if (copiedBytes != 0u) {
        std::memcpy(destination, visible.data(), copiedBytes);
    }
    destination[copiedBytes] = '\0';
    return containsEmbeddedNull || copiedBytes != source.size();
}

GameProjectDialogueEventType ToProjectDialogueEventType(DIALOGUE::DialogueRuntimeEventType type) noexcept {
    switch (type) {
    case DIALOGUE::DialogueRuntimeEventType::Started: return GameProjectDialogueEventType::Started;
    case DIALOGUE::DialogueRuntimeEventType::NodeEntered: return GameProjectDialogueEventType::NodeEntered;
    case DIALOGUE::DialogueRuntimeEventType::ChoiceSelected: return GameProjectDialogueEventType::ChoiceSelected;
    case DIALOGUE::DialogueRuntimeEventType::EventEmitted: return GameProjectDialogueEventType::EventEmitted;
    case DIALOGUE::DialogueRuntimeEventType::Ended: return GameProjectDialogueEventType::Ended;
    }
    return GameProjectDialogueEventType::Unknown;
}

void FillProjectDialogueEvent(const DIALOGUE::DialogueRuntimeEvent& source,
                              GameProjectDialogueEvent& destination) noexcept {
    destination = {};
    destination.sequence = source.sequence;
    destination.triggerObjectHandle = source.trigger.ToValue();
    destination.interactorObjectHandle = source.interactor.ToValue();
    destination.type = ToProjectDialogueEventType(source.type);
    if (CopyDialogueUtf8Truncated(source.assetGuid, destination.assetGuid)) {
        destination.flags |= GameProjectDialogueEventFlags::AssetGuidTruncated;
    }
    if (CopyDialogueUtf8Truncated(source.nodeId, destination.nodeId)) {
        destination.flags |= GameProjectDialogueEventFlags::NodeIdTruncated;
    }
    if (CopyDialogueUtf8Truncated(source.value, destination.value)) {
        destination.flags |= GameProjectDialogueEventFlags::ValueTruncated;
    }
}

} // namespace

SceneFlowView GameProjectSdkAccess::MakeSceneFlowView(World& world) noexcept { return SceneFlowView(&world); }

DialogueView GameProjectSdkAccess::MakeDialogueView(World& world) noexcept { return DialogueView(&world); }

SaveGameView GameProjectSdkAccess::MakeSaveGameView(World& world) noexcept { return SaveGameView(&world); }

RuntimeSessionView GameProjectSdkAccess::MakeRuntimeSessionView(World& world) noexcept {
    return RuntimeSessionView(&world);
}

SceneFlowView WorldView::GetSceneFlow() const noexcept {
    World* world = GameProjectSdkAccess::GetWorld(*this);
    return world != nullptr ? GameProjectSdkAccess::MakeSceneFlowView(*world) : SceneFlowView{};
}

DialogueView WorldView::GetDialogue() const noexcept {
    World* world = GameProjectSdkAccess::GetWorld(*this);
    return world != nullptr ? GameProjectSdkAccess::MakeDialogueView(*world) : DialogueView{};
}

SaveGameView WorldView::GetSaveGame() const noexcept {
    World* world = GameProjectSdkAccess::GetWorld(*this);
    return world != nullptr ? GameProjectSdkAccess::MakeSaveGameView(*world) : SaveGameView{};
}

RuntimeSessionView WorldView::GetRuntimeSession() const noexcept {
    World* world = GameProjectSdkAccess::GetWorld(*this);
    return world != nullptr ? GameProjectSdkAccess::MakeRuntimeSessionView(*world) : RuntimeSessionView{};
}

IGameProjectSession* WorldView::GetGameSession() const noexcept {
    World* world = GameProjectSdkAccess::GetWorld(*this);
    if (world == nullptr || !world->IsOwnerThread()) {
        return nullptr;
    }
    RUNTIME::GameProjectSessionRuntimeExtension* sessionOwner =
        world->Services().Find<RUNTIME::GameProjectSessionRuntimeExtension>();
    return sessionOwner != nullptr ? sessionOwner->GetGameSession() : nullptr;
}

bool DialogueView::Start(std::string_view assetGuid, uint64_t actorObjectHandle, std::string_view locale,
                         uint8_t pauseWorld) const noexcept {
    try {
        World* world = ResolveDialogueWorld(world_);
        if (world == nullptr || !IsValidAssetGuid(assetGuid) || actorObjectHandle == 0u || locale.size() > 32u) {
            HIKARI_LOG_WARN("[GameProject][Dialogue] Start rejected: invalid "
                            "world, asset GUID, actor, or locale.");
            return false;
        }
        DIALOGUE::DialogueSessionService* service = world->Services().Find<DIALOGUE::DialogueSessionService>();
        const RuntimeObjectHandle actor = RuntimeObjectHandle::FromValue(actorObjectHandle);
        if (service == nullptr) {
            HIKARI_LOG_WARN("[GameProject][Dialogue] Start rejected: session "
                            "service is unavailable.");
            return false;
        }
        if (service->IsActive()) {
            HIKARI_LOG_WARN("[GameProject][Dialogue] Start rejected: another "
                            "session is active.");
            return false;
        }
        if (world->FindObject(actor) == nullptr) {
            HIKARI_LOG_WARN("[GameProject][Dialogue] Start rejected: actor "
                            "handle is not present in the current World.");
            return false;
        }
        const bool started = service->Start(*world, AssetGuid{std::string(assetGuid)}, std::string(locale), actor,
                                            actor, pauseWorld != 0u);
        if (!started) {
            HIKARI_LOG_WARN("[GameProject][Dialogue] Session rejected asset " + std::string(assetGuid) + ".");
        } else {
            HIKARI_LOG_INFO("[GameProject][Dialogue] Session started asset " + std::string(assetGuid) + ".");
        }
        return started;
    } catch (...) {
        return false;
    }
}

bool DialogueView::IsActive() const noexcept {
    try {
        World* world = ResolveDialogueWorld(world_);
        DIALOGUE::DialogueSessionService* service =
            world != nullptr ? world->Services().Find<DIALOGUE::DialogueSessionService>() : nullptr;
        return service != nullptr && service->IsActive();
    } catch (...) {
        return false;
    }
}

bool DialogueView::Stop() const noexcept {
    try {
        World* world = ResolveDialogueWorld(world_);
        DIALOGUE::DialogueSessionService* service =
            world != nullptr ? world->Services().Find<DIALOGUE::DialogueSessionService>() : nullptr;
        if (service == nullptr || !service->IsActive()) {
            return false;
        }
        service->Stop(*world, "game-project-stop");
        return true;
    } catch (...) {
        return false;
    }
}

bool DialogueView::ConsumeEvent(GameProjectDialogueEvent& outEvent) const noexcept {
    outEvent = {};
    try {
        World* world = ResolveDialogueWorld(world_);
        DIALOGUE::DialogueSessionService* service =
            world != nullptr ? world->Services().Find<DIALOGUE::DialogueSessionService>() : nullptr;
        if (service == nullptr)
            return false;

        DIALOGUE::DialogueRuntimeEvent event{};
        if (!service->ConsumeEvent(event))
            return false;
        FillProjectDialogueEvent(event, outEvent);
        return true;
    } catch (...) {
        outEvent = {};
        return false;
    }
}

uint32_t DialogueView::GetPendingEventCount() const noexcept {
    try {
        World* world = ResolveDialogueWorld(world_);
        DIALOGUE::DialogueSessionService* service =
            world != nullptr ? world->Services().Find<DIALOGUE::DialogueSessionService>() : nullptr;
        return service != nullptr ? service->GetPendingEventCount() : 0u;
    } catch (...) {
        return 0u;
    }
}

uint64_t DialogueView::GetDroppedEventCount() const noexcept {
    try {
        World* world = ResolveDialogueWorld(world_);
        DIALOGUE::DialogueSessionService* service =
            world != nullptr ? world->Services().Find<DIALOGUE::DialogueSessionService>() : nullptr;
        return service != nullptr ? service->GetDroppedEventCount() : 0u;
    } catch (...) {
        return 0u;
    }
}

uint64_t DialogueView::CaptureOneShotTriggerMask() const noexcept {
    try {
        World* world = ResolveDialogueWorld(world_);
        if (world == nullptr) {
            return 0u;
        }
        const OneShotTriggers triggers(*world);
        uint64_t mask = 0u;
        for (size_t index = 0u; index < triggers.count; ++index) {
            if (triggers.entries[index].trigger != nullptr && triggers.entries[index].trigger->IsConsumed()) {
                mask |= uint64_t{1u} << index;
            }
        }
        return mask;
    } catch (...) {
        return 0u;
    }
}

uint32_t DialogueView::RestoreOneShotTriggerMask(uint64_t completedMask) const noexcept {
    try {
        World* world = ResolveDialogueWorld(world_);
        if (world == nullptr) {
            return 0u;
        }
        const OneShotTriggers triggers(*world);
        uint32_t restored = 0u;
        for (size_t index = 0u; index < triggers.count; ++index) {
            const bool consumed = (completedMask & (uint64_t{1u} << index)) != 0u;
            if (triggers.entries[index].trigger != nullptr) {
                triggers.entries[index].trigger->SetConsumed(consumed);
                restored += consumed ? 1u : 0u;
            }
        }
        return restored;
    } catch (...) {
        return 0u;
    }
}

bool SaveGameView::Exists(std::string_view slotId) const noexcept {
    try {
        return CurrentWorld(world_) != nullptr && SERVICES::GetGameProjectSaveStorageService().Exists(slotId);
    } catch (...) {
        return false;
    }
}

uint64_t SaveGameView::GetSize(std::string_view slotId) const noexcept {
    try {
        return CurrentWorld(world_) != nullptr
                   ? static_cast<uint64_t>(SERVICES::GetGameProjectSaveStorageService().Size(slotId))
                   : 0u;
    } catch (...) {
        return 0u;
    }
}

bool SaveGameView::Read(std::string_view slotId, void* outBytes, uint64_t capacity, uint64_t& outSize) const noexcept {
    outSize = 0u;
    try {
        if (CurrentWorld(world_) == nullptr || capacity > static_cast<uint64_t>((std::numeric_limits<size_t>::max)()) ||
            (capacity != 0u && outBytes == nullptr)) {
            return false;
        }
        size_t bytesRead = 0u;
        const bool result = SERVICES::GetGameProjectSaveStorageService().Read(
            slotId, std::span<std::byte>(static_cast<std::byte*>(outBytes), static_cast<size_t>(capacity)), bytesRead);
        outSize = static_cast<uint64_t>(bytesRead);
        return result;
    } catch (...) {
        outSize = 0u;
        return false;
    }
}

bool SaveGameView::WriteAtomic(std::string_view slotId, const void* bytes, uint64_t size) const noexcept {
    try {
        if (CurrentWorld(world_) == nullptr || size > static_cast<uint64_t>((std::numeric_limits<size_t>::max)()) ||
            (size != 0u && bytes == nullptr)) {
            return false;
        }
        return SERVICES::GetGameProjectSaveStorageService().Write(
            slotId, std::span<const std::byte>(static_cast<const std::byte*>(bytes), static_cast<size_t>(size)));
    } catch (...) {
        return false;
    }
}

bool SaveGameView::Delete(std::string_view slotId) const noexcept {
    try {
        return CurrentWorld(world_) != nullptr && SERVICES::GetGameProjectSaveStorageService().Delete(slotId);
    } catch (...) {
        return false;
    }
}

GameProjectRuntimeHostKind RuntimeSessionView::GetHostKind() const noexcept {
    try {
        if (CurrentWorld(world_) == nullptr) {
            return GameProjectRuntimeHostKind::Unknown;
        }
        if (SERVICES::IsEditorHost()) {
            return GameProjectRuntimeHostKind::EditorPreview;
        }
        if (SERVICES::IsStandaloneGameHost()) {
            return GameProjectRuntimeHostKind::StandaloneGame;
        }
        return GameProjectRuntimeHostKind::Unknown;
    } catch (...) {
        return GameProjectRuntimeHostKind::Unknown;
    }
}

bool RuntimeSessionView::IsPlayActive() const noexcept {
    try {
        World* world = CurrentWorld(world_);
        const RuntimePlayStateService* playState =
            world != nullptr ? world->Services().Find<RuntimePlayStateService>() : nullptr;
        return playState != nullptr && playState->IsActive();
    } catch (...) {
        return false;
    }
}

bool RuntimeSessionView::RequestEndSession() const noexcept {
    try {
        if (CurrentWorld(world_) == nullptr) {
            return false;
        }
        SERVICES::RequestRuntimeSessionEnd();
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace HIKARI
