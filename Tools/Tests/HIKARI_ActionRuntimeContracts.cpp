#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

#include "Animation/Runtime/HIKARI_AnimationClipSampler.h"
#include "Animation/Runtime/HIKARI_AnimationNodeOverrideService.h"
#include "Animation/Runtime/HIKARI_AnimationPoseEvaluation.h"
#include "Animation/Runtime/HIKARI_AnimationRootMotionService.h"
#include "Core/HIKARI_FixedStepClock.h"
#include "Core/HIKARI_TimeService.h"
#include "Diagnostics/HIKARI_CpuFrameProfiler.h"
#include "Diagnostics/HIKARI_MetricRegistry.h"
#include "Diagnostics/HIKARI_PerformanceBreakdownAnalysis.h"
#include "Physics/HIKARI_KinematicMotionService.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Shadow/HIKARI_ShadowCachePolicy.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"
#include "Runtime/Services/HIKARI_ActionTimeDilationService.h"
#include "Scene/Camera/HIKARI_CameraRigService.h"
#include "Scene/Components/HIKARI_AnimatorComponent.h"

void CheckAudioPlaybackContracts();
void CheckAfterimageContracts();
namespace {

int gFailures = 0;

void Check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++gFailures;
    }
}

bool Near(float lhs, float rhs, float epsilon = 1.0e-4f) {
    return std::isfinite(lhs) && std::isfinite(rhs) && std::abs(lhs - rhs) <= epsilon;
}

HIKARI::RuntimeObjectHandle Object(uint32_t slot) { return {slot, 1u}; }

HIKARI::ANIMATION::AnimationRootMotionDelta RootDelta(float x, float z) {
    HIKARI::ANIMATION::AnimationRootMotionDelta delta{};
    delta.translation = {x, 0.0f, z};
    delta.rotation = HIKARI::MATH::Quat::Identity();
    delta.valid = true;
    return delta;
}

void TestFixedStepCatchUpBudget() {
    HIKARI::FixedStepClock clock{};
    Check(clock.GetSettings().maxStepsPerFrame == 3u, "fixed-step default must cap per-frame catch-up work");

    const HIKARI::FixedStepFramePlan slowFrame = clock.Advance(0.1f);
    Check(slowFrame.stepCount == 3u, "a slow frame must not schedule more than three fixed steps");
    Check(slowFrame.droppedSeconds > 0.03f, "excess fixed time must be reported instead of replayed");
    Check(clock.GetCompletedTickCount() == 3u, "dropped fixed time must not advance the simulation tick");
    Check(clock.GetAccumulatorSeconds() < slowFrame.stepSeconds,
          "fixed-step catch-up must leave less than one pending tick");

    HIKARI::FixedStepSettings sanitized{};
    sanitized.maxStepsPerFrame = 0u;
    clock.SetSettings(sanitized);
    Check(clock.GetSettings().maxStepsPerFrame == 1u, "fixed-step settings must retain a one-step minimum");
}

void TestCpuProfileRollingSystemTiming() {
    namespace PROFILE = HIKARI::CPU_PROFILE;
    namespace METRIC = HIKARI::METRIC;
    const PROFILE::DynamicPassHandle handle = PROFILE::RegisterDynamicPass("Contract/System/Fixed");
    Check(handle != PROFILE::kInvalidDynamicPassHandle, "CPU profiler must register a dynamic system pass");
    Check(PROFILE::RegisterDynamicPass("Contract/System/Fixed") == handle,
          "CPU profiler must deduplicate dynamic pass names");
    Check(PROFILE::RegisterDynamicPass({}) == PROFILE::kInvalidDynamicPassHandle,
          "CPU profiler must reject an empty dynamic pass name");

    PROFILE::BeginFrame(41u, 0.0);
    {
        PROFILE::ScopedDynamicCpuTimer first(handle);
    }
    {
        PROFILE::ScopedDynamicCpuTimer second(handle);
    }
    PROFILE::EndFrame();

    std::vector<METRIC::MetricReading> readings = METRIC::Snapshot(41u);
    const METRIC::MetricReading* calls = METRIC::FindReading(readings, "cpu.Contract/System/Fixed.calls");
    const METRIC::MetricReading* ms = METRIC::FindReading(readings, "cpu.Contract/System/Fixed.ms");
    Check(calls != nullptr && ms != nullptr && calls->status == METRIC::MetricStatus::Fresh &&
              calls->sourceFrame == 41u,
          "CPU profiler must publish a registered dynamic segment under its stable name");
    if (calls != nullptr && ms != nullptr) {
        Check(calls->value == 2.0 && ms->window.sampleCount == 1u,
              "dynamic system timing must aggregate calls and history");
    }

    PROFILE::BeginFrame(42u, 0.0);
    PROFILE::EndFrame();
    readings = METRIC::Snapshot(42u);
    calls = METRIC::FindReading(readings, "cpu.Contract/System/Fixed.calls");
    ms = METRIC::FindReading(readings, "cpu.Contract/System/Fixed.ms");
    if (calls != nullptr && ms != nullptr) {
        Check(calls->sourceFrame == 41u && ms->window.sampleCount == 1u,
              "an idle frame must neither publish the segment nor enter its sample window");
        Check(ms->window.average >= 0.0 && ms->window.maximum >= 0.0,
              "rolling CPU statistics must stay finite and non-negative");
    }

    // 名前域が所属モジュールを決める。地形の区間は Terrain（地形のシミュレーションの区間）で、Scene の費用になる。
    const PROFILE::DynamicPassHandle terrain = PROFILE::RegisterDynamicPass("Terrain/Contract");
    PROFILE::BeginFrame(43u, 0.0);
    {
        PROFILE::ScopedDynamicCpuTimer timer(terrain);
    }
    PROFILE::EndFrame();
    readings = METRIC::Snapshot(43u);
    const METRIC::MetricReading* terrainMs = METRIC::FindReading(readings, "cpu.Terrain/Contract.ms");
    Check(terrainMs != nullptr && terrainMs->descriptor->module == "Scene",
          "a Terrain/ CPU segment must be attributed to the Scene module");
    // 実行しなかった区間は公開されないので、両方を実行した 1 フレームで読む。
    PROFILE::BeginFrame(44u, 0.0);
    {
        PROFILE::ScopedCpuTimer timer(PROFILE::Pass::UiLayers);
    }
    {
        PROFILE::ScopedCpuTimer timer(PROFILE::Pass::GameUi);
    }
    PROFILE::EndFrame();
    readings = METRIC::Snapshot(44u);
    const METRIC::MetricReading* gameUi = METRIC::FindReading(readings, "cpu.Game UI.ms");
    const METRIC::MetricReading* uiLayers = METRIC::FindReading(readings, "cpu.UI Layers.ms");
    Check(gameUi != nullptr && gameUi->descriptor->module == "Render3D" && uiLayers != nullptr &&
              uiLayers->descriptor->module == "Render2D",
          "UI Layers belongs to Render2D and the game UI effect drawing to Render3D");
}

