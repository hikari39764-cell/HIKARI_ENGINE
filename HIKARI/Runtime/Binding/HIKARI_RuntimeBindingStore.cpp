#include "Runtime/Binding/HIKARI_RuntimeBindingStore.h"

#include <algorithm>
#include <utility>

namespace HIKARI::BINDING {
namespace {
std::atomic<uint64_t> gNextStoreInstanceId{1u};
thread_local std::shared_ptr<const Snapshot> gCurrentSnapshot{};

uint64_t AllocateStoreInstanceId() noexcept {
    uint64_t id = gNextStoreInstanceId.fetch_add(1u, std::memory_order_relaxed);
    if (id == 0u) {
        id = gNextStoreInstanceId.fetch_add(1u, std::memory_order_relaxed);
    }
    return id;
}
} // namespace

RuntimeBindingStore::RuntimeBindingStore() noexcept : instanceId_(AllocateStoreInstanceId()) {}

const Value* Snapshot::Find(std::string_view id) const noexcept {
    const auto found = values.find(std::string(id));
    return found == values.end() ? nullptr : &found->second;
}

bool RuntimeBindingStore::Initialize(Catalog catalog) {
    Clear();
    return ReplaceCatalog(std::move(catalog), false);
}

bool RuntimeBindingStore::ReloadCatalog(Catalog catalog) { return ReplaceCatalog(std::move(catalog), true); }

void RuntimeBindingStore::Clear() {
    catalog_ = {};
    values_.clear();
    writers_.clear();
    events_.clear();
    publishedSnapshot_.store({}, std::memory_order_release);
    revision_ = 0u;
    eventSequence_ = 0u;
    frameIndex_ = 0u;
    eventAdmissionStats_ = {};
    snapshotDirty_ = true;
}

const Catalog& RuntimeBindingStore::GetCatalog() const noexcept { return catalog_; }

uint64_t RuntimeBindingStore::GetInstanceId() const noexcept { return instanceId_; }

uint64_t RuntimeBindingStore::GetRevision() const noexcept { return revision_; }

bool RuntimeBindingStore::ReadVariable(std::string_view variableId, Value& outValue) const noexcept {
    const auto found = values_.find(std::string(variableId));
    if (found == values_.end()) {
        return false;
    }
    outValue = found->second;
    return true;
}

bool RuntimeBindingStore::WriteVariable(std::string_view variableId, const Value& value, std::string_view writerId) {
    if (writerId.empty()) {
        return false;
    }
    const VariableDefinition* definition = catalog_.FindVariable(variableId);
    if (definition == nullptr || !IsValueCompatible(value, definition->defaultValue.type) || !IsValueFinite(value) ||
        (definition->hasRange && !IsValueWithinRange(value, definition->minimum, definition->maximum))) {
        return false;
    }

    const std::string id(variableId);
    auto owner = writers_.find(id);
    if (owner == writers_.end()) {
        writers_.emplace(id, writerId);
    } else if (owner->second != writerId) {
        return false;
    }
    auto current = values_.find(id);
    if (current == values_.end() || current->second != value) {
        values_[id] = value;
        ++revision_;
        snapshotDirty_ = true;
    }
    return true;
}

bool RuntimeBindingStore::ReleaseWriter(std::string_view variableId, std::string_view writerId) noexcept {
    const auto found = writers_.find(std::string(variableId));
    if (found == writers_.end() || found->second != writerId) {
        return false;
    }
    writers_.erase(found);
    return true;
}

bool RuntimeBindingStore::ResetVariableToDefault(std::string_view variableId, std::string_view writerId) {
    const VariableDefinition* definition = catalog_.FindVariable(variableId);
    if (definition == nullptr) {
        return false;
    }
    const std::string id(variableId);
    const auto writer = writers_.find(id);
    if (writer == writers_.end() || writer->second != writerId) {
        return false;
    }
    const auto current = values_.find(id);
    if (current == values_.end() || current->second != definition->defaultValue) {
        values_[id] = definition->defaultValue;
        ++revision_;
        snapshotDirty_ = true;
    }
    writers_.erase(writer);
    return true;
}

std::string_view RuntimeBindingStore::GetWriter(std::string_view variableId) const noexcept {
    const auto found = writers_.find(std::string(variableId));
    return found == writers_.end() ? std::string_view{} : std::string_view(found->second);
}

void RuntimeBindingStore::BeginFrame(uint64_t frameIndex) {
    if (frameIndex_ != frameIndex) {
        eventAdmissionStats_.acceptedThisFrame = 0u;
        eventAdmissionStats_.budgetRejectionsThisFrame = 0u;
    }
    frameIndex_ = frameIndex;
    const uint64_t oldestRetainedFrame = frameIndex_ > 0u ? frameIndex_ - 1u : 0u;
    std::erase_if(events_,
                  [oldestRetainedFrame](const EventRecord& event) { return event.frameIndex < oldestRetainedFrame; });
}

bool RuntimeBindingStore::Emit(std::string_view eventId, RuntimeObjectHandle sourceObject, const Value* payload) {
    const EventDefinition* definition = catalog_.FindEvent(eventId);
    if (definition == nullptr || definition->hasPayload != (payload != nullptr) ||
        (payload != nullptr && !IsValueCompatible(*payload, definition->payloadType)) ||
        (payload != nullptr && !IsValueFinite(*payload))) {
        return false;
    }
    if (eventAdmissionStats_.acceptedThisFrame >= kMaximumRuntimeBindingEventsPerFrame) {
        ++eventAdmissionStats_.budgetRejectionsThisFrame;
        ++eventAdmissionStats_.budgetRejectionsTotal;
        return false;
    }
    EventRecord record{};
    record.sequence = ++eventSequence_;
    record.frameIndex = frameIndex_;
    record.eventId = eventId;
    record.sourceObject = sourceObject;
    record.hasPayload = payload != nullptr;
    if (payload != nullptr) {
        record.payload = *payload;
    }
    events_.push_back(std::move(record));
    ++eventAdmissionStats_.acceptedThisFrame;
    return true;
}

const std::vector<EventRecord>& RuntimeBindingStore::GetEvents() const noexcept { return events_; }

const RuntimeBindingEventAdmissionStats& RuntimeBindingStore::GetEventAdmissionStats() const noexcept {
    return eventAdmissionStats_;
}

std::vector<const EventRecord*> RuntimeBindingStore::FindEvents(std::string_view eventId) const {
    return FindEventsAfter(eventId, 0u);
}

std::vector<const EventRecord*> RuntimeBindingStore::FindEventsAfter(std::string_view eventId,
                                                                     uint64_t sequence) const {
    std::vector<const EventRecord*> result{};
    for (const EventRecord& event : events_) {
        if (event.sequence > sequence && event.eventId == eventId) {
            result.push_back(&event);
        }
    }
    return result;
}

std::shared_ptr<const Snapshot> RuntimeBindingStore::PublishSnapshot() {
    std::shared_ptr<const Snapshot> current = publishedSnapshot_.load(std::memory_order_acquire);
    if (!snapshotDirty_ && current != nullptr) {
        return current;
    }
    auto snapshot = std::make_shared<Snapshot>();
    snapshot->storeInstanceId = instanceId_;
    snapshot->revision = revision_;
    snapshot->values = values_;
    publishedSnapshot_.store(snapshot, std::memory_order_release);
    snapshotDirty_ = false;
    return snapshot;
}

std::shared_ptr<const Snapshot> RuntimeBindingStore::GetPublishedSnapshot() const noexcept {
    return publishedSnapshot_.load(std::memory_order_acquire);
}

bool RuntimeBindingStore::ReplaceCatalog(Catalog catalog, bool preserveValues) {
    const std::vector<CatalogDiagnostic> diagnostics = ValidateCatalog(catalog);
    if (HasCatalogErrors(diagnostics)) {
        return false;
    }

    std::unordered_map<std::string, Value> nextValues{};
    nextValues.reserve(catalog.variables.size());
    for (const VariableDefinition& definition : catalog.variables) {
        const auto existing = values_.find(definition.id);
        const bool hasLiveWriter = writers_.contains(definition.id);
        if (preserveValues && hasLiveWriter && existing != values_.end() &&
            IsValueCompatible(existing->second, definition.defaultValue.type) && IsValueFinite(existing->second) &&
            (!definition.hasRange || IsValueWithinRange(existing->second, definition.minimum, definition.maximum))) {
            nextValues.emplace(definition.id, existing->second);
        } else {
            nextValues.emplace(definition.id, definition.defaultValue);
        }
    }
    std::erase_if(writers_, [this, &catalog](const auto& writer) {
        const VariableDefinition* previous = catalog_.FindVariable(writer.first);
        const VariableDefinition* next = catalog.FindVariable(writer.first);
        return previous == nullptr || next == nullptr || previous->defaultValue.type != next->defaultValue.type;
    });
    catalog_ = std::move(catalog);
    values_ = std::move(nextValues);
    events_.clear();
    ++revision_;
    snapshotDirty_ = true;
    (void)PublishSnapshot();
    return true;
}

ScopedSnapshot::ScopedSnapshot(std::shared_ptr<const Snapshot> snapshot) noexcept
    : previous_(std::move(gCurrentSnapshot)) {
    gCurrentSnapshot = std::move(snapshot);
}

ScopedSnapshot::~ScopedSnapshot() { gCurrentSnapshot = std::move(previous_); }

} // namespace HIKARI::BINDING
