#include "Runtime/Diagnostics/HIKARI_RuntimeDebugService.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <json.hpp>
#include "Audio/HIKARI_Audio.h"
#include "Core/HIKARI_TimeService.h"
#include "Core/Text/HIKARI_Utf8.h"
#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/WorldSprite/HIKARI_WorldSpritePass.h"
#include "Runtime/Binding/HIKARI_RuntimeBindingStore.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Scene/HIKARI_World.h"
#include "Scene/HIKARI_SystemScheduler.h"
#include "Scene/Vfx/HIKARI_VfxPlaybackService.h"

namespace HIKARI::RUNTIME {
namespace {
using Json = nlohmann::json;
namespace GPU_PROFILE = GFX::GPU_PROFILE;
struct Command {
    const char* name;
    const char* usage;
};
constexpr Command commands[]{{"help", "help"},
                             {"world", "world [offset]"},
                             {"object", "object <authored-id>"},
                             {"systems", "systems"},
                             {"perf", "perf"},
                             {"audio", "audio"},
                             {"vfx", "vfx"},
                             {"errors", "errors"},
                             {"events", "events"},
                             {"trace", "trace <event-id|off>"},
                             {"emit", "emit <declared-event-id>"},
                             {"time", "time <0..2|reset>"},
                             {"clear", "clear"}};
template <class T>
bool Number(std::string_view value, T& result) {
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
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
    const auto& cpu = CPU_PROFILE::GetLatestSnapshot();
    const auto& gpu = GPU_PROFILE::GetLatestSnapshot();
    Json result{{"cpuAvailable", cpu.valid},
                {"cpuFrame", cpu.frameIndex},
                {"gpuAvailable", gpu.gpuTimingAvailable},
                {"gpuFrame", gpu.frameIndex},
                {"gpuUnavailableReason", gpu.unavailableReason ? gpu.unavailableReason : ""}};
    Json timings = Json::array();
    std::vector<const CPU_PROFILE::PassTiming*> cpuPasses;
    for (const auto& pass : cpu.passes)
        if (pass.valid)
            cpuPasses.push_back(&pass);
    for (uint32_t i = 0; i < cpu.dynamicPassCount && i < cpu.dynamicPasses.size(); ++i)
        if (cpu.dynamicPasses[i].valid)
            cpuPasses.push_back(&cpu.dynamicPasses[i]);
    std::stable_sort(cpuPasses.begin(), cpuPasses.end(), [](auto* a, auto* b) { return a->averageMs > b->averageMs; });
    for (size_t i = 0; i < (std::min)(cpuPasses.size(), size_t(12)); ++i) {
        const auto& pass = *cpuPasses[i];
        timings.push_back({{"name", pass.name},
                           {"meanMs", pass.averageMs},
                           {"p95Ms", pass.p95Ms},
                           {"p99Ms", pass.p99Ms},
                           {"samples", pass.rollingSampleCount}});
    }
    result["cpuTop"] = std::move(timings);
    timings = Json::array();
    std::vector<const GPU_PROFILE::PassTiming*> gpuPasses;
    for (const auto& pass : gpu.passes)
        if (pass.valid)
            gpuPasses.push_back(&pass);
    std::stable_sort(gpuPasses.begin(), gpuPasses.end(), [](auto* a, auto* b) { return a->averageMs > b->averageMs; });
    for (size_t i = 0; i < (std::min)(gpuPasses.size(), size_t(12)); ++i) {
        const auto& pass = *gpuPasses[i];
        timings.push_back({{"name", pass.name},
                           {"meanMs", pass.averageMs},
                           {"exclusiveMs", pass.exclusiveGpuMs},
                           {"p95Ms", pass.p95Ms},
                           {"samples", pass.rollingSampleCount}});
    }
    result["gpuTop"] = std::move(timings);
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
} // namespace

void RuntimeDebugService::Append(std::string text) {
    TEXT::TruncateUtf8(text, kMaximumReplyBytes);
    if (history_.size() == kMaximumHistory)
        history_.pop_front();
    history_.push_back(std::move(text));
}
void RuntimeDebugService::Reset(World& world) {
    if (timeToken_)
        SERVICES::GetActionTimeDilationService().Release(&world, timeToken_);
    timeToken_ = lastEvent_ = bindingStoreId_ = 0;
    traceEvent_.clear();
    history_.clear();
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
            Append(Json{{"event", event.eventId},
                        {"sequence", event.sequence},
                        {"frame", event.frameIndex},
                        {"source", event.sourceObject.ToValue()}}
                       .dump());
    }
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
    auto* bindings = world.Services().Find<BINDING::RuntimeBindingStore>();
    Json output;
    bool success = true;
    const auto fail = [&](std::string reason) {
        success = false;
        output = {{"error", std::move(reason)}};
    };
    try {
        const auto found =
            std::find_if(std::begin(commands), std::end(commands), [&](const auto& item) { return name == item.name; });
        if (found == std::end(commands))
            fail("Unknown command. Use help.");
        else if (name == "help") {
            output = Json::array();
            for (const auto& item : commands)
                output.push_back(item.usage);
        } else if (name == "clear") {
            history_.clear();
            return {true, "History cleared."};
        } else if (name == "world") {
            uint64_t offset = 0;
            if (!argument.empty() && !Number(argument, offset))
                fail("world expects an unsigned offset.");
            else {
                Json rows = Json::array();
                uint64_t index = 0;
                for (const auto& object : world.GetObjects()) {
                    if (index++ < offset)
                        continue;
                    if (rows.size() == 16)
                        break;
                    const auto& label = object->GetName();
                    rows.push_back({{"id", object->GetDocumentId().value},
                                    {"name", label.substr(0, TEXT::Utf8PrefixByteCount(label, 128))}});
                }
                output = {{"totalIncludingMass", world.GetTotalObjectCount()},
                          {"offset", offset},
                          {"nextOffset", offset + rows.size()},
                          {"objects", std::move(rows)}};
            }
        } else if (name == "object") {
            uint64_t id = 0;
            auto* object = Number(argument, id) ? world.FindObject(SceneObjectId{id}) : nullptr;
            if (!object)
                fail("Object not found. Use an authored id from world.");
            else
                output = ObjectDescription(*object);
        } else if (name == "systems") {
            const auto* scheduler = world.Services().Find<SystemScheduler>();
            if (!scheduler)
                fail("No system scheduler attached.");
            else
                output = scheduler->GetExecutionOrder();
        } else if (name == "perf")
            output = Performance();
        else if (name == "audio") {
            const auto s = AUDIO::GetStats();
            output = {{"activeVoices", s.activeVoices},   {"pausedVoices", s.pausedVoices},
                      {"spatialVoices", s.spatialVoices}, {"streamingVoices", s.streamingVoices},
                      {"residentBytes", s.residentBytes}, {"streamBufferBytes", s.streamBufferBytes},
                      {"rejectedPlays", s.rejectedPlays}, {"stolenVoices", s.stolenVoices}};
        } else if (name == "vfx") {
            const auto* vfx = world.Services().Find<VFX::VfxPlaybackService>();
            if (!vfx)
                fail("VFX feature is not installed.");
            else {
                const auto& s = vfx->GetAdmissionStats();
                output = {{"instances", s.activeInstances},
                          {"emitters", s.activeEmitterInstances},
                          {"budgetRejections", s.budgetRejections},
                          {"pendingDelivery", vfx->HasPendingRenderInput()}};
            }
        } else if (name == "errors") {
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
            output = {{"failures", std::move(rows)},
                      {"count", world.GetCommandFailureCount()},
                      {"overflow", world.GetCommandFailureOverflowCount()},
                      {"note", "Current frame only; enable a specific event trace for a retained timeline."}};
        } else if (name == "events") {
            if (!bindings)
                fail("Runtime binding catalog is unavailable.");
            else {
                output = Json::array();
                for (const auto& event : bindings->GetCatalog().events) {
                    if (output.size() == 32)
                        break;
                    output.push_back({{"id", event.id}, {"payload", event.hasPayload}});
                }
            }
        } else if (name == "trace") {
            if (argument == "off") {
                traceEvent_.clear();
                output = "Trace disabled.";
            } else if (!bindings || !bindings->GetCatalog().FindEvent(argument))
                fail("Choose an event declared in events.");
            else {
                traceEvent_ = argument;
                bindingStoreId_ = bindings->GetInstanceId();
                lastEvent_ = bindings->GetEvents().empty() ? 0 : bindings->GetEvents().back().sequence;
                output = "Tracing " + traceEvent_ + " (bounded 128-entry history).";
            }
        } else if (name == "emit") {
            const auto* event = bindings ? bindings->GetCatalog().FindEvent(argument) : nullptr;
            if (!event || event->hasPayload)
                fail("emit requires a declared event without a payload.");
            else if (!bindings->Emit(argument))
                fail("Event rejected by the runtime budget.");
            else
                output =
                    "Event admitted; use trace to inspect delivery. Admission does not prove a gameplay listener succeeded.";
        } else if (name == "time") {
            auto& service = SERVICES::GetActionTimeDilationService();
            float scale = 1;
            if (argument != "reset" && (!Number(argument, scale) || !std::isfinite(scale) || scale < 0 || scale > 2))
                fail("time expects a finite scale in [0,2], or reset.");
            else {
                const auto token =
                    argument == "reset" ? 0 : service.Request(&world, 0x48494b4445425547ull, 20000, scale, 0);
                if (argument != "reset" && !token)
                    fail("Time request rejected.");
                else {
                    if (timeToken_ && timeToken_ != token)
                        service.Release(&world, timeToken_);
                    timeToken_ = token;
                    output = "Time request applies next host frame.";
                }
            }
        }
        std::string text = output.dump(2);
        if (text.size() > kMaximumReplyBytes) {
            success = false;
            text = "Reply exceeds diagnostic budget; narrow the query.";
        }
        Append("> " + std::string(command));
        Append(text);
        return {success, std::move(text)};
    } catch (...) {
        return {false, "Diagnostic command failed; no exception escaped the runtime."};
    }
}
} // namespace HIKARI::RUNTIME
