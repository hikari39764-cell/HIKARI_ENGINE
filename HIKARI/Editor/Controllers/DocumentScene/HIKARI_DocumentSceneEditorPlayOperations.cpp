#include "Editor/Controllers/DocumentScene/HIKARI_DocumentSceneEditorController.h"

#include "Assets/Models/HIKARI_ModelBounds.h"
#include "Core/HIKARI_Logger.h"
#include "Core/HIKARI_TimeService.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Editor/Authoring/HIKARI_SceneObjectObservation.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Runtime/HIKARI_RuntimeLaunchConfig.h"
#include "Platform/Paths/HIKARI_ExecutablePath.h"
#include "Editor/Agent/HIKARI_HeadlessEngineRun.h"
#include "Editor/Play/HIKARI_EditorPlaySession.h"
#include "Physics/HIKARI_PhysicsWorldService.h"
#include "Runtime/Services/HIKARI_RuntimeServices.h"
#include "Scene/Components/HIKARI_ColliderComponent.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <string>
#include <utility>

namespace HIKARI {
namespace {

using EDITOR::EditorOperationParameterType;

constexpr uint32_t kMaximumStepTicks = 3600u;
constexpr uint32_t kMaximumRunTicks = 36000u;
constexpr size_t kMaximumRunCaptures = 8u;
constexpr size_t kMaximumRuntimeObjects = 64u;
constexpr size_t kMaximumLogRecords = 500u;
constexpr size_t kMaximumRunLogRecords = 50u;
// trackObjects：記録する物体の数、記録の間隔（tick）、結果に残す最大の数。
constexpr size_t kMaximumTrackedObjects = 16u;
constexpr uint64_t kTrackSampleTicks = 30u;
constexpr size_t kMaximumTrackSamples = 21u;

constexpr TEXT::EnumName<EDITOR::EditorPlayState> kPlayStateNames[] = {
    {EDITOR::EditorPlayState::Stopped, "stopped"},          {EDITOR::EditorPlayState::Starting, "starting"},
    {EDITOR::EditorPlayState::Running, "running"},          {EDITOR::EditorPlayState::Paused, "paused"},
    {EDITOR::EditorPlayState::StopRequested, "stopping"},   {EDITOR::EditorPlayState::Draining, "stopping"},
    {EDITOR::EditorPlayState::RestoringEditor, "stopping"}, {EDITOR::EditorPlayState::Failed, "failed"},
};

constexpr TEXT::EnumName<CORE::LogLevel> kLogLevelNames[] = {
    {CORE::LogLevel::Info, "info"},
    {CORE::LogLevel::Warn, "warning"},
    {CORE::LogLevel::Error, "error"},
    {CORE::LogLevel::D3D12, "d3d12"},
};

nlohmann::json Vec3Json(const MATH::Vec3& value) { return nlohmann::json::array({value.x, value.y, value.z}); }

bool ReadVec3Parameter(const nlohmann::json& parameters, const char* key, MATH::Vec3& outValue) {
    const auto found = parameters.find(key);
    if (found == parameters.end() || !found->is_array() || found->size() != 3u)
        return false;
    for (size_t index = 0u; index < 3u; ++index) {
        if (!(*found)[index].is_number())
            return false;
    }
    outValue = {(*found)[0].get<float>(), (*found)[1].get<float>(), (*found)[2].get<float>()};
    return MATH::IsFinite(outValue.x) && MATH::IsFinite(outValue.y) && MATH::IsFinite(outValue.z);
}

// 入力轨道は引数の track（JSON 本体）か path（.hinputtrack、相対パスは项目根から）で受け取る。
bool ReadTrackParameter(const nlohmann::json& parameters, const std::filesystem::path& projectRoot,
                        INPUT::InputTrack& outTrack, std::string& outError) {
    if (parameters.contains("track"))
        return INPUT::ReadInputTrack(parameters["track"], outTrack, outError);
    std::filesystem::path path = TEXT::PathFromUtf8(parameters.value("path", std::string{}));
    if (!path.empty() && !path.is_absolute())
        path = projectRoot / path;
    if (path.empty()) {
        outError = "Provide track (an InputTrack object) or path (a .hinputtrack file).";
        return false;
    }
    return INPUT::LoadInputTrack(path, outTrack, outError);
}

// play.run と play.background の共通の引数（ticks、track / path、captureAt、trackObjects、pauseAfter）を読む。
EDITOR::EditorOperationResult ReadAgentPlayRun(const nlohmann::json& parameters,
                                               const std::filesystem::path& projectRoot,
                                               EDITOR::AGENT::AgentPlayRun& run) {
    run.ticks = parameters.value("ticks", 600u);
    run.pauseAfter = parameters.value("pauseAfter", true);
    if (parameters.contains("track") || parameters.contains("path")) {
        INPUT::InputTrack track;
        std::string error;
        if (!ReadTrackParameter(parameters, projectRoot, track, error))
            return EDITOR::OperationFailure("INVALID_INPUT_TRACK", std::move(error));
        run.input = std::move(track);
    }
    for (const nlohmann::json& tick : parameters.value("captureAt", nlohmann::json::array())) {
        if (tick.get<uint64_t>() > run.ticks)
            return EDITOR::OperationFailure("INVALID_CAPTURE_TICK", "captureAt ticks must be within ticks.");
        run.captureTicks.push_back(tick.get<uint32_t>());
    }
    if (run.captureTicks.size() > kMaximumRunCaptures)
        return EDITOR::OperationFailure("TOO_MANY_CAPTURES", "captureAt allows at most 8 ticks.");
    std::sort(run.captureTicks.begin(), run.captureTicks.end());
    run.trackObjects = parameters.value("trackObjects", std::vector<uint64_t>{});
    if (run.trackObjects.size() > kMaximumTrackedObjects)
        return EDITOR::OperationFailure("TOO_MANY_TRACKED_OBJECTS", "trackObjects allows at most 16 objects.");
    return EDITOR::OperationOk("", {});
}

// 記録する物体の今の世界位置（無くなった物は null）。
nlohmann::json SampleTrackedObjects(World& world, const std::vector<uint64_t>& objects, uint64_t tick) {
    nlohmann::json positions = nlohmann::json::object();
    for (const uint64_t id : objects) {
        const GameObject* object = world.FindObject(SceneObjectId{id});
        if (object == nullptr) {
            positions[std::to_string(id)] = nullptr;
            continue;
        }
        const auto matrix = object->GetTransform().GetWorldMatrix();
        positions[std::to_string(id)] = {std::round(matrix.m[3][0] * 1000.0f) / 1000.0f,
                                         std::round(matrix.m[3][1] * 1000.0f) / 1000.0f,
                                         std::round(matrix.m[3][2] * 1000.0f) / 1000.0f};
    }
    return {{"tick", tick}, {"positions", std::move(positions)}};
}

// 記録を最大 kMaximumTrackSamples に間引く（最初と最後は残す）。
nlohmann::json ThinSamples(const nlohmann::json& samples) {
    if (samples.size() <= kMaximumTrackSamples)
        return samples;
    nlohmann::json thinned = nlohmann::json::array();
    for (size_t index = 0u; index < kMaximumTrackSamples; ++index)
        thinned.push_back(samples[index * (samples.size() - 1u) / (kMaximumTrackSamples - 1u)]);
    return thinned;
}

nlohmann::json DescribeLogRecord(const CORE::LogRecord& record) {
    nlohmann::json entry{{"sequence", record.sequence},
                         {"level", TEXT::EnumToName(kLogLevelNames, record.level, "info")},
                         {"time", record.timestamp},
                         {"message", record.message}};
    if (!record.source.empty())
        entry["source"] = record.source;
    if (!record.resource.empty())
        entry["resource"] = record.resource;
    return entry;
}

nlohmann::json ReadLogs(uint64_t afterSequence, CORE::LogLevel minimum, size_t limit, std::string_view contains) {
    nlohmann::json records = nlohmann::json::array();
    for (const CORE::LogRecord& record : CORE::ReadLogHistory(afterSequence)) {
        if (records.size() >= limit)
            break;
        // D3D12 の記録は初期化の経過（デバイスや堆の作成）で、警告でも错误でもない。
        const bool passes = minimum == CORE::LogLevel::Info ||
                            (minimum == CORE::LogLevel::Warn &&
                             (record.level == CORE::LogLevel::Warn || record.level == CORE::LogLevel::Error)) ||
                            (minimum == CORE::LogLevel::Error && record.level == CORE::LogLevel::Error);
        if (passes && (contains.empty() || record.message.find(contains) != std::string::npos ||
                       record.source.find(contains) != std::string::npos))
            records.push_back(DescribeLogRecord(record));
    }
    return records;
}

} // namespace

void DocumentSceneEditorController::RegisterPlayOperations() {
    const auto add = [this](EDITOR::EditorOperationDefinition definition, EDITOR::EditorOperationKind kind,
                            EDITOR::EditorOperationPermission permission, std::vector<std::string> terms) {
        definition.permission = permission;
        definition.searchTerms = std::move(terms);
        operationRegistry_.Register(std::move(definition), kind, false);
    };
    const auto modify = EDITOR::EditorOperationPermission::ModifyEditorState;
    const auto read = EDITOR::EditorOperationPermission::ReadEditorState;

    add({"play.status",
         "Play state, simulation tick, remaining lockstep ticks and input-track playback. pending is true while Play is "
         "starting, stopping or stepping.",
         "runtime",
         {},
         [this](const nlohmann::json&) { return DescribePlayState(); }},
        EDITOR::EditorOperationKind::Query, read, {"play", "status", "running", "Play 状态"});
    add({"play.start",
         "Start Play in the editor's game view. Requires a saved scene and no unsaved documents (save first; saving "
         "needs the user's approval). Poll play.status until running.",
         "runtime",
         {},
         [this](const nlohmann::json&) { return StartAgentPlay(); }},
        EDITOR::EditorOperationKind::Session, modify, {"play", "start", "run game", "开始 Play", "运行游戏"});
    add({"play.stop",
         "Stop Play and restore the editor scene. Poll play.status until stopped.",
         "runtime",
         {},
         [this](const nlohmann::json&) -> EDITOR::EditorOperationResult {
             if (aiPlaySession_ == nullptr || !aiPlaySession_->IsRunning())
                 return EDITOR::OperationFailure("PLAY_NOT_RUNNING", "Play is not running.");
             if (SERVICES::GetInputService().GetPlaybackStatus().active)
                 SERVICES::GetInputService().StopPlayback();
             TIME::SetLockstepFrames(0u);
             aiPlaySession_->RequestStop();
             return DescribePlayState();
         }},
        EDITOR::EditorOperationKind::Session, modify, {"play", "stop", "停止 Play"});
    add({"play.pause",
         "Pause or resume Play. While paused, play.step advances exact fixed ticks.",
         "runtime",
         {{"paused", EditorOperationParameterType::Bool, true, "true to pause, false to resume"}},
         [this](const nlohmann::json& parameters) -> EDITOR::EditorOperationResult {
             if (aiPlaySession_ == nullptr || !aiPlaySession_->IsRunning())
                 return EDITOR::OperationFailure("PLAY_NOT_RUNNING", "Start Play first.");
             if (parameters.at("paused").get<bool>() != aiPlaySession_->IsPaused())
                 aiPlaySession_->TogglePause();
             return DescribePlayState();
         }},
        EDITOR::EditorOperationKind::Session, modify, {"pause", "resume", "暂停", "继续"});
    add({"play.step",
         "Advance Play by an exact number of fixed simulation ticks, one tick per frame regardless of wall time and "
         "pause. Poll play.status until pending is false.",
         "runtime",
         {{"ticks",
           EditorOperationParameterType::Int,
           true,
           "1 to 3600 fixed ticks",
           {{"minimum", 1}, {"maximum", kMaximumStepTicks}}}},
         [this](const nlohmann::json& parameters) -> EDITOR::EditorOperationResult {
             if (aiPlaySession_ == nullptr || !aiPlaySession_->IsRunning())
                 return EDITOR::OperationFailure("PLAY_NOT_RUNNING", "Start Play first.");
             TIME::SetLockstepFrames(parameters.at("ticks").get<uint32_t>());
             return DescribePlayState();
         }},
        EDITOR::EditorOperationKind::Session, modify, {"step", "frame", "tick", "单步", "逐帧"});
    add({"play.input",
         "Play an input track (action values by fixed tick, InputTrack format) into the running game, or stop it. The "
         "values add to real input like another device.",
         "runtime",
         {{"track", EditorOperationParameterType::Json, false, "InputTrack object"},
          {"path", EditorOperationParameterType::String, false, ".hinputtrack file, absolute or project-relative"},
          {"stop", EditorOperationParameterType::Bool, false, "true to stop the current track"}},
         [this](const nlohmann::json& parameters) -> EDITOR::EditorOperationResult {
             INPUT::InputService& input = SERVICES::GetInputService();
             if (parameters.value("stop", false)) {
                 input.StopPlayback();
                 return DescribePlayState();
             }
             if (aiPlaySession_ == nullptr || !aiPlaySession_->IsRunning() || aiOperationScene_ == nullptr)
                 return EDITOR::OperationFailure("PLAY_NOT_RUNNING", "Start Play first.");
             INPUT::InputTrack track;
             std::string error;
             if (!ReadTrackParameter(parameters, aiOperationScene_->GetAssetDatabase().GetProjectRoot(), track,
                                     error) ||
                 !input.BeginPlayback(std::move(track), error))
                 return EDITOR::OperationFailure("INVALID_INPUT_TRACK", std::move(error));
             return DescribePlayState();
         }},
        EDITOR::EditorOperationKind::Session, modify,
        {"input", "replay", "keys", "simulate input", "输入", "模拟操作"});

    EDITOR::EditorOperationDefinition run{
        "play.run",
        "One call for a gameplay check: start Play if needed, play an optional input track, advance exactly ticks fixed "
        "ticks (lockstep), capture the game view at captureAt ticks, then pause. Poll play.run.status for captures, "
        "warnings and errors.",
        "runtime",
        {{"ticks",
          EditorOperationParameterType::Int,
          true,
          "1 to 36000 fixed ticks",
          {{"minimum", 1}, {"maximum", kMaximumRunTicks}}},
         {"track", EditorOperationParameterType::Json, false, "Optional InputTrack object"},
         {"path", EditorOperationParameterType::String, false, "Optional .hinputtrack file"},
         {"captureAt",
          EditorOperationParameterType::Json,
          false,
          "Up to 8 tick numbers to capture",
          {{"type", "array"}, {"items", {{"type", "integer"}, {"minimum", 0}}}}},
         {"trackObjects",
          EditorOperationParameterType::Json,
          false,
          "Up to 16 scene object ids whose world positions are recorded every 30 ticks (evidence of where things "
          "moved)",
          {{"type", "array"}, {"items", {{"type", "integer"}, {"minimum", 1}}}}},
         {"pauseAfter", EditorOperationParameterType::Bool, false, "Pause when done (default true)"}},
        [this](const nlohmann::json& parameters) { return StartAgentPlayRun(parameters); }};
    run.entryPoint = true;
    add(std::move(run), EDITOR::EditorOperationKind::Session, modify,
        {"run", "test gameplay", "playtest", "跑一遍", "试玩", "自测"});
    add({"play.run.status",
         "Result of a play.run: pending until done, then captures, final Play state, warnings and errors.",
         "runtime",
         {{"runId", EditorOperationParameterType::Int, true, "Id returned by play.run"}},
         [this](const nlohmann::json& parameters) { return PollAgentPlayRun(parameters.at("runId").get<uint64_t>()); }},
        EDITOR::EditorOperationKind::Query, read, {});

    EDITOR::EditorOperationDefinition background{
        "play.background",
        "Play the current scene once in a separate headless editor, without saving it and without touching the "
        "user's editor, view or Play: the same run as play.run (optional input track, exactly ticks fixed ticks, game "
        "view captures at captureAt ticks, positions of trackObjects every 30 ticks), then captures, positions and "
        "log warnings and errors. Poll play.background.status. Prefer this to play.run for gameplay checks while the "
        "user is working. Only the current scene's unsaved changes are carried over; other assets (materials, VFX, "
        "cameras) are read from disk, so save them first.",
        "runtime",
        {{"ticks",
          EditorOperationParameterType::Int,
          false,
          "Fixed ticks after Play starts, 1 to 7200 (default 600; 60 ticks per second of game time)",
          {{"minimum", 1}, {"maximum", EDITOR::AGENT::kMaximumHeadlessTicks}}},
         {"track", EditorOperationParameterType::Json, false, "Optional InputTrack object"},
         {"path", EditorOperationParameterType::String, false, "Optional .hinputtrack file instead of track"},
         {"captureAt",
          EditorOperationParameterType::Json,
          false,
          "Up to 8 tick numbers to capture the game view",
          {{"type", "array"}, {"items", {{"type", "integer"}, {"minimum", 0}}}}},
         {"trackObjects",
          EditorOperationParameterType::Json,
          false,
          "Up to 16 scene object ids whose world positions are recorded every 30 ticks",
          {{"type", "array"}, {"items", {{"type", "integer"}, {"minimum", 1}}}}},
         {"quality",
          EditorOperationParameterType::String,
          false,
          "light (default: 1280x720, low GI and volumetrics, so it shares the GPU with the user) or project "
          "(1920x1080 at the project's render quality, when the captures must match the game's look)",
          {{"enum", nlohmann::json::array({"light", "project"})}}}},
        [this](const nlohmann::json& parameters) { return StartBackgroundPlay(parameters); }};
    background.entryPoint = true;
    add(std::move(background), EDITOR::EditorOperationKind::Session,
        EDITOR::EditorOperationPermission::ExecuteExternalProcess,
        {"background play", "test gameplay", "headless", "playtest", "后台试玩", "自测"});
    add({"play.background.status",
         "Result of play.background: pending while the engine runs, then captures, exit code, log warnings and errors.",
         "runtime",
         {},
         [this](const nlohmann::json&) { return backgroundPlay_.Status(); }},
        EDITOR::EditorOperationKind::Query, read, {});

    add({"runtime.objects.query",
         "Live runtime objects (the playing world during Play): world position and bounds, components and resolved "
         "collider shapes.",
         "runtime",
         {{"objectIds",
           EditorOperationParameterType::Json,
           false,
           "Scene object ids",
           {{"type", "array"}, {"items", {{"type", "integer"}}}}},
          {"keyword", EditorOperationParameterType::String, false, "Optional name substring."},
          {"limit",
           EditorOperationParameterType::Int,
           false,
           "1 to 64, default 32",
           {{"minimum", 1}, {"maximum", kMaximumRuntimeObjects}}}},
         [this](const nlohmann::json& parameters) -> EDITOR::EditorOperationResult {
             if (aiOperationScene_ == nullptr)
                 return EDITOR::OperationFailure("NO_SCENE", "No scene is open.");
             std::vector<uint64_t> ids;
             for (const nlohmann::json& id : parameters.value("objectIds", nlohmann::json::array()))
                 ids.push_back(id.get<uint64_t>());
             const std::string contains = parameters.value("keyword", std::string{});
             const size_t limit = parameters.value("limit", size_t{32});
             nlohmann::json objects = nlohmann::json::array();
             bool truncated = false;
             for (const auto& object : aiOperationScene_->GetWorld().GetObjects()) {
                 const SceneObjectId id = object->GetDocumentId();
                 if ((!ids.empty() && std::find(ids.begin(), ids.end(), id.value) == ids.end()) ||
                     (!contains.empty() && object->GetName().find(contains) == std::string::npos))
                     continue;
                 if (objects.size() >= limit) {
                     truncated = true;
                     break;
                 }
                 const auto world = object->GetTransform().GetWorldMatrix();
                 nlohmann::json entry{{"objectId", id.value},
                                      {"name", object->GetName()},
                                      {"worldPosition", Vec3Json({world.m[3][0], world.m[3][1], world.m[3][2]})}};
                 if (const Bounds bounds = EDITOR::ObserveSceneObjectBounds(*aiOperationScene_, id);
                     BOUNDS::IsUsable(bounds))
                     entry["worldBounds"] = {{"min", Vec3Json(bounds.min)}, {"max", Vec3Json(bounds.max)}};
                 nlohmann::json components = nlohmann::json::array();
                 for (const auto& component : object->GetComponents())
                     components.push_back(std::string(component->GetTypeName()));
                 entry["components"] = std::move(components);
                 if (const auto* collider = object->GetComponent<ColliderComponent>()) {
                     nlohmann::json shapes = nlohmann::json::array();
                     for (const ResolvedColliderShape& shape : collider->ResolveShapes())
                         shapes.push_back({{"shape", TEXT::EnumToName(kColliderShapeTypeNames, shape.type, "Box")},
                                           {"center", Vec3Json(shape.center)},
                                           {"size", Vec3Json(shape.size)},
                                           {"radius", shape.radius},
                                           {"height", shape.height}});
                     entry["colliderShapes"] = std::move(shapes);
                 }
                 objects.push_back(std::move(entry));
             }
             return EDITOR::OperationOk("Runtime objects.", {{"play", aiOperationScene_->IsRuntimePlayActive()},
                                                             {"objects", std::move(objects)},
                                                             {"truncated", truncated}});
         }},
        EDITOR::EditorOperationKind::Query, read, {"runtime", "position", "collider", "运行时", "位置", "碰撞"});
    add({"runtime.physics.raycast",
         "Cast a ray into the playing world's physics and return the first hit object, point, normal and distance.",
         "runtime",
         {{"origin", EditorOperationParameterType::Json, true, "World [x, y, z]"},
          {"direction", EditorOperationParameterType::Json, true, "World [x, y, z]; normalized here"},
          {"maxDistance",
           EditorOperationParameterType::Float,
           false,
           "Default 1000 m",
           {{"minimum", 0.001}, {"maximum", 100000.0}}}},
         [this](const nlohmann::json& parameters) -> EDITOR::EditorOperationResult {
             if (aiOperationScene_ == nullptr || !aiOperationScene_->IsRuntimePlayActive())
                 return EDITOR::OperationFailure("PLAY_NOT_RUNNING", "Physics queries need a running Play session.");
             World& world = aiOperationScene_->GetWorld();
             const PHYSICS::PhysicsWorldService* physics = world.Services().Find<PHYSICS::PhysicsWorldService>();
             if (physics == nullptr)
                 return EDITOR::OperationFailure("PHYSICS_UNAVAILABLE", "This world has no physics.");
             PHYSICS::PhysicsRaycastQuery query{};
             MATH::Vec3 direction{};
             if (!ReadVec3Parameter(parameters, "origin", query.origin) ||
                 !ReadVec3Parameter(parameters, "direction", direction))
                 return EDITOR::OperationFailure("INVALID_RAY", "origin and direction must be [x, y, z].");
             const float length =
                 std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
             if (!(length > 1e-6f))
                 return EDITOR::OperationFailure("INVALID_RAY", "direction must not be zero.");
             query.direction = {direction.x / length, direction.y / length, direction.z / length};
             query.maxDistance = parameters.value("maxDistance", 1000.0f);
             PHYSICS::PhysicsHit hit{};
             if (!physics->Raycast(query, hit))
                 return EDITOR::OperationOk("No hit.", {{"hit", false}});
             nlohmann::json data{{"hit", true},
                                 {"position", Vec3Json(hit.position)},
                                 {"normal", Vec3Json(hit.normal)},
                                 {"distance", hit.distance},
                                 {"isTrigger", hit.isTrigger}};
             if (const GameObject* object = world.FindObject(hit.object)) {
                 data["objectId"] = object->GetDocumentId().value;
                 data["name"] = object->GetName();
             }
             return EDITOR::OperationOk("Hit.", std::move(data));
         }},
        EDITOR::EditorOperationKind::Query, read, {"raycast", "hit test", "collision check", "射线", "碰撞检测"});
    add({"log.read",
         "Editor and game log records after a sequence number. Use nextSince for the following call.",
         "runtime",
         {{"since",
           EditorOperationParameterType::Int,
           false,
           "Return records after this sequence (default 0)",
           {{"minimum", 0}}},
          {"minLevel",
           EditorOperationParameterType::String,
           false,
           "info (default), warning or error",
           {{"enum", {"info", "warning", "error"}}}},
          {"contains", EditorOperationParameterType::String, false, "Text in the message or source"},
          {"limit",
           EditorOperationParameterType::Int,
           false,
           "1 to 500, default 100",
           {{"minimum", 1}, {"maximum", kMaximumLogRecords}}}},
         [](const nlohmann::json& parameters) -> EDITOR::EditorOperationResult {
             const std::string level = parameters.value("minLevel", std::string("info"));
             const CORE::LogLevel minimum = level == "error"     ? CORE::LogLevel::Error
                                            : level == "warning" ? CORE::LogLevel::Warn
                                                                 : CORE::LogLevel::Info;
             const CORE::LogHistoryInfo info = CORE::GetLogHistoryInfo();
             return EDITOR::OperationOk("Log records.",
                                        {{"records", ReadLogs(parameters.value("since", uint64_t{0}), minimum,
                                                              parameters.value("limit", size_t{100}),
                                                              parameters.value("contains", std::string{}))},
                                         {"nextSince", info.latestSequence},
                                         {"warnings", info.warnings},
                                         {"errors", info.errors},
                                         {"logFile", TEXT::PathToUtf8(CORE::GetLogFilePath())}});
         }},
        EDITOR::EditorOperationKind::Query, read, {"log", "errors", "warnings", "console", "日志", "报错"});
}

EDITOR::EditorOperationResult DocumentSceneEditorController::DescribePlayState() const {
    nlohmann::json data{{"state", "stopped"}, {"pending", false}};
    if (aiPlaySession_ != nullptr) {
        const EDITOR::EditorPlayState state = aiPlaySession_->GetState();
        data["state"] = TEXT::EnumToName(kPlayStateNames, state, "stopped");
        data["pending"] = aiPlaySession_->HasPendingStart() || aiPlaySession_->IsTransitioning();
        if (!aiPlaySession_->GetStatusMessage().empty())
            data["message"] = aiPlaySession_->GetStatusMessage();
    }
    const uint32_t lockstep = TIME::GetLockstepFramesRemaining();
    data["tick"] = TIME::GetFrameContext().fixedTickIndex;
    data["lockstepTicksRemaining"] = lockstep;
    if (lockstep > 0u)
        data["pending"] = true;
    const INPUT::InputService::PlaybackStatus playback = SERVICES::GetInputService().GetPlaybackStatus();
    if (playback.active)
        data["inputTrack"] = {{"tick", playback.tick}, {"length", playback.length}};
    if (data["pending"].get<bool>())
        data["poll"] = {{"operation", "play.status"}, {"parameters", nlohmann::json::object()}};
    return EDITOR::OperationOk("Play state.", std::move(data));
}

EDITOR::EditorOperationResult DocumentSceneEditorController::StartAgentPlay() {
    if (aiPlaySession_ == nullptr || aiOperationScene_ == nullptr)
        return EDITOR::OperationFailure("NO_SCENE", "No scene is open.");
    if (aiPlaySession_->IsRunning() || aiPlaySession_->IsTransitioning() || aiPlaySession_->HasPendingStart())
        return DescribePlayState();
    DocumentSceneBase& scene = *aiOperationScene_;
    if (!scene.GetCurrentSceneAssetGuid().IsValid())
        return EDITOR::OperationFailure("SCENE_NOT_SAVED", "Play needs the scene saved as an asset first.");
    // 編集器の Play ボタンは開始前に保存する。智能体からは保存（利用者の承認が要る）と開始を分ける。
    if (HasKnownUnsavedProjectChanges(scene) || renderQualitySavePending_) {
        EDITOR::EditorOperationResult result = EDITOR::OperationFailure(
            "UNSAVED_CHANGES", "Save the open documents before Play; editor.save_all asks the user for approval.");
        result.hints.push_back("Call editor.save_all, wait for approval, then play.start again.");
        return result;
    }
    CinematicsWorkspace().PrepareForRuntimePlay(scene);
    aiPlaySession_->RequestEmbeddedStart();
    return DescribePlayState();
}

EDITOR::EditorOperationResult DocumentSceneEditorController::StartAgentPlayRun(const nlohmann::json& parameters) {
    if (agentPlayRun_ && agentPlayRun_->phase != EDITOR::AGENT::AgentPlayRun::Phase::Done &&
        agentPlayRun_->phase != EDITOR::AGENT::AgentPlayRun::Phase::Failed)
        return EDITOR::OperationFailure("RUN_IN_PROGRESS", "Another play.run is still in progress.");
    if (aiOperationScene_ == nullptr)
        return EDITOR::OperationFailure("NO_SCENE", "No scene is open.");
    EDITOR::AGENT::AgentPlayRun run{};
    if (EDITOR::EditorOperationResult read =
            ReadAgentPlayRun(parameters, aiOperationScene_->GetAssetDatabase().GetProjectRoot(), run);
        !read.ok)
        return read;
    if (aiPlaySession_ == nullptr || !aiPlaySession_->IsRunning()) {
        const EDITOR::EditorOperationResult started = StartAgentPlay();
        if (!started.ok)
            return started;
    }
    run.id = nextAgentPlayRunId_++;
    run.logSequenceAtStart = CORE::GetLogHistoryInfo().latestSequence;
    agentPlayRun_ = std::move(run);
    return PollAgentPlayRun(agentPlayRun_->id);
}

void DocumentSceneEditorController::TickAgentPlayRun() {
    INPUT::InputService& input = SERVICES::GetInputService();
    // --agent-play-run（後台試玩・hikari headless の無頭の编辑器）：場面が開いたら依頼の play.run を一回走らせ、
    // 終わったら同じ目录の result.json に play.run.status と同じ結果を書く（起動した側が読んで引擎を止める）。
    if (pendingBackgroundRun_ && aiOperationScene_ != nullptr && aiPlaySession_ != nullptr) {
        const std::filesystem::path request = *std::exchange(pendingBackgroundRun_, std::nullopt);
        std::ifstream file(request, std::ios::binary);
        const nlohmann::json parameters = nlohmann::json::parse(file, nullptr, false);
        EDITOR::EditorOperationResult started =
            parameters.is_object() ? StartAgentPlayRun(parameters)
                                   : EDITOR::OperationFailure("INVALID_REQUEST", "The run request is not JSON.");
        backgroundResult_ = request.parent_path() / "result.json";
        if (!started.ok)
            WriteBackgroundResult(started);
    }
    if (backgroundResult_ && agentPlayRun_ &&
        (agentPlayRun_->phase == EDITOR::AGENT::AgentPlayRun::Phase::Done ||
         agentPlayRun_->phase == EDITOR::AGENT::AgentPlayRun::Phase::Failed))
        WriteBackgroundResult(PollAgentPlayRun(agentPlayRun_->id));
    if (!agentPlayRun_)
        return;
    using Phase = EDITOR::AGENT::AgentPlayRun::Phase;
    EDITOR::AGENT::AgentPlayRun& run = *agentPlayRun_;
    const auto fail = [&run](std::string message) {
        run.phase = Phase::Failed;
        run.error = std::move(message);
        TIME::SetLockstepFrames(0u);
    };
    if (run.phase == Phase::Done || run.phase == Phase::Failed)
        return;
    const bool starting =
        aiPlaySession_ != nullptr &&
        (aiPlaySession_->HasPendingStart() || aiPlaySession_->GetState() == EDITOR::EditorPlayState::Starting);
    if (aiPlaySession_ == nullptr || (!aiPlaySession_->IsRunning() && !starting)) {
        fail(aiPlaySession_ != nullptr && !aiPlaySession_->GetStatusMessage().empty()
                 ? aiPlaySession_->GetStatusMessage()
                 : "Play stopped before the run finished.");
        return;
    }
    const uint64_t now = TIME::GetFrameContext().fixedTickIndex;
    if (run.phase == Phase::WaitingForPlay) {
        if (!aiPlaySession_->IsRunning())
            return;
        if (aiPlaySession_->IsPaused())
            aiPlaySession_->TogglePause();
        std::string error;
        if (run.input && !input.BeginPlayback(*run.input, error)) {
            fail(std::move(error));
            return;
        }
        run.startTick = now;
        TIME::SetLockstepFrames(run.ticks);
        run.phase = Phase::Running;
        return;
    }
    const uint64_t elapsed = now - run.startTick;
    if (!run.trackObjects.empty() && elapsed >= run.nextSampleTick && elapsed <= run.ticks) {
        run.samples.push_back(SampleTrackedObjects(aiOperationScene_->GetWorld(), run.trackObjects, elapsed));
        run.nextSampleTick = elapsed + kTrackSampleTicks;
    }
    if (run.pendingCaptureId != 0u) {
        const EDITOR::EditorOperationResult capture = PollAiPreviewCapture(run.pendingCaptureId);
        if (!capture.ok || !capture.data.value("pending", false)) {
            nlohmann::json entry{{"requestedTick", run.pendingCaptureTick}, {"ok", capture.ok}};
            if (capture.ok && capture.data.contains("images") && !capture.data["images"].empty())
                entry["path"] = capture.data["images"][0].value("path", std::string{});
            else if (!capture.ok)
                entry["message"] = capture.message;
            entry["frameIndex"] = capture.data.value("frameIndex", uint64_t{0});
            run.captures.push_back(std::move(entry));
            run.pendingCaptureId = 0u;
        }
    }
    if (run.pendingCaptureId == 0u && run.nextCapture < run.captureTicks.size() &&
        elapsed >= run.captureTicks[run.nextCapture]) {
        const uint32_t tick = run.captureTicks[run.nextCapture++];
        const EDITOR::EditorOperationResult capture = ExecuteAiCapturePreview({{"source", "game"}});
        if (capture.ok && capture.data.contains("poll")) {
            run.pendingCaptureId = capture.data["poll"]["parameters"].value("captureId", uint64_t{0});
            run.pendingCaptureTick = tick;
        } else {
            nlohmann::json entry{{"requestedTick", tick}, {"ok", capture.ok}, {"message", capture.message}};
            if (capture.ok && capture.data.contains("images") && !capture.data["images"].empty())
                entry["path"] = capture.data["images"][0].value("path", std::string{});
            run.captures.push_back(std::move(entry));
        }
    }
    if (run.phase == Phase::Running && TIME::GetLockstepFramesRemaining() == 0u && elapsed >= run.ticks)
        run.phase = Phase::Finishing;
    if (run.phase == Phase::Finishing && run.pendingCaptureId == 0u && run.nextCapture >= run.captureTicks.size()) {
        // 最後の位置（間隔の途中で終わっても終点を残す）。
        if (!run.trackObjects.empty() && (run.samples.empty() || run.samples.back()["tick"] != run.ticks))
            run.samples.push_back(SampleTrackedObjects(aiOperationScene_->GetWorld(), run.trackObjects, run.ticks));
        if (run.pauseAfter && aiPlaySession_->IsRunning() && !aiPlaySession_->IsPaused())
            aiPlaySession_->TogglePause();
        run.phase = Phase::Done;
    }
}

void DocumentSceneEditorController::WriteBackgroundResult(const EDITOR::EditorOperationResult& result) {
    const std::filesystem::path path = *std::exchange(backgroundResult_, std::nullopt);
    const nlohmann::json written{
        {"ok", result.ok}, {"code", result.code}, {"message", result.message}, {"data", result.data}};
    // 読む側が書きかけを読まないよう、別名で書いてから置き換える。
    const std::filesystem::path temporary = path.parent_path() / "result.json.tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << written.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error)
        HIKARI_LOG_ERROR("[BackgroundPlay] Could not write " + TEXT::PathToUtf8(path) + ": " + error.message());
}

EDITOR::EditorOperationResult DocumentSceneEditorController::PollAgentPlayRun(uint64_t runId) const {
    if (!agentPlayRun_ || agentPlayRun_->id != runId)
        return EDITOR::OperationFailure("UNKNOWN_RUN", "No play.run with this id; only the latest run is kept.");
    using Phase = EDITOR::AGENT::AgentPlayRun::Phase;
    const EDITOR::AGENT::AgentPlayRun& run = *agentPlayRun_;
    const bool pending = run.phase != Phase::Done && run.phase != Phase::Failed;
    const uint64_t now = TIME::GetFrameContext().fixedTickIndex;
    nlohmann::json data{{"runId", run.id},
                        {"pending", pending},
                        {"ticks", run.ticks},
                        {"completedTicks", run.phase == Phase::WaitingForPlay
                                               ? 0u
                                               : (std::min)(now - run.startTick, uint64_t{run.ticks})},
                        {"captures", run.captures}};
    if (pending) {
        data["poll"] = {{"operation", "play.run.status"}, {"parameters", {{"runId", run.id}}}};
        return EDITOR::OperationOk("Run in progress.", std::move(data));
    }
    data["problems"] = ReadLogs(run.logSequenceAtStart, CORE::LogLevel::Warn, kMaximumRunLogRecords, {});
    if (!run.samples.empty())
        data["tracked"] = ThinSamples(run.samples);
    data["play"] = DescribePlayState().data;
    if (run.phase == Phase::Failed) {
        EDITOR::EditorOperationResult result = EDITOR::OperationFailure("RUN_FAILED", run.error);
        result.data = std::move(data);
        return result;
    }
    return EDITOR::OperationOk("Run finished.", std::move(data));
}

EDITOR::EditorOperationResult DocumentSceneEditorController::StartBackgroundPlay(const nlohmann::json& parameters) {
    if (aiOperationScene_ == nullptr)
        return EDITOR::OperationFailure("NO_SCENE", "No scene is open.");
    DocumentSceneBase& scene = *aiOperationScene_;
    const AssetDatabase& assets = scene.GetAssetDatabase();
    EDITOR::BackgroundPlayRun::Request request;
    request.engine = PLATFORM::GetExecutablePath();
    request.descriptor = EDITOR::AGENT::FindProjectDescriptor(assets.GetProjectRoot());
    request.libraryRoot = assets.GetLibraryRoot();
    // 编辑器と同じ项目模块で遊ばせる。
    request.projectModule = ReadRuntimeCommandLinePath(L"--project-module").value_or(std::filesystem::path{});
    request.sceneGuid = scene.GetCurrentSceneAssetGuid().value;
    request.scene = &scene.GetSceneDocument();
    EDITOR::AGENT::AgentPlayRun checked{};
    if (EDITOR::EditorOperationResult read = ReadAgentPlayRun(parameters, assets.GetProjectRoot(), checked); !read.ok)
        return read;
    request.run = parameters;
    request.run.erase("quality");
    request.run["ticks"] = checked.ticks;
    request.run["pauseAfter"] = false;
    // 相対の path は依頼を読む後ろの编辑器でも项目根から解ける。
    request.projectQuality = parameters.value("quality", std::string("light")) == "project";
    return backgroundPlay_.Start(request);
}

} // namespace HIKARI
