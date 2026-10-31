#include "Diagnostics/HIKARI_MetricRegistry.h"

#include <cmath>
#include <deque>
#include <map>
#include <memory>
#include <unordered_map>
#include <utility>

#include "Core/HIKARI_RollingTimingWindow.h"
#include "Core/Text/HIKARI_EnumNames.h"

namespace HIKARI::METRIC {

namespace {

struct Entry {
    MetricDescriptor descriptor{};
    double value = 0.0;
    uint64_t sourceFrame = 0;
    bool hasData = false;
    bool unsupported = false;
    std::string note{};
    // 耗時（Work / Wait）だけが持つ。
    std::unique_ptr<PERFORMANCE::RollingTimingWindow> window{};
};

struct Registry {
    // 指標は消さないので、Entry のアドレスと MetricReading::descriptor は process が終わるまで有効。
    std::deque<Entry> entries{};
    std::map<std::string, MetricHandle, std::less<>> handleById{};
};

Registry& GetRegistry() {
    static Registry registry;
    return registry;
}

Entry* FindEntry(MetricHandle handle) {
    Registry& registry = GetRegistry();
    return handle < registry.entries.size() ? &registry.entries[handle] : nullptr;
}

void ClearEntry(Entry& entry) {
    entry.value = 0.0;
    entry.sourceFrame = 0;
    entry.hasData = false;
    entry.unsupported = false;
    entry.note.clear();
    if (entry.window) {
        *entry.window = {};
    }
}

bool HasWindow(MetricCategory category) { return category == MetricCategory::Work || category == MetricCategory::Wait; }

} // namespace

MetricHandle Register(MetricDescriptor descriptor) {
    if (descriptor.id.empty()) {
        return kInvalidMetric;
    }
    Registry& registry = GetRegistry();
    if (const auto found = registry.handleById.find(descriptor.id); found != registry.handleById.end()) {
        return found->second;
    }
    const MetricHandle handle = static_cast<MetricHandle>(registry.entries.size());
    registry.handleById.emplace(descriptor.id, handle);
    Entry& entry = registry.entries.emplace_back();
    if (HasWindow(descriptor.category)) {
        entry.window = std::make_unique<PERFORMANCE::RollingTimingWindow>();
    }
    entry.descriptor = std::move(descriptor);
    return handle;
}

void Publish(MetricHandle handle, double value, uint64_t frameIndex) {
    Entry* entry = FindEntry(handle);
    if (entry == nullptr || !std::isfinite(value)) {
        return;
    }
    entry->value = value;
    entry->sourceFrame = frameIndex;
    entry->hasData = true;
    entry->unsupported = false;
    entry->note.clear();
    if (entry->window) {
        entry->window->Push(value);
    }
}

void MarkUnsupported(MetricHandle handle, std::string_view reason) {
    Entry* entry = FindEntry(handle);
    if (entry == nullptr) {
        return;
    }
    if (entry->unsupported && entry->note == reason) {
        return;
    }
    ClearEntry(*entry);
    entry->unsupported = true;
    entry->note.assign(reason);
}

void ClearOwner(std::string_view owner) {
    for (Entry& entry : GetRegistry().entries) {
        if (entry.descriptor.owner == owner) {
            ClearEntry(entry);
        }
    }
}

std::vector<MetricReading> Snapshot(uint64_t currentFrame) {
    const std::deque<Entry>& entries = GetRegistry().entries;
    std::vector<MetricReading> readings;
    readings.reserve(entries.size());
    for (const Entry& entry : entries) {
        MetricReading& reading = readings.emplace_back();
        reading.descriptor = &entry.descriptor;
        if (entry.unsupported) {
            reading.status = MetricStatus::Unsupported;
            reading.note = entry.note;
            continue;
        }
        if (!entry.hasData) {
            continue;
        }
        reading.value = entry.value;
        reading.sourceFrame = entry.sourceFrame;
        reading.status = currentFrame > entry.sourceFrame && currentFrame - entry.sourceFrame > kStaleAfterFrames
                             ? MetricStatus::Stale
                             : MetricStatus::Fresh;
        if (entry.window) {
            const PERFORMANCE::RollingTimingWindow& window = *entry.window;
            reading.window = {window.Average(), window.p95, window.p99, window.maximum,
                              static_cast<uint32_t>(window.count)};
        }
    }
    return readings;
}

const MetricReading* FindReading(const std::vector<MetricReading>& readings, std::string_view id) {
    for (const MetricReading& reading : readings) {
        if (reading.descriptor->id == id) {
            return &reading;
        }
    }
    return nullptr;
}

std::vector<TimingRow> TimingRows(const std::vector<MetricReading>& readings, std::string_view domain) {
    // id が "<domain>.<名前><suffix>" ならその <名前> を返す。
    const auto nameOf = [domain](const MetricReading& reading, std::string_view suffix) -> std::string_view {
        const std::string_view id = reading.descriptor->id;
        const size_t overhead = domain.size() + 1u + suffix.size();
        if (id.size() <= overhead || !id.starts_with(domain) || id[domain.size()] != '.' || !id.ends_with(suffix)) {
            return {};
        }
        return id.substr(domain.size() + 1u, id.size() - overhead);
    };

    std::vector<TimingRow> rows;
    std::unordered_map<std::string_view, size_t> rowByName;
    for (const MetricReading& reading : readings) {
        if (const std::string_view name = nameOf(reading, ".ms"); !name.empty()) {
            rowByName.emplace(name, rows.size());
            rows.push_back({name, &reading});
        }
    }
    for (const MetricReading& reading : readings) {
        if (const std::string_view name = nameOf(reading, ".calls"); !name.empty()) {
            if (const auto found = rowByName.find(name); found != rowByName.end()) {
                rows[found->second].calls = &reading;
            }
        } else if (const std::string_view selfName = nameOf(reading, ".selfMs"); !selfName.empty()) {
            if (const auto found = rowByName.find(selfName); found != rowByName.end()) {
                rows[found->second].selfMs = &reading;
            }
        }
    }
    return rows;
}

const char* ToString(MetricStatus status) {
    static constexpr TEXT::EnumName<MetricStatus> names[] = {
        {MetricStatus::NoData, "noData"},
        {MetricStatus::Unsupported, "unsupported"},
        {MetricStatus::Stale, "stale"},
        {MetricStatus::Fresh, "fresh"},
    };
    return TEXT::EnumToName(names, status, "unknown");
}

const char* ToString(MetricCategory category) {
    static constexpr TEXT::EnumName<MetricCategory> names[] = {
        {MetricCategory::Work, "work"},
        {MetricCategory::Wait, "wait"},
        {MetricCategory::Count, "count"},
        {MetricCategory::Memory, "memory"},
    };
    return TEXT::EnumToName(names, category, "unknown");
}

} // namespace HIKARI::METRIC