// 性能の分解の解析：基準と変種を交互に測った区間から、項目の費用・状態・未帰因・専用シェーダーの収益を求める。
void TestPerformanceBreakdownAnalysis() {
    using Json = nlohmann::json;
    namespace DIAG = HIKARI::DIAGNOSTICS;
    const auto step = [](const char* id, const char* role, const char* baseline) {
        Json entry = {{"id", id}, {"label", id}, {"role", role}};
        if (baseline != nullptr)
            entry["baseline"] = baseline;
        return entry;
    };
    Json unchanged = step("giOff", "system", "full");
    unchanged["status"] = "unchanged";
    Json report = {
        {"schemaVersion", DIAG::kBreakdownSchemaVersion},
        {"kind", "breakdown"},
        {"preset", "test"},
        {"targetPass", "Forward"},
        {"totalReference", "generalFull"},
        {"productionReference", "full"},
        {"complete", true},
        {"rounds", 2},
        {"sampleFrames", 60},
        {"steps", Json::array({step("full", "reference", nullptr), step("generalFull", "reference", nullptr),
                               step("albedoOnly", "floor", "generalFull"), step("skeleton", "base", "generalFull"),
                               step("normalMap", "part", "generalFull"), step("shadow", "part", "generalFull"),
                               step("slower", "part", "generalFull"), step("tiny", "part", "generalFull"),
                               step("missing", "part", "generalFull"), step("prepass", "system", "full"), unchanged})},
        {"segments", Json::array()}};
    size_t order = 0;
    const auto add = [&](const char* config, bool baseline, const char* status, double target, double frame) {
        Json segment = {
            {"index", order++}, {"config", config}, {"kind", baseline ? "baseline" : "variant"}, {"status", status}};
        if (std::string_view(status) == "ok") {
            segment["targetMs"] = target;
            segment["frameMs"] = frame;
        }
        report["segments"].push_back(segment);
    };
    // 1 回目と 2 回目で normalMap の差だけ変える（1.0 と 0.8 の平均 0.9）。基準は常に 10.0。
    for (const double normalMap : {9.0, 9.2}) {
        add("generalFull", true, "ok", 10.0, 14.0);
        add("albedoOnly", false, "ok", 0.2, 4.0);
        add("generalFull", true, "ok", 10.0, 14.0);
        add("skeleton", false, "ok", 3.0, 7.0);
        add("generalFull", true, "ok", 10.0, 14.0);
        add("normalMap", false, "ok", normalMap, 13.0);
        add("generalFull", true, "ok", 10.0, 14.0);
        add("shadow", false, "ok", 6.0, 10.0);
        add("generalFull", true, "ok", 10.0, 14.0);
        add("slower", false, "ok", 10.5, 14.5);
        add("generalFull", true, "ok", 10.0, 14.0);
        add("tiny", false, "ok", 10.0, 14.0);
        add("generalFull", true, "ok", 10.0, 14.0);
        add("missing", false, "notApplied", 0.0, 0.0);
        add("generalFull", true, "ok", 10.0, 14.0);
        add("full", true, "ok", 9.5, 13.5);
        add("prepass", false, "ok", 9.0, 14.0);
        add("full", true, "ok", 9.5, 13.5);
    }
    Check(DIAG::IsBreakdown(report) && DIAG::BreakdownSchemaError(report).empty(),
          "a breakdown must pass its schema check");
    Json wrongVersion = report;
    wrongVersion["schemaVersion"] = DIAG::kBreakdownSchemaVersion + 1;
    Check(!DIAG::BreakdownSchemaError(wrongVersion).empty(), "another breakdown schema version must be rejected");

    const Json analysis = DIAG::AnalyzeBreakdown(report);
    const auto find = [&analysis](const char* id) -> const Json* {
        for (const Json& entry : analysis["steps"]) {
            if (entry["id"] == id)
                return &entry;
        }
        return nullptr;
    };
    const Json* normalMap = find("normalMap");
    Check(normalMap != nullptr && (*normalMap)["status"] == "ok" && Near(float((*normalMap)["deltaMs"]), 0.9f) &&
              Near(float((*normalMap)["share"]), 0.09f) && Near(float((*normalMap)["frameDeltaMs"]), 1.0f),
          "a step's cost is the baseline minus the variant, averaged over the rounds");
    const Json* slower = find("slower");
    Check(slower != nullptr && (*slower)["status"] == "unreliable" && (*slower)["counted"] == false,
          "a variant slower than its baseline must be flagged unreliable and not counted");
    const Json* tiny = find("tiny");
    Check(tiny != nullptr && (*tiny)["status"] == "belowNoise", "a cost within the noise must be flagged");
    const Json* missing = find("missing");
    Check(missing != nullptr && (*missing)["status"] == "notApplied",
          "a setting that never took effect is not measured");
    const Json* skipped = find("giOff");
    Check(skipped != nullptr && (*skipped)["status"] == "unchanged",
          "a setting already in place is reported unchanged");
    const Json* prepass = find("prepass");
    Check(prepass != nullptr && Near(float((*prepass)["deltaMs"]), 0.5f) &&
              Near(float((*prepass)["frameDeltaMs"]), -0.5f),
          "a system step is compared with its own baseline and reports the whole-frame change");

    const Json& decomposition = analysis["decomposition"];
    Check(decomposition.is_object() && Near(float(decomposition["floorMs"]), 0.2f) &&
              Near(float(decomposition["partsMs"]), 4.9f) && Near(float(decomposition["baseShadingMs"]), 2.8f) &&
              Near(float(decomposition["jointMs"]), 7.0f) && Near(float(decomposition["differenceMs"]), -2.1f) &&
              Near(float(decomposition["uncoveredMs"]), 2.8f) && Near(float(decomposition["uncoveredShare"]), 0.28f),
          "total = floor + base shading + the features off together; the single-feature sum is compared with it");
    Check(analysis["normalOverGeneral"].is_object() && Near(float(analysis["normalOverGeneral"]["ms"]), -0.5f),
          "the normal pass is compared with the general baseline");

    const Json findings = DIAG::BuildBreakdownFindings(report);
    bool uncoveredWarned = false, missingWarned = false, shadowReported = false, overlapReported = false;
    for (const Json& finding : findings["findings"]) {
        const std::string id = finding["id"];
        overlapReported |= id == "breakdown.overlap";
        uncoveredWarned |= id == "breakdown.uncovered" && finding["severity"] == "warning";
        missingWarned |= id == "breakdown.notApplied.missing" && finding["severity"] == "warning";
        shadowReported |=
            id == "breakdown.step.shadow" && finding["title"].get<std::string>().find("4.00 ms") != std::string::npos;
    }
    Check(uncoveredWarned && missingWarned && shadowReported && overlapReported,
          "findings must name the expensive features, the overlap, the share outside every switch and the steps that "
          "did not apply");
    const std::vector<std::string> table = DIAG::FormatBreakdownTable(report);
    bool hasBaseShading = false;
    for (const std::string& line : table)
        hasBaseShading |= line.find("Base shading (no switch removes it)") != std::string::npos;
    Check(table.size() >= 12 && hasBaseShading, "the console table must list every step and the decomposition");

    // 前後の基準が段差（10 → 13 ms）を挟んだ測定は数えず、残りの回で費用を出す。
    Json drift = {
        {"schemaVersion", DIAG::kBreakdownSchemaVersion},
        {"kind", "breakdown"},
        {"targetPass", "Forward"},
        {"totalReference", "generalFull"},
        {"steps", Json::array({step("generalFull", "reference", nullptr), step("shadow", "part", "generalFull")})},
        {"segments", Json::array()}};
    const std::pair<const char*, double> driftSegments[] = {
        {"generalFull", 10.0}, {"shadow", 6.0}, {"generalFull", 10.0}, {"shadow", 6.0},
        {"generalFull", 13.0}, {"shadow", 9.0}, {"generalFull", 13.0}};
    for (const auto& [config, target] : driftSegments) {
        Json segment = {{"index", drift["segments"].size()},
                        {"config", config},
                        {"kind", std::string_view(config) == "generalFull" ? "baseline" : "variant"},
                        {"status", "ok"},
                        {"targetMs", target},
                        {"frameMs", target}};
        drift["segments"].push_back(std::move(segment));
    }
    const Json driftAnalysis = DIAG::AnalyzeBreakdown(drift);
    const Json& drifting = driftAnalysis["steps"][0];
    Check(drifting["status"] == "ok" && drifting["driftedRounds"] == 1 && Near(float(drifting["deltaMs"]), 4.0f),
          "a measurement whose baselines straddle a jump must be dropped and the other rounds still counted");
}

