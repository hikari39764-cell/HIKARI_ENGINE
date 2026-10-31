#include "Diagnostics/HIKARI_TraceRecorder.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <limits>
#include <vector>

namespace HIKARI::TRACING {

namespace {

enum Track : uint8_t { kCpuTrack = 1, kGpuTrack = 2 };

struct Event {
    const char* name = "";
    const char* module = "";
    const char* parent = nullptr;
    int64_t beginNs = 0;
    int64_t durationNs = 0;
    uint64_t frame = 0;
    uint8_t track = kCpuTrack;
};

struct Recorder {
    std::vector<Event> events{};
    size_t capacity = 0;
    uint64_t dropped = 0;
    GpuClock gpuClock{};
    bool hasGpuClock = false;
};

Recorder gRecorder{};

int64_t Nanoseconds(std::chrono::steady_clock::time_point time) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch()).count();
}

void Push(const Event& event) {
    if (gRecorder.events.size() >= gRecorder.capacity) {
        ++gRecorder.dropped;
        return;
    }
    gRecorder.events.push_back(event);
}

// GPU のタイムスタンプを ns へ。較正できた時は CPU の時計に重なり、できない時は GPU 自身の時計のまま。
int64_t GpuNanoseconds(uint64_t ticks) {
    const GpuClock& clock = gRecorder.gpuClock;
    const double perSecond = static_cast<double>(clock.ticksPerSecond);
    if (clock.calibrated) {
        const double delta = (static_cast<double>(ticks) - static_cast<double>(clock.gpuTicks)) * 1.0e9 / perSecond;
        return clock.cpuNanoseconds + static_cast<int64_t>(delta);
    }
    return static_cast<int64_t>(static_cast<double>(ticks) * 1.0e9 / perSecond);
}

void AppendEscaped(std::string& out, const char* text) {
    out.push_back('"');
    for (const char* c = text; *c != '\0'; ++c) {
        const unsigned char ch = static_cast<unsigned char>(*c);
        if (ch == '"' || ch == '\\') {
            out.push_back('\\');
            out.push_back(*c);
        } else if (ch < 0x20) {
            char escaped[8]{};
            std::snprintf(escaped, sizeof(escaped), "\\u%04x", ch);
            out += escaped;
        } else {
            out.push_back(*c);
        }
    }
    out.push_back('"');
}

} // namespace

void Start(size_t eventCapacity) {
    if (detail::gRecording) {
        return;
    }
    gRecorder = {};
    gRecorder.capacity = eventCapacity;
    gRecorder.events.reserve(eventCapacity);
    detail::gRecording = true;
}

void Stop() noexcept { detail::gRecording = false; }

void Clear() noexcept {
    detail::gRecording = false;
    gRecorder = {};
}

void RecordCpu(const char* name, const char* module, std::chrono::steady_clock::time_point begin,
               std::chrono::steady_clock::time_point end, uint64_t frame) {
    const int64_t beginNs = Nanoseconds(begin);
    Push({name, module, nullptr, beginNs, (std::max)(Nanoseconds(end) - beginNs, int64_t{0}), frame, kCpuTrack});
}

void SetGpuClock(const GpuClock& clock) {
    gRecorder.gpuClock = clock;
    gRecorder.hasGpuClock = clock.ticksPerSecond != 0u;
}

bool HasGpuClock() noexcept { return gRecorder.hasGpuClock; }

void RecordGpu(const char* name, const char* module, const char* parent, uint64_t beginTicks, uint64_t endTicks,
               uint64_t frame) {
    if (!gRecorder.hasGpuClock) {
        return;
    }
    const int64_t beginNs = GpuNanoseconds(beginTicks);
    Push({name, module, parent, beginNs, (std::max)(GpuNanoseconds(endTicks) - beginNs, int64_t{0}), frame, kGpuTrack});
}

