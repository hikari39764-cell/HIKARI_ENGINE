#pragma once

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "Runtime/Binding/HIKARI_RuntimeBindingCatalog.h"

namespace HIKARI::BINDING {

    inline constexpr size_t
        kMaximumRuntimeBindingEventsPerFrame = 4096u;

    struct RuntimeBindingEventAdmissionStats {
        size_t acceptedThisFrame = 0u;
        size_t budgetRejectionsThisFrame = 0u;
        uint64_t budgetRejectionsTotal = 0u;
    };

    struct Snapshot {
        uint64_t storeInstanceId = 0u;
        uint64_t revision = 0u;
        std::unordered_map<std::string, Value> values{};

        const Value* Find(std::string_view id) const noexcept;
    };

    class RuntimeBindingStore final {
    public:
        RuntimeBindingStore() noexcept;

        // World update owns every mutating call. Render and background work
        // consume only the immutable snapshot returned by PublishSnapshot().
        bool Initialize(Catalog catalog);
        bool ReloadCatalog(Catalog catalog);
        void Clear();

        const Catalog& GetCatalog() const noexcept;
        uint64_t GetInstanceId() const noexcept;
        uint64_t GetRevision() const noexcept;

        bool ReadVariable(
            std::string_view variableId,
            Value& outValue) const noexcept;
        bool WriteVariable(
            std::string_view variableId,
            const Value& value,
            std::string_view writerId);
        bool ReleaseWriter(
            std::string_view variableId,
            std::string_view writerId) noexcept;
        // Restores the authored catalog default and releases the matching
        // writer as one world-thread operation. This is intended for bounded
        // editor previews; ordinary runtime writer release preserves value.
        bool ResetVariableToDefault(
            std::string_view variableId,
            std::string_view writerId);
        std::string_view GetWriter(
            std::string_view variableId) const noexcept;

        // Events remain observable for their emission frame and the following
        // frame. Consumers that can run before or after a producer must track
        // EventRecord::sequence and ignore records they already processed.
        void BeginFrame(uint64_t frameIndex);
        bool Emit(
            std::string_view eventId,
            RuntimeObjectHandle sourceObject = {},
            const Value* payload = nullptr);
        const std::vector<EventRecord>& GetEvents() const noexcept;
        std::vector<const EventRecord*> FindEvents(
            std::string_view eventId) const;
        std::vector<const EventRecord*> FindEventsAfter(
            std::string_view eventId,
            uint64_t sequence) const;
        const RuntimeBindingEventAdmissionStats&
            GetEventAdmissionStats() const noexcept;

        std::shared_ptr<const Snapshot> PublishSnapshot();
        std::shared_ptr<const Snapshot> GetPublishedSnapshot() const noexcept;

    private:
        bool ReplaceCatalog(Catalog catalog, bool preserveValues);

        Catalog catalog_{};
        std::unordered_map<std::string, Value> values_{};
        std::unordered_map<std::string, std::string> writers_{};
        std::vector<EventRecord> events_{};
        std::atomic<std::shared_ptr<const Snapshot>> publishedSnapshot_{};
        uint64_t instanceId_ = 0u;
        uint64_t revision_ = 0u;
        uint64_t eventSequence_ = 0u;
        uint64_t frameIndex_ = 0u;
        RuntimeBindingEventAdmissionStats eventAdmissionStats_{};
        bool snapshotDirty_ = true;
    };

    class ScopedSnapshot final {
    public:
        explicit ScopedSnapshot(
            std::shared_ptr<const Snapshot> snapshot) noexcept;
        ~ScopedSnapshot();

        ScopedSnapshot(const ScopedSnapshot&) = delete;
        ScopedSnapshot& operator=(const ScopedSnapshot&) = delete;

    private:
        std::shared_ptr<const Snapshot> previous_{};
    };

    std::shared_ptr<const Snapshot> GetCurrentSnapshot() noexcept;

} // namespace HIKARI::BINDING