void TestCpuProfileResetDropsPartialFrame() {
    namespace PROFILE = HIKARI::CPU_PROFILE;
    namespace METRIC = HIKARI::METRIC;

    PROFILE::BeginFrame(50u, 0.0);
    {
        PROFILE::ScopedCpuTimer partial(PROFILE::Pass::AppUpdate);
        PROFILE::ResetStatistics();
    }
    PROFILE::EndFrame();
    std::vector<METRIC::MetricReading> readings = METRIC::Snapshot(50u);
    const METRIC::MetricReading* frame = METRIC::FindReading(readings, METRIC::kFrameCpuMs);
    Check(frame != nullptr && frame->status == METRIC::MetricStatus::NoData,
          "resetting CPU statistics during a frame must discard the partial frame");

    PROFILE::BeginFrame(51u, 0.0);
    {
        PROFILE::ScopedCpuTimer complete(PROFILE::Pass::AppUpdate);
    }
    PROFILE::EndFrame();
    readings = METRIC::Snapshot(51u);
    const METRIC::MetricReading* appUpdate = METRIC::FindReading(readings, "cpu.App Update.ms");
    Check(appUpdate != nullptr && appUpdate->status == METRIC::MetricStatus::Fresh && appUpdate->sourceFrame == 51u,
          "CPU statistics must resume with the first complete post-reset frame");
    Check(appUpdate != nullptr && appUpdate->window.sampleCount == 1u,
          "post-reset CPU rolling history must exclude the discarded frame");
}

void TestShadowCacheLastMissSurvivesHitAndInvalidation() {
    using namespace HIKARI::SHADOW;
    ShadowCachePolicy policy;
    ShadowCacheReuseInput input{};
    input.key.resolution = 1024u;
    input.key.cascadeCount = 1u;
    input.key.layoutVersion = 2u;
    input.key.sourceVersion = 3u;
    input.key.sourceInstanceCount = 4u;
    input.hasStaticWork = false;
    input.resourcesReady = true;
    input.stateReusable = true;

    Check(!policy.EvaluateReuse(input), "shadow cache must miss when the static cache is invalid");
    policy.MarkMiss(1234u);
    ShadowMapDebugStats stats{};
    policy.PublishStats(stats);
    Check((stats.shadowCacheLastMissReasonFlags & ShadowCacheMissReasonNoStaticWork) != 0u &&
              stats.shadowCacheLastMissFrameIndex == 1234u,
          "shadow cache must publish its last miss reason and logical frame");

    policy.MarkValid(input.key);
    input.hasStaticWork = true;
    Check(policy.EvaluateReuse(input), "a valid unchanged shadow cache must be reusable");
    policy.MarkHit();
    policy.PublishStats(stats);
    Check(stats.shadowCacheHit && stats.shadowCacheMissReasonFlags == ShadowCacheMissReasonNone &&
              (stats.shadowCacheLastMissReasonFlags & ShadowCacheMissReasonNoStaticWork) != 0u &&
              stats.shadowCacheLastMissFrameIndex == 1234u,
          "shadow cache hits must preserve the last miss diagnostics");

    policy.Invalidate();
    policy.PublishStats(stats);
    Check(stats.shadowCacheLastMissReasonFlags != ShadowCacheMissReasonNone &&
              stats.shadowCacheLastMissFrameIndex == 1234u,
          "ordinary shadow cache invalidation must preserve failure diagnostics");

    policy = {};
    policy.PublishStats(stats);
    Check(stats.shadowCacheLastMissReasonFlags == ShadowCacheMissReasonNone &&
              stats.shadowCacheLastMissFrameIndex == 0u,
          "resetting shadow cache state for a new scene must clear stale diagnostics");
}