bool Write(const std::filesystem::path& path, uint64_t firstFrame, uint64_t lastFrame, TraceStats& stats,
           std::string& error) {
    std::vector<Event> selected;
    selected.reserve(gRecorder.events.size());
    for (const Event& event : gRecorder.events) {
        if (event.frame >= firstFrame && event.frame <= lastFrame) {
            selected.push_back(event);
        }
    }

    // 較正できていない GPU の時計は CPU の時計と無関係なので、先頭を CPU 側の先頭へ揃えるだけにする。
    int64_t cpuFirst = (std::numeric_limits<int64_t>::max)();
    int64_t gpuFirst = (std::numeric_limits<int64_t>::max)();
    for (const Event& event : selected) {
        int64_t& first = event.track == kCpuTrack ? cpuFirst : gpuFirst;
        first = (std::min)(first, event.beginNs);
    }
    const bool aligned = gRecorder.gpuClock.calibrated;
    const int64_t gpuShift = !aligned && cpuFirst != (std::numeric_limits<int64_t>::max)() &&
                                     gpuFirst != (std::numeric_limits<int64_t>::max)()
                                 ? cpuFirst - gpuFirst
                                 : 0;
    for (Event& event : selected) {
        if (event.track == kGpuTrack) {
            event.beginNs += gpuShift;
        }
    }
    int64_t origin = (std::numeric_limits<int64_t>::max)();
    for (const Event& event : selected) {
        origin = (std::min)(origin, event.beginNs);
    }
    if (selected.empty()) {
        origin = 0;
    }
    // 同じ開始なら長い方（外側）を先に書く。入れ子はこの順で正しく重なる。
    std::sort(selected.begin(), selected.end(), [](const Event& lhs, const Event& rhs) {
        if (lhs.track != rhs.track) {
            return lhs.track < rhs.track;
        }
        if (lhs.beginNs != rhs.beginNs) {
            return lhs.beginNs < rhs.beginNs;
        }
        return lhs.durationNs > rhs.durationNs;
    });

    std::error_code directoryError;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), directoryError);
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "Could not open the trace file for writing.";
        return false;
    }

    std::string text;
    text.reserve(1u << 20);
    text += "{\"displayTimeUnit\":\"ms\",\"traceEvents\":[\n";
    text += "{\"ph\":\"M\",\"pid\":1,\"tid\":0,\"name\":\"process_name\",\"args\":{\"name\":\"HIKARI\"}},\n";
    text += "{\"ph\":\"M\",\"pid\":1,\"tid\":1,\"name\":\"thread_name\",\"args\":{\"name\":\"CPU (main thread)\"}},\n";
    text += "{\"ph\":\"M\",\"pid\":1,\"tid\":2,\"name\":\"thread_name\",\"args\":{\"name\":\"GPU (direct queue)\"}}";
    char number[192]{};
    for (const Event& event : selected) {
        text += ",\n{\"ph\":\"X\",\"pid\":1,\"tid\":";
        text += event.track == kCpuTrack ? '1' : '2';
        text += ",\"cat\":";
        AppendEscaped(text, event.module);
        text += ",\"name\":";
        AppendEscaped(text, event.name);
        std::snprintf(number, sizeof(number), ",\"ts\":%.3f,\"dur\":%.3f,\"args\":{\"frame\":%llu",
                      static_cast<double>(event.beginNs - origin) / 1000.0,
                      static_cast<double>(event.durationNs) / 1000.0, static_cast<unsigned long long>(event.frame));
        text += number;
        if (event.parent != nullptr) {
            text += ",\"parent\":";
            AppendEscaped(text, event.parent);
        }
        text += "}}";
        if (text.size() >= (1u << 20)) {
            file.write(text.data(), static_cast<std::streamsize>(text.size()));
            text.clear();
        }
    }
    stats.events = selected.size();
    stats.dropped = gRecorder.dropped;
    stats.gpuClockAligned = aligned;
    std::snprintf(number, sizeof(number), "],\"metadata\":{\"eventCount\":%llu,\"droppedEvents\":%llu,",
                  static_cast<unsigned long long>(stats.events), static_cast<unsigned long long>(stats.dropped));
    text += "\n";
    text += number;
    std::snprintf(number, sizeof(number), "\"firstFrame\":%llu,\"lastFrame\":%llu,\"gpuClockAligned\":%s}}\n",
                  static_cast<unsigned long long>(firstFrame), static_cast<unsigned long long>(lastFrame),
                  aligned ? "true" : "false");
    text += number;
    file.write(text.data(), static_cast<std::streamsize>(text.size()));
    file.flush();
    if (!file) {
        error = "Writing the trace file failed.";
        return false;
    }
    return true;
}

} // namespace HIKARI::TRACING
