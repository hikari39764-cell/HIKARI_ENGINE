#include "Runtime/Diagnostics/HIKARI_RuntimeDebugService.h"
#include <algorithm>
#include <utility>
#include <json.hpp>
#include "Audio/HIKARI_Audio.h"
#include "Core/HIKARI_TimeService.h"
#include "Core/Math/HIKARI_MathValidation.h"
#include "Core/Text/HIKARI_ParseNumber.h"
#include "Core/Text/HIKARI_Utf8.h"
#include "Diagnostics/HIKARI_MetricRegistry.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/WorldSprite/HIKARI_WorldSpritePass.h"
#include "Runtime/Binding/HIKARI_RuntimeBindingStore.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Scene/HIKARI_World.h"
#include "Scene/HIKARI_SystemScheduler.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"

namespace HIKARI::RUNTIME {
namespace {
using Json = nlohmann::json;
DebugReply OkReply(const Json& output) { return {true, output.dump(2)}; }
DebugReply ErrorReply(std::string reason) { return {false, Json{{"error", std::move(reason)}}.dump(2)}; }
// Play 中だけ意味のあるコマンドの前提。編集中の World ではゲームが動いていないので、効果も観測もできない。
bool PlayRunning(World& world) {
    const auto* play = world.Services().Find<RuntimePlayStateService>();
    return play != nullptr && play->IsActive();
}
DebugReply NeedsPlay(std::string_view command) {
    return ErrorReply(std::string(command) + " works only while Play is running. Start Play first.");
}
Json ObjectDescription(const GameObject& object) {
    const auto& transform = object.GetTransform();
    Json types = Json::array();
    for (const auto& component : object.GetComponents()) {
        if (types.size() == 32)
            break;
        const std::string_view type = component->GetTypeName();
        types.push_back(type.substr(0, TEXT::Utf8PrefixByteCount(type, 128)));
    }
    const auto position = transform.position;
    return {{"id", object.GetDocumentId().value},
            {"handle", object.GetRuntimeHandle().ToValue()},
            {"name", object.GetName().substr(0, TEXT::Utf8PrefixByteCount(object.GetName(), 128))},
            {"localPosition", {position.x, position.y, position.z}},
            {"components", std::move(types)}};
}
Json Performance() {
    const std::vector<METRIC::MetricReading> metrics = METRIC::Snapshot(TIME::GetFrameContext().frameIndex);
    const METRIC::MetricReading* cpuFrame = METRIC::FindReading(metrics, METRIC::kFrameCpuMs);
    const METRIC::MetricReading* gpuFrame = METRIC::FindReading(metrics, METRIC::kFrameGpuMs);
    const auto statusOf = [](const METRIC::MetricReading* reading) {
        return METRIC::ToString(reading != nullptr ? reading->status : METRIC::MetricStatus::NoData);
    };
    Json result{{"cpuStatus", statusOf(cpuFrame)},
                {"cpuFrame", cpuFrame != nullptr ? cpuFrame->sourceFrame : 0u},
                {"gpuStatus", statusOf(gpuFrame)},
                {"gpuFrame", gpuFrame != nullptr ? gpuFrame->sourceFrame : 0u},
                {"gpuUnavailableReason", gpuFrame != nullptr && gpuFrame->status == METRIC::MetricStatus::Unsupported
                                             ? std::string(gpuFrame->note)
                                             : std::string()}};
    // 平均の大きい順に上位 12 件。新しい値（Fresh）のある区間だけを並べる。
    const auto top = [&metrics](std::string_view domain, bool gpu) {
        std::vector<METRIC::TimingRow> rows = METRIC::TimingRows(metrics, domain);
        rows.erase(
            std::remove_if(rows.begin(), rows.end(),
                           [](const METRIC::TimingRow& row) { return row.ms->status != METRIC::MetricStatus::Fresh; }),
            rows.end());
        std::stable_sort(rows.begin(), rows.end(), [](const METRIC::TimingRow& a, const METRIC::TimingRow& b) {
            return a.ms->window.average > b.ms->window.average;
        });
        Json timings = Json::array();
        for (size_t i = 0; i < (std::min)(rows.size(), size_t(12)); ++i) {
            const METRIC::TimingRow& row = rows[i];
            Json timing{{"name", std::string(row.name)},
                        {"meanMs", row.ms->window.average},
                        {"p95Ms", row.ms->window.p95},
                        {"samples", row.ms->window.sampleCount}};
            if (gpu) {
                timing["exclusiveMs"] = row.selfMs != nullptr ? row.selfMs->value : 0.0;
                timing["calls"] = row.calls != nullptr ? static_cast<uint32_t>(row.calls->value) : 0u;
            } else {
                timing["p99Ms"] = row.ms->window.p99;
            }
            timings.push_back(std::move(timing));
        }
        return timings;
    };
    result["cpuTop"] = top("cpu", false);
    result["gpuTop"] = top("gpu", true);
    const auto& sprites = MESHRENDERER::GetWorldSpritePassStats();
    result["worldSprites"] = {{"submitted", sprites.submittedCount},
                              {"uploadBytes", sprites.uploadedBytes},
                              {"residentBytes", sprites.residentBytes},
                              {"textureLoadRequests", sprites.textureLoadRequests},
                              {"missingTextures", sprites.missingTextureCount},
                              {"dropped", sprites.droppedCount}};
    result["note"] = "Nested timings overlap; do not sum parent and child passes. GPU results are delayed.";
    return result;
}

DebugReply WorldCommand(World& world, std::string_view argument) {
    uint64_t offset = 0;
    if (!argument.empty() && !TEXT::ParseNumber(argument, offset))
        return ErrorReply("world expects an unsigned offset.");
    Json rows = Json::array();
    uint64_t index = 0;
    for (const auto& object : world.GetObjects()) {
        if (index++ < offset)
            continue;
        if (rows.size() == 16)
            break;
        const auto& label = object->GetName();
        rows.push_back(
            {{"id", object->GetDocumentId().value}, {"name", label.substr(0, TEXT::Utf8PrefixByteCount(label, 128))}});
    }
    return OkReply({{"totalIncludingMass", world.GetTotalObjectCount()},
                    {"offset", offset},
                    {"nextOffset", offset + rows.size()},
                    {"objects", std::move(rows)}});
}
DebugReply ObjectCommand(World& world, std::string_view argument) {
    uint64_t id = 0;
    const auto* object = TEXT::ParseNumber(argument, id) ? world.FindObject(SceneObjectId{id}) : nullptr;
    if (!object)
        return ErrorReply("Object not found. Use an authored id from world.");
    return OkReply(ObjectDescription(*object));
}
DebugReply SystemsCommand(World& world, std::string_view) {
    const auto* scheduler = world.Services().Find<SystemScheduler>();
    if (!scheduler)
        return ErrorReply("No system scheduler attached.");
    return OkReply(scheduler->GetExecutionOrder());
}
DebugReply AudioCommand(World&, std::string_view) {
    const auto s = AUDIO::GetStats();
    return OkReply({{"activeVoices", s.activeVoices},
                    {"pausedVoices", s.pausedVoices},
                    {"spatialVoices", s.spatialVoices},
                    {"streamingVoices", s.streamingVoices},
                    {"residentBytes", s.residentBytes},
                    {"streamBufferBytes", s.streamBufferBytes},
                    {"rejectedPlays", s.rejectedPlays},
                    {"stolenVoices", s.stolenVoices}});
}
DebugReply VfxCommand(World& world, std::string_view) {
    const auto* vfx = world.Services().Find<VFX::VfxPlaybackService>();
    if (!vfx)
        return ErrorReply("VFX feature is not installed.");
    const auto& s = vfx->GetAdmissionStats();
    return OkReply({{"instances", s.activeInstances},
                    {"emitters", s.activeEmitterInstances},
                    {"budgetRejections", s.budgetRejections},
                    {"pendingDelivery", vfx->HasPendingRenderInput()}});
}
DebugReply ErrorsCommand(World& world, std::string_view) {
    Json rows = Json::array();
    for (uint32_t i = 0; i < (std::min)(world.GetCommandFailureCount(), 32u); ++i) {
        WorldCommandFailure f;
        if (!world.ReadCommandFailure(i, f))
            break;
        rows.push_back({{"token", f.token},
                        {"kind", f.kind},
                        {"result", f.result},
                        {"phase", f.phase},
                        {"object", f.object.ToValue()}});
    }
    return OkReply({{"failures", std::move(rows)},
                    {"count", world.GetCommandFailureCount()},
                    {"overflow", world.GetCommandFailureOverflowCount()},
                    {"note", "Current frame only; enable a specific event trace for a retained timeline."}});
}
DebugReply EventsCommand(World& world, std::string_view) {
    const auto* bindings = world.Services().Find<BINDING::RuntimeBindingStore>();
    if (!bindings)
        return ErrorReply("Runtime binding catalog is unavailable.");
    Json output = Json::array();
    for (const auto& event : bindings->GetCatalog().events) {
        if (output.size() == 32)
            break;
        output.push_back({{"id", event.id}, {"payload", event.hasPayload}});
    }
    return OkReply(output);
}
DebugReply EmitCommand(World& world, std::string_view argument) {
    if (!PlayRunning(world))
        return NeedsPlay("emit");
    auto* bindings = world.Services().Find<BINDING::RuntimeBindingStore>();
    const auto* event = bindings ? bindings->GetCatalog().FindEvent(argument) : nullptr;
    if (!event || event->hasPayload)
        return ErrorReply("emit requires a declared event without a payload.");
    if (!bindings->Emit(argument))
        return ErrorReply("Event rejected by the runtime budget.");
    return OkReply(
        "Event admitted; use trace to inspect delivery. Admission does not prove a gameplay listener succeeded.");
}
} // namespace

RuntimeDebugService::RuntimeDebugService() {
    RegisterCommand("help", "help", [this](World&, std::string_view) {
        Json usages = Json::array();
        for (const DebugCommand& command : commands_)
            usages.push_back(command.usage);
        return OkReply(usages);
    });
    RegisterCommand("world", "world [offset]", WorldCommand);
    RegisterCommand("object", "object <authored-id>", ObjectCommand);
    RegisterCommand("systems", "systems", SystemsCommand);
    RegisterCommand("perf", "perf", [](World&, std::string_view) { return OkReply(Performance()); });
    RegisterCommand("audio", "audio", AudioCommand);
    RegisterCommand("vfx", "vfx", VfxCommand);
    RegisterCommand("errors", "errors", ErrorsCommand);
    RegisterCommand("events", "events", EventsCommand);
    RegisterCommand("trace", "trace <event-id|off>",
                    [this](World& world, std::string_view argument) { return Trace(world, argument); });
    RegisterCommand("emit", "emit <declared-event-id>", EmitCommand);
    RegisterCommand("time", "time <0..2|reset>",
                    [this](World& world, std::string_view argument) { return Time(world, argument); });
}
bool RuntimeDebugService::RegisterCommand(std::string name, std::string usage, DebugCommandHandler handler) {
    const auto same = [&](const DebugCommand& item) { return item.name == name; };
    if (name.empty() || !handler || std::any_of(commands_.begin(), commands_.end(), same))
        return false;
    commands_.push_back({std::move(name), std::move(usage), std::move(handler)});
    return true;
}
void RuntimeDebugService::AppendTraceLine(std::string text) {
    TEXT::TruncateUtf8(text, kMaximumReplyBytes);
    if (traceLines_.size() == kMaximumTraceLines)
        traceLines_.pop_front();
    traceLines_.push_back(std::move(text));
}
std::deque<std::string> RuntimeDebugService::TakeTraceLines() { return std::exchange(traceLines_, {}); }
void RuntimeDebugService::Reset(World& world) {
    if (timeToken_)
        SERVICES::GetActionTimeDilationService().Release(&world, timeToken_);
    timeToken_ = lastEvent_ = bindingStoreId_ = 0;
    traceEvent_.clear();
    traceLines_.clear();
    enabled_ = false;
}
void RuntimeDebugService::SetEnabled(World& world, bool enabled) {
    if (!world.IsOwnerThread())
        return;
    if (!enabled && enabled_)
        Reset(world);
    enabled_ = enabled;
}
void RuntimeDebugService::Observe(World& world) {
    if (!enabled_ || traceEvent_.empty() || !world.IsOwnerThread())
        return;
    auto* bindings = world.Services().Find<BINDING::RuntimeBindingStore>();
    if (!bindings)
        return;
    if (bindingStoreId_ != bindings->GetInstanceId()) {
        bindingStoreId_ = bindings->GetInstanceId();
        lastEvent_ = 0;
    }
    for (const auto& event : bindings->GetEvents()) {
        if (event.sequence <= lastEvent_)
            continue;
        lastEvent_ = event.sequence;
        if (event.eventId == traceEvent_)
            AppendTraceLine(Json{{"event", event.eventId},
                                 {"sequence", event.sequence},
                                 {"frame", event.frameIndex},
                                 {"source", event.sourceObject.ToValue()}}
                                .dump());
    }
}
DebugReply RuntimeDebugService::Trace(World& world, std::string_view argument) {
    if (argument == "off") {
        traceEvent_.clear();
        return OkReply("Trace disabled.");
    }
    if (!PlayRunning(world))
        return NeedsPlay("trace");
    const auto* bindings = world.Services().Find<BINDING::RuntimeBindingStore>();
    if (!bindings || !bindings->GetCatalog().FindEvent(argument))
        return ErrorReply("Choose an event declared in events.");
    traceEvent_ = argument;
    bindingStoreId_ = bindings->GetInstanceId();
    lastEvent_ = bindings->GetEvents().empty() ? 0 : bindings->GetEvents().back().sequence;
    return OkReply("Tracing " + traceEvent_ + " (bounded 128-entry history).");
}
DebugReply RuntimeDebugService::Time(World& world, std::string_view argument) {
    if (!PlayRunning(world))
        return NeedsPlay("time");
    auto& service = SERVICES::GetActionTimeDilationService();
    float scale = 1;
    const bool reset = argument == "reset";
    if (!reset && (!TEXT::ParseNumber(argument, scale) || !MATH::IsFinite(scale) || scale < 0 || scale > 2))
        return ErrorReply("time expects a finite scale in [0,2], or reset.");
    const auto token = reset ? 0 : service.Request(&world, 0x48494b4445425547ull, 20000, scale, 0);
    if (!reset && !token)
        return ErrorReply("Time request rejected.");
    if (timeToken_ && timeToken_ != token)
        service.Release(&world, timeToken_);
    timeToken_ = token;
    return OkReply("Time request applies next host frame.");
}
DebugReply RuntimeDebugService::Execute(World& world, std::string_view command) {
    if (!world.IsOwnerThread())
        return {false, "Debug commands require the World owner thread."};
    if (!enabled_)
        return {false, "Developer diagnostics are disabled for this session."};
    if (command.empty() || command.size() > 512 || command.find('\0') != std::string_view::npos ||
        !TEXT::IsValidUtf8(command))
        return {false, "Expected a command of 1..512 UTF-8 bytes."};
    const auto first = command.find_first_not_of(" \t");
    if (first == std::string_view::npos)
        return {false, "Empty command."};
    command.remove_prefix(first);
    command = command.substr(0, command.find_last_not_of(" \t") + 1);
    const auto split = command.find_first_of(" \t");
    const auto name = command.substr(0, split);
    std::string_view argument = split == std::string_view::npos ? std::string_view{} : command.substr(split + 1);
    if (auto pos = argument.find_first_not_of(" \t"); pos != std::string_view::npos)
        argument.remove_prefix(pos);
    try {
        const auto found = std::find_if(commands_.begin(), commands_.end(),
                                        [&](const DebugCommand& item) { return name == item.name; });
        DebugReply reply =
            found == commands_.end() ? ErrorReply("Unknown command. Use help.") : found->handler(world, argument);
        if (reply.text.size() > kMaximumReplyBytes)
            return {false, "Reply exceeds diagnostic budget; narrow the query."};
        return reply;
    } catch (...) {
        return {false, "Diagnostic command failed; no exception escaped the runtime."};
    }
}
} // namespace HIKARI::RUNTIME