void TestActionTimeDilation() {
    using HIKARI::SERVICES::ActionTimeDilationService;

    HIKARI::TIME::Reset();
    ActionTimeDilationService service;
    auto* ownerA = reinterpret_cast<HIKARI::World*>(static_cast<std::uintptr_t>(0x101u));
    auto* ownerB = reinterpret_cast<HIKARI::World*>(static_cast<std::uintptr_t>(0x202u));

    HIKARI::TIME::SetGameTimeScale(0.35f);
    service.ApplyPending();
    Check(Near(service.GetEffectiveScale(), 0.35f) && Near(HIKARI::TIME::GetGameTimeScale(), 0.35f),
          "empty action arbitration must preserve an existing base scale");

    const uint64_t low = service.Request(ownerA, ActionTimeDilationService::RequestDesc{20u, 50, 0.5f, 0.0f});
    const uint64_t high = service.Request(ownerA, ActionTimeDilationService::RequestDesc{30u, 100, 0.25f, 0.2f});
    Check(low != 0u && high != 0u && low != high, "time requests must allocate distinct non-zero tokens");

    service.ApplyPending();
    Check(Near(service.GetEffectiveScale(), 0.25f) && Near(HIKARI::TIME::GetGameTimeScale(), 0.25f),
          "highest-priority time request must win at the frame boundary");

    const uint64_t upserted = service.Request(ownerA, ActionTimeDilationService::RequestDesc{30u, 100, 0.75f, 0.2f});
    Check(upserted == high, "same owner/source must upsert while retaining its release token");
    service.ApplyPending();
    Check(Near(service.GetEffectiveScale(), 0.75f), "upserted request must replace its scale");

    const uint64_t tieHighSource =
        service.Request(ownerB, ActionTimeDilationService::RequestDesc{99u, 200, 0.6f, 0.0f});
    const uint64_t tieLowSource = service.Request(ownerB, ActionTimeDilationService::RequestDesc{77u, 200, 0.4f, 0.0f});
    Check(tieHighSource != 0u && tieLowSource != 0u, "same-priority requests must remain addressable");
    service.ApplyPending();
    Check(Near(service.GetEffectiveScale(), 0.4f), "same-priority time requests must tie-break by source id");

    Check(service.Release(ownerB, tieHighSource), "owner-scoped release must remove an owned token");
    Check(!service.Release(ownerA, tieHighSource), "owner-scoped release must reject another owner");
    service.ReleaseOwner(ownerB);
    service.ApplyPending();
    Check(Near(service.GetEffectiveScale(), 0.75f), "releasing an owner must reveal the next winning request");

    service.Advance(0.21f);
    Check(service.HasRequests(), "an indefinite request must survive unscaled advancement");
    service.ApplyPending();
    Check(Near(service.GetEffectiveScale(), 0.5f), "expired finite request must fall back to the indefinite request");

    Check(service.Release(low), "token release must remove an indefinite request");
    service.ApplyPending();
    Check(Near(service.GetEffectiveScale(), 0.35f) && Near(HIKARI::TIME::GetGameTimeScale(), 0.35f),
          "empty time arbitration must restore the prior base scale");

    const uint64_t stale = service.Request(ownerA, ActionTimeDilationService::RequestDesc{1u, 1, 0.1f, 0.0f});
    Check(stale != 0u, "reset test request must be accepted");
    service.Reset();
    Check(!service.HasRequests() && Near(service.GetEffectiveScale(), 0.35f) && !service.Release(stale),
          "reset must clear requests and invalidate old tokens");
    HIKARI::TIME::Reset();
}

void TestAnimationRootMotion() {
    HIKARI::ANIMATION::AnimationRootMotionService service;
    const HIKARI::RuntimeObjectHandle object = Object(7u);
    HIKARI::ANIMATION::AnimationRootMotionDelta out{};

    Check(!service.Publish({}, 1u, RootDelta(1.0f, 0.0f)), "root motion must reject invalid objects");
    Check(service.Publish(object, 10u, RootDelta(1.0f, 0.0f)), "root motion must publish a valid fixed-tick delta");
    Check(service.Publish(object, 10u, RootDelta(0.0f, 2.0f)), "same tick root motion must accumulate");
    Check(service.GetRevision(object) == 2u, "each root motion publication must advance its revision");
    Check(service.Peek(object, 10u, out) && Near(out.translation.x, 1.0f) && Near(out.translation.z, 2.0f),
          "peek must expose accumulated translation without consuming it");
    Check(service.Consume(object, 10u, out) && Near(out.translation.x, 1.0f) && Near(out.translation.z, 2.0f) &&
              out.fixedTickIndex == 10u && out.revision == 2u,
          "consume must return accumulated delivery metadata");
    Check(!service.Consume(object, 10u, out), "root motion must be consume-once per object and fixed tick");
    Check(!service.Publish(object, 9u, RootDelta(1.0f, 0.0f)), "root motion must reject publications for stale ticks");
    Check(!service.Consume(object, 9u, out), "root motion must reject stale consumption ticks");
    Check(service.Publish(object, 11u, RootDelta(3.0f, 0.0f)),
          "a later fixed tick must accept a new root motion delivery");
    Check(!service.Consume(object, 12u, out), "consuming a missing future tick must not fabricate root motion");
    Check(!service.Publish(object, 12u, RootDelta(1.0f, 0.0f)), "a tick passed by the fixed consumer must stay closed");

    const HIKARI::RuntimeObjectHandle turningObject = Object(8u);
    HIKARI::ANIMATION::AnimationRootMotionDelta turn = RootDelta(0.0f, 0.0f);
    turn.rotation = {0.0f, 1.0f, 0.0f, 0.0f};
    Check(service.Publish(turningObject, 20u, turn) && service.Publish(turningObject, 20u, RootDelta(1.0f, 0.0f)) &&
              service.Consume(turningObject, 20u, out) && Near(out.translation.x, -1.0f),
          "same-tick root deltas must compose translation after rotation");

    service.Remove(object);
    Check(service.GetRevision(object) == 0u, "removing an object must clear root motion state");
}

void TestAnimationRootMotionBindSpace() {
    using namespace HIKARI;

    ModelAsset model{};
    model.nodes.resize(1u);
    model.nodes[0].name = "root";
    model.nodes[0].localTransform.rotation = {-0.70710678f, 0.0f, 0.0f, 0.70710678f};
    SkeletonAsset skin{};
    skin.joints.push_back({"root", 0, -1, MATH::Mat4::Identity()});
    model.skins.push_back(skin);

    ANIMATION::AnimationLocalPose previousPose{};
    previousPose.nodes.push_back(model.nodes[0].localTransform);
    ANIMATION::AnimationLocalPose currentPose = previousPose;
    currentPose.nodes[0].position = {0.0f, 0.0f, 1.0f};

    ANIMATION::AnimationLocalPose reused;
    reused.nodes.resize(1);
    reused.nodes[0].useExplicitMatrix = true;
    reused.nodes[0].explicitMatrix = MATH::Mat4::Identity();
    Check(ANIMATION::BlendAnimationPoses(previousPose, currentPose, 0.5f, reused) &&
              !reused.nodes[0].useExplicitMatrix && Near(reused.nodes[0].position.z, 0.5f),
          "reused blend scratch must clear explicit matrix state");
    Check(ANIMATION::BlendAnimationPoses(reused, currentPose, 0.5f, reused) && Near(reused.nodes[0].position.z, 0.75f),
          "blend output may alias its input");

    ANIMATION::AnimationPoseSnapshot previous{};
    ANIMATION::AnimationPoseSnapshot current{};
    previous.primaryClip.fallbackName = "Dodge";
    current.primaryClip.fallbackName = "Dodge";
    previous.normalizedTime = 0.1f;
    current.normalizedTime = 0.2f;
    previous.loop = false;
    current.loop = false;
    previous.valid = true;
    current.valid = true;
    Check(ANIMATION::POSE_EVALUATION::CaptureRootMotionSample(model, previousPose, previous.rootMotionSample) &&
              ANIMATION::POSE_EVALUATION::CaptureRootMotionSample(model, currentPose, current.rootMotionSample),
          "root motion samples must normalize an imported bind transform");

    ANIMATION::AnimationRootMotionDelta delta{};
    Check(ANIMATION::EvaluateAnimationRootMotion(model, &previous, current, delta) && Near(delta.translation.x, 0.0f) &&
              Near(delta.translation.y, 0.0f) && Near(delta.translation.z, 1.0f),
          "bind-axis conversion must not turn forward root motion vertical");
}

void TestAnimationMaskedPoseSubtree() {
    using namespace HIKARI;

    ModelAsset model{};
    model.nodes.resize(5u);
    model.nodes[0].name = "root";
    model.nodes[0].parent = -1;
    model.nodes[1].name = "pelvis";
    model.nodes[1].parent = 0;
    model.nodes[2].name = "spine_01";
    model.nodes[2].parent = 1;
    model.nodes[3].name = "hand_r";
    model.nodes[3].parent = 2;
    model.nodes[4].name = "thigh_l";
    model.nodes[4].parent = 1;
    SkeletonAsset skin{};
    skin.skeletonRootNode = 0;
    model.skins.push_back(skin);

    ANIMATION::AnimationLocalPose base{};
    ANIMATION::AnimationLocalPose overlay{};
    base.nodes.resize(model.nodes.size());
    overlay.nodes.resize(model.nodes.size());
    for (size_t index = 0u; index < model.nodes.size(); ++index) {
        const float value = static_cast<float>(index);
        base.nodes[index].position = {value, 0.0f, 0.0f};
        base.nodes[index].rotation = MATH::Quat::Identity();
        base.nodes[index].scale = {1.0f, 1.0f, 1.0f};
        overlay.nodes[index] = base.nodes[index];
        overlay.nodes[index].position.x += 10.0f;
    }

    ANIMATION::AnimationLocalPose blended{};
    Check(ANIMATION::BlendAnimationPoseSubtree(model, base, overlay, "spine_01", 0.5f, blended),
          "masked animation must blend a valid model subtree");
    Check(blended.nodes.size() == model.nodes.size() && Near(blended.nodes[0].position.x, 0.0f) &&
              Near(blended.nodes[1].position.x, 1.0f) && Near(blended.nodes[2].position.x, 7.0f) &&
              Near(blended.nodes[3].position.x, 8.0f) && Near(blended.nodes[4].position.x, 4.0f),
          "masked animation must preserve parents and sibling limbs");

    ANIMATION::AnimationPoseSubtreeCache cache{};
    Check(ANIMATION::PrepareAnimationPoseSubtreeCache(model, "spine_01", {}, cache) && cache.valid &&
              cache.rootPath.size() == 3u && cache.subtreeNodes.size() == 2u,
          "masked animation must cache the root path and subtree topology");
    ANIMATION::AnimationLocalPose cachedBlend{};
    Check(ANIMATION::BlendAnimationPoseSubtreeCached(model, base, overlay, cache, 0.5f, cachedBlend) &&
              Near(cachedBlend.nodes[2].position.x, 7.0f) && Near(cachedBlend.nodes[3].position.x, 8.0f),
          "cached masked animation must match the transactional blend");
    ANIMATION::AnimationLocalPose inPlace = base;
    const size_t inPlaceCapacity = inPlace.nodes.capacity();
    Check(ANIMATION::ApplyAnimationPoseSubtreeInPlace(model, inPlace, overlay, cache, 0.5f) &&
              inPlace.nodes.capacity() == inPlaceCapacity && Near(inPlace.nodes[2].position.x, 7.0f) &&
              Near(inPlace.nodes[3].position.x, 8.0f),
          "cached masked animation must apply without reallocating the base pose");
    ANIMATION::AnimationLocalPose staleRevisionPose = base;
    ++model.sourceRevision;
    Check(!ANIMATION::ApplyAnimationPoseSubtreeInPlace(model, staleRevisionPose, overlay, cache, 0.5f) &&
              Near(staleRevisionPose.nodes[2].position.x, 2.0f),
          "masked animation caches must reject an in-place model reload generation");
    Check(ANIMATION::PrepareAnimationPoseSubtreeCache(model, "spine_01", {}, cache) &&
              cache.modelSourceRevision == model.sourceRevision,
          "masked animation caches must rebuild against the current model generation");
    ANIMATION::AnimationPoseSubtreeCache missingCache{};
    Check(!ANIMATION::PrepareAnimationPoseSubtreeCache(model, "missing", {}, missingCache) && !missingCache.valid,
          "cached masked animation must reject a missing root without a stale cache");

    Check(ANIMATION::BlendAnimationPoseSubtree(model, base, overlay, "root", 1.0f, blended) &&
              Near(blended.nodes[0].position.x, 0.0f) && Near(blended.nodes[1].position.x, 11.0f),
          "masked animation must never overwrite the root-motion node");

    overlay.nodes[1].rotation = MATH::Quat::FromEulerXYZ(0.0f, 1.57079632679f, 0.0f);
    overlay.nodes[2].rotation = MATH::Quat::Identity();
    Check(ANIMATION::BlendAnimationPoseSubtree(model, base, overlay, "spine_01", 1.0f, blended,
                                               ANIMATION::AnimationSubtreeRootAlignment::PreserveModelRotation),
          "model-aligned masked animation must accept a valid subtree");
    const std::vector<MATH::Mat4> alignedGlobals = ANIMATION::BuildAnimationNodeGlobalMatrices(model, &blended);
    Check(alignedGlobals.size() == model.nodes.size() && Near(alignedGlobals[2].m[2][0], 1.0f, 1.0e-4f) &&
              Near(alignedGlobals[2].m[2][2], 0.0f, 1.0e-4f) && Near(blended.nodes[1].rotation.w, 1.0f),
          "model-aligned subtree root must preserve the overlay facing without overwriting its base pelvis");

    ANIMATION::AnimationLocalPose untouched{};
    untouched.nodes.resize(1u);
    untouched.nodes[0].position.x = 99.0f;
    Check(!ANIMATION::BlendAnimationPoseSubtree(model, base, overlay, "missing", 1.0f, untouched) &&
              untouched.nodes.size() == 1u && Near(untouched.nodes[0].position.x, 99.0f),
          "missing animation masks must fail without modifying output");

    overlay.nodes[2].position.x = (std::numeric_limits<float>::quiet_NaN)();
    Check(!ANIMATION::BlendAnimationPoseSubtree(model, base, overlay, "spine_01", 1.0f, untouched) &&
              untouched.nodes.size() == 1u && Near(untouched.nodes[0].position.x, 99.0f),
          "non-finite masked poses must fail transactionally");
}

// 片段の無い Animator はバインド姿勢に節点の偏移を重ねる。重ねた数で姿勢を有効にするので、数え方を固定する。
void TestAnimationNodeOverrideOnBindPose() {
    using namespace HIKARI;

    ModelAsset model{};
    model.nodes.resize(2u);
    model.nodes[0].name = "root";
    model.nodes[0].parent = -1;
    model.nodes[1].name = "head";
    model.nodes[1].parent = 0;
    model.nodes[1].localTransform.position = {0.0f, 1.0f, 0.0f};

    ANIMATION::AnimationLocalPose pose{};
    ANIMATION::BuildBindPose(model, pose);
    const RuntimeObjectHandle object = RuntimeObjectHandle::FromValue((uint64_t{1u} << 32u) | 1u);
    ANIMATION::AnimationNodeOverrideService overrides;
    Check(overrides.Apply(model, object, pose) == 0u && !pose.nodes[1].useExplicitMatrix,
          "an object without node offsets must leave the bind pose untouched");

    Transform3D offset{};
    offset.position = {0.5f, 0.0f, 0.0f};
    Check(overrides.SetLocalOffset(object, "head", offset) && overrides.SetLocalOffset(object, "missing", offset),
          "node offsets must be accepted before the model is known");
    Check(overrides.Apply(model, object, pose) == 1u, "only offsets whose node resolves may count as applied");
    const MATH::Vec3 head = MATH::TransformPoint(pose.nodes[1].explicitMatrix, MATH::Vec3{0.0f, 0.0f, 0.0f});
    Check(pose.nodes[1].useExplicitMatrix && Near(head.x, 0.5f) && Near(head.y, 1.0f) && Near(head.z, 0.0f),
          "a node offset must compose onto the bind pose");
}

void TestAnimatorMaskedPoseLifecycle() {
    using HIKARI::AnimatorComponent;

    AnimatorComponent animator;
    animator.SetMaskedPoseLayer("Guard", "spine_01", 0.25f, 1.0f, 1.0f);
    const AnimatorComponent::MaskedPoseLayerState& initial = animator.GetMaskedPoseLayer();
    Check(initial.active && Near(initial.currentWeight, 0.0f) && Near(initial.targetWeight, 1.0f),
          "masked layer must start active at zero weight when fading in");

    animator.Pause();
    animator.Advance(0.25f);
    Check(Near(animator.GetMaskedPoseLayer().currentWeight, 0.25f),
          "masked layer weight must advance while the base animator is paused");

    animator.SetMaskedPoseLayer("Guard", "spine_01", 0.50f, 1.0f, 0.0f);
    animator.Advance(0.25f);
    Check(Near(animator.GetMaskedPoseLayer().currentWeight, 0.50f),
          "repeating the same masked layer with zero blend must preserve its fade progress");

    animator.ClearMaskedPoseLayer(1.0f);
    animator.Advance(0.25f);
    const float afterFirstClearStep = animator.GetMaskedPoseLayer().currentWeight;
    Check(Near(afterFirstClearStep, 0.375f), "masked layer must fade out from its current weight");

    animator.ClearMaskedPoseLayer(1.0f);
    animator.Advance(0.25f);
    Check(Near(animator.GetMaskedPoseLayer().currentWeight, 0.25f),
          "repeating Clear must not restart the masked layer fade-out");

    animator.Advance(1.0f);
    Check(!animator.GetMaskedPoseLayer().active && animator.GetMaskedPoseLayer().clip.IsEmpty() &&
              animator.GetMaskedPoseLayer().maskRootNodeName.empty() &&
              Near(animator.GetMaskedPoseLayer().currentWeight, 0.0f),
          "a completed masked fade-out must clear its state");

    animator.ClearMaskedPoseLayer(1.0f);
    Check(!animator.GetMaskedPoseLayer().active, "clearing an inactive masked layer must be a safe no-op");

    animator.SetMaskedPoseLayer("", "spine_01", 0.0f, 1.0f, 0.0f);
    Check(!animator.GetMaskedPoseLayer().active, "an empty masked clip name must leave the layer inactive");
    animator.SetMaskedPoseLayer("Guard", "", 0.0f, 1.0f, 0.0f);
    Check(!animator.GetMaskedPoseLayer().active, "an empty mask root name must leave the layer inactive");

    animator.SetMaskedPoseLayer("Guard", "spine_01", 0.0f, 1.0f, 0.0f);
    Check(animator.GetMaskedPoseLayer().active && Near(animator.GetMaskedPoseLayer().currentWeight, 1.0f),
          "an instantaneous masked fade-in must become fully weighted");
    animator.SetMaskedPoseLayer("BowAttack", "spine_01", 0.5f, 1.0f, 0.0f,
                                HIKARI::ANIMATION::AnimationSubtreeRootAlignment::PreserveModelRotation);
    Check(animator.GetMaskedPoseLayer().rootAlignment ==
              HIKARI::ANIMATION::AnimationSubtreeRootAlignment::PreserveModelRotation,
          "masked pose lifecycle must retain the requested root alignment");
    animator.ClearMaskedPoseLayer((std::numeric_limits<float>::quiet_NaN)());
    Check(!animator.GetMaskedPoseLayer().active && animator.GetMaskedPoseLayer().clip.IsEmpty(),
          "an invalid Clear duration must safely remove the masked layer");

    animator.SetMaskedPoseLayer("Guard", "spine_01", 0.0f, 1.0f, 0.0f);
    animator.ClearMaskedPoseLayer(0.0f);
    Check(!animator.GetMaskedPoseLayer().active, "an instantaneous Clear must safely remove the masked layer");
}

void TestKinematicMotionDelta() {
    HIKARI::PHYSICS::KinematicMotionService service;
    const HIKARI::RuntimeObjectHandle object = Object(11u);
    HIKARI::PHYSICS::KinematicMotionDelta out{};
    HIKARI::PHYSICS::KinematicMotionSourceId source = 0u;

    Check(!service.SubmitMotionDelta(object, 0u, 200, 4u, {1.0f, 0.0f, 0.0f}, HIKARI::MATH::Quat::Identity()),
          "kinematic motion deltas must reject source zero");
    Check(service.SubmitMotionDelta(object, 20u, 200, 4u, {2.0f, 0.0f, 0.0f}, HIKARI::MATH::Quat::Identity()),
          "kinematic motion delta must accept a valid submission");
    Check(service.SubmitMotionDelta(object, 10u, 200, 4u, {1.0f, 0.0f, 0.0f}, HIKARI::MATH::Quat::Identity()),
          "kinematic motion delta must accept a source tie");
    Check(service.SubmitMotionDelta(object, 30u, 100, 4u, {9.0f, 0.0f, 0.0f}, HIKARI::MATH::Quat::Identity()),
          "lower-priority motion delta must remain available for arbitration");

    Check(service.ConsumeMotionDelta(object, 4u, out, &source) && source == 10u && Near(out.translation.x, 1.0f),
          "kinematic motion delta must select highest priority then lowest source");
    Check(!service.ConsumeMotionDelta(object, 4u, out, &source),
          "kinematic motion delta must be consume-once per fixed tick");
    Check(!service.SubmitMotionDelta(object, 40u, 300, 4u, {10.0f, 0.0f, 0.0f}, HIKARI::MATH::Quat::Identity()),
          "late motion delta submissions must not reopen a consumed tick");
    Check(service.SubmitMotionDelta(object, 40u, 300, 5u, {4.0f, 0.0f, 0.0f}, HIKARI::MATH::Quat::Identity()),
          "a later fixed tick must accept a motion delta");
    Check(service.ConsumeMotionDelta(object, 5u, out, &source) && source == 40u && Near(out.translation.x, 4.0f),
          "later fixed tick delta must be delivered to the solver");
    Check(!service.SubmitMotionDelta(object, 41u, 300, 4u, {1.0f, 0.0f, 0.0f}, HIKARI::MATH::Quat::Identity()),
          "stale fixed tick submissions must be rejected");
    service.Remove(object);
    Check(!service.ConsumeMotionDelta(object, 6u, out, &source),
          "removed kinematic actors must have no pending motion delta");
}

void TestKinematicMoveCollisionOptions() {
    HIKARI::PHYSICS::KinematicMotionService service;
    const HIKARI::RuntimeObjectHandle actor = Object(12u);
    const HIKARI::RuntimeObjectHandle ignoredTarget = Object(13u);

    HIKARI::PHYSICS::KinematicMotionRequest selected{};
    selected.horizontalVelocity = {20.0f, 0.0f, 0.0f};
    selected.ignoredCollisionObject = ignoredTarget;
    selected.overrideVerticalGravity = true;
    selected.verticalGravityAcceleration = -12.f;
    service.SubmitMove(actor, 20u, 220, selected, 8u);

    HIKARI::PHYSICS::KinematicMotionRequest lowerPriority{};
    lowerPriority.horizontalVelocity = {1.0f, 0.0f, 0.0f};
    service.SubmitMove(actor, 10u, 100, lowerPriority, 8u);

    HIKARI::PHYSICS::KinematicMotionRequest out{};
    Check(service.ConsumeMove(actor, 8u, out) && out.ignoredCollisionObject == ignoredTarget &&
              Near(out.horizontalVelocity.x, 20.0f) && out.overrideVerticalGravity &&
              Near(out.verticalGravityAcceleration, -12.f),
          "kinematic move arbitration preserves the selected collision and gravity options");
    service.SubmitMove(actor, 10u, 220, lowerPriority, 8u);
    Check(service.ConsumeMove(actor, 8u, out) && !out.overrideVerticalGravity && !out.ignoredCollisionObject.IsValid(),
          "equal-priority lower source wins with its own disabled gravity override");
    Check(!service.ConsumeMove(actor, 9u, out) && !out.ignoredCollisionObject.IsValid() && !out.overrideVerticalGravity,
          "collision and gravity options must not leak into a later fixed tick");
}

void TestCameraRoll() {
    using namespace HIKARI;
    CAMERA::CameraRigService service;
    Camera3D camera;
    camera.SetLookAt({0.0f, 0.0f, -5.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    service.BeginFrame(1u);
    const float initialFov = camera.GetFovYRad();
    CAMERA::CameraModifierSubmission offset{};
    offset.sourceId = 77u;
    offset.priority = 100;
    offset.verticalFovOffsetRadians = 0.1f;
    Check(service.SubmitModifier(offset), "camera offset modifier must be accepted");
    CAMERA::CameraModifierSubmission roll{};
    roll.sourceId = 77u;
    roll.channelId = 1u;
    roll.priority = 100;
    roll.localRollRadians = 1.57079632679f;
    Check(service.SubmitModifier(roll), "camera roll modifier must be accepted");
    CAMERA::CameraTargetFramingSubmission framing{};
    framing.targetObject = {3u, 1u};
    framing.sourceId = 90u;
    framing.priority = 120;
    framing.targetWeight = 0.46f;
    framing.dynamics.targetSwitchSmoothSeconds = 0.33f;
    Check(service.SubmitTargetFraming(framing), "camera target framing must be accepted");
    CAMERA::CameraTargetFramingSubmission invalidDynamics = framing;
    invalidDynamics.dynamics.enterSmoothSeconds = -0.01f;
    Check(!service.SubmitTargetFraming(invalidDynamics), "camera target framing must reject negative response time");
    invalidDynamics = framing;
    invalidDynamics.dynamics.yawDeadZoneDegrees = 181.0f;
    Check(!service.SubmitTargetFraming(invalidDynamics), "camera target framing must reject an invalid yaw dead zone");
    CAMERA::CameraTargetFramingSubmission higherPriority = framing;
    higherPriority.sourceId = 91u;
    higherPriority.priority = 140;
    higherPriority.targetWeight = 0.60f;
    Check(service.SubmitTargetFraming(higherPriority), "a competing camera target framing must be accepted");
    CAMERA::CameraTargetFramingSubmission resolvedFraming{};
    Check(service.TryResolveTargetFraming(SceneObjectId{1u}, resolvedFraming) && resolvedFraming.sourceId == 91u &&
              Near(resolvedFraming.targetWeight, 0.60f) &&
              Near(resolvedFraming.dynamics.targetSwitchSmoothSeconds, 0.33f),
          "camera target framing must resolve by priority");
    service.ApplyModifiers(SceneObjectId{1u}, false, camera);
    const MATH::Vec3 up = camera.GetUp();
    Check(std::abs(up.x) > 0.99f && std::abs(up.y) < 0.01f && std::abs(up.z) < 0.01f,
          "camera local roll must rotate the final up basis");
    Check(Near(camera.GetFovYRad(), initialFov + 0.1f), "same-source camera channels must retain the FOV offset");
    Camera3D blendedCamera;
    blendedCamera.SetLookAt({0.0f, 0.0f, -5.0f}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f});
    const float blendedInitialFov = blendedCamera.GetFovYRad();
    service.ApplyModifiers(SceneObjectId{1u}, false, blendedCamera, 0.25f);
    const MATH::Vec3 blendedUp = blendedCamera.GetUp();
    Check(Near(blendedCamera.GetFovYRad(), blendedInitialFov + 0.025f) && Near(blendedUp.x, -0.38268343f, 0.0001f),
          "camera modifier handoff must scale FOV and roll continuously");
    Check(service.GetStatus(SceneObjectId{1u}).modifierCount == 2u,
          "camera status must report both same-source channels");
    service.BeginFrame(2u);
    Check(service.GetStatus(SceneObjectId{1u}).modifierCount == 0u, "camera modifiers must be scoped to one frame");
    Check(!service.TryResolveTargetFraming(SceneObjectId{1u}, resolvedFraming),
          "camera target framing must be scoped to one frame");
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--afterimage") {
        CheckAfterimageContracts();
        std::cout << "Afterimage contracts passed.\n";
        return EXIT_SUCCESS;
    }
    CheckAfterimageContracts();
    CheckAudioPlaybackContracts();
    TestFixedStepCatchUpBudget();
    TestCpuProfileRollingSystemTiming();
    TestCpuProfileResetDropsPartialFrame();
    TestPerformanceBreakdownAnalysis();
    TestShadowCacheLastMissSurvivesHitAndInvalidation();
    TestActionTimeDilation();
    TestAnimationRootMotion();
    TestAnimationRootMotionBindSpace();
    TestAnimationMaskedPoseSubtree();
    TestAnimationNodeOverrideOnBindPose();
    TestAnimatorMaskedPoseLifecycle();
    TestKinematicMotionDelta();
    TestKinematicMoveCollisionOptions();
    TestCameraRoll();
    if (gFailures != 0) {
        std::cerr << gFailures << " action runtime contract checks failed.\n";
        return EXIT_FAILURE;
    }
    std::cout << "Action runtime contracts passed.\n";
    return EXIT_SUCCESS;
}
