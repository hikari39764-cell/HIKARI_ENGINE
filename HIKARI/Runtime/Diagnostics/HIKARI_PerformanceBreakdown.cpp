#include "Runtime/Diagnostics/HIKARI_PerformanceBreakdown.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <memory>
#include <span>
#include <utility>

#include "Core/HIKARI_Logger.h"
#include "Core/Serialization/Json/HIKARI_JsonFieldPath.h"
#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Text/HIKARI_EnumNames.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Diagnostics/HIKARI_MetricRegistry.h"
#include "Diagnostics/HIKARI_PerformanceBreakdownAnalysis.h"
#include "Render3D/Settings/HIKARI_RenderQualityFields.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettingsJson.h"
#include "Runtime/HIKARI_RuntimeLaunchConfig.h"
#include "Runtime/Presentation/HIKARI_FramePresentationService.h"
#include "Scene/HIKARI_IScene.h"

namespace HIKARI::RUNTIME::PERFORMANCE_BREAKDOWN {
namespace CAPTURE = PERFORMANCE_CAPTURE;
using Json = nlohmann::json;
using RENDER3D::ForwardShadingCostMode;

namespace {

// ---- 表（preset）：基準と、1 項目を省いた変種 ----
// 変種 1 つ = 画質の項目の上書き（項目の表 kRenderQualitySettingsFields の名前 = JSON の値）と、比べる基準。
// 基準は何も省かない描画で、変種の上書きはその基準の上書きの上に重ねる。
struct Override {
    const char* field = nullptr;
    // JSON として読めればその型（true、数）、読めなければ文字列（列挙の名前）。
    const char* value = nullptr;
};

enum class Role : uint8_t {
    // 何も省かない描画。区間は基準として交互に挟む。
    Reference,
    // 底色だけを描く下限。耗時そのものを数える。
    Floor,
    // 切り替えられる項目を全部省いた描画（基礎の着色）。耗時そのものを数える。
    Base,
    // 完全な描画から差し引く項目。未帰因を求める時の和に入る。
    Part,
    // 他の項目と重なる参考値。和に入れない。
    Info,
    // 描画の仕組み全体を切り替える項目（別の基準と比べる）。和に入れない。
    System,
};

constexpr TEXT::EnumName<Role> kRoleNames[] = {
    {Role::Reference, "reference"}, {Role::Floor, "floor"}, {Role::Base, "base"},
    {Role::Part, "part"},           {Role::Info, "info"},   {Role::System, "system"},
};

struct Step {
    const char* id;
    const char* label;
    Role role;
    // 比べる基準の id（基準自身は無し）。
    const char* baseline;
    Override overrides[2];
};

struct Preset {
    const char* name;
    // 耗時を読む GPU pass の名前。
    const char* targetPass;
    // 完全な描画（底色と各項目の和を引く元）にする基準と、通常の描画の基準（汎用シェーダーとの差を出す）。
    const char* totalReference;
    const char* productionReference;
    std::span<const Step> steps;
};

constexpr Override CostMode(ForwardShadingCostMode mode) {
    return {"forwardCostMode", TEXT::EnumToName(RENDER3D::kForwardShadingCostModeNames, mode)};
}

// 前向きの診断モードを 1 つ切り替える項目。比べる基準は何も省かない GeneralFull。
constexpr Step Mode(const char* id, const char* label, Role role, ForwardShadingCostMode mode) {
    return {id, label, role, "generalFull", {CostMode(mode)}};
}

// 前向き不透明（Meshlet Draw Forward）の分解。完全な描画 = 底色だけ + 基礎の着色 + 項目（Part）の和 + 未帰因。
constexpr Step kForwardSteps[] = {
    {"full", "Full (normal rendering)", Role::Reference, nullptr, {CostMode(ForwardShadingCostMode::Full)}},
    {"generalFull",
     "General full (one shader for every surface, no SSR capture)",
     Role::Reference,
     nullptr,
     {CostMode(ForwardShadingCostMode::GeneralFull)}},
    Mode("albedoOnly", "Base colour only (geometry, raster, albedo)", Role::Floor, ForwardShadingCostMode::AlbedoOnly),
    Mode("skeleton", "Base shading: every optional feature off", Role::Base, ForwardShadingCostMode::Skeleton),
    Mode("normalMap", "Normal map", Role::Part, ForwardShadingCostMode::NoNormalMap),
    Mode("materialExtras", "Material extras (metal, rough, AO, emissive, FX)", Role::Part,
         ForwardShadingCostMode::NoMaterialExtras),
    Mode("decals", "Decals", Role::Part, ForwardShadingCostMode::NoDecals),
    Mode("ssao", "SSAO read", Role::Part, ForwardShadingCostMode::NoSsao),
    Mode("cloudShadow", "Cloud shadow", Role::Part, ForwardShadingCostMode::NoCloudShadow),
    Mode("sunShadow", "Sun shadow (cascades + contact)", Role::Part, ForwardShadingCostMode::NoSunShadow),
    Mode("localLights", "Local lights (lighting + their shadows)", Role::Part, ForwardShadingCostMode::NoLocalLights),
    Mode("lightProbes", "GI / light-probe sampling", Role::Part, ForwardShadingCostMode::NoLightProbes),
    Mode("envReflection", "Environment reflection (IBL specular)", Role::Part, ForwardShadingCostMode::NoEnvReflection),
    Mode("contactShadow", "of which contact shadow", Role::Info, ForwardShadingCostMode::NoContactShadow),
    Mode("allShadows", "all shadows (sun + local lights)", Role::Info, ForwardShadingCostMode::NoShadow),
    {"dynamicGiOff", "Dynamic GI system off", Role::System, "full", {{"dynamicGiQuality", "Off"}}},
    {"depthPrepass", "Scene depth prepass on", Role::System, "full", {{"sceneDepthPrepass", "true"}}},
};

constexpr Preset kPresets[] = {
    {"forward", "Meshlet Draw Forward", "generalFull", "full", kForwardSteps},
};

const Preset* FindPreset(std::string_view name) {
    for (const Preset& preset : kPresets) {
        if (name == preset.name)
            return &preset;
    }
    return nullptr;
}

const Step* FindStep(const Preset& preset, std::string_view id) {
    for (const Step& step : preset.steps) {
        if (id == step.id)
            return &step;
    }
    return nullptr;
}

Json ParseOverrideValue(const char* text) {
    Json value = Json::parse(text, nullptr, false);
    return value.is_discarded() ? Json(std::string(text)) : value;
}

// 基準の上書きの上に、この項目の上書きを重ねた画質。基準自身は自分の上書きだけ。
bool BuildSettings(const Preset& preset, const Step& step, const RENDER3D::RenderQualitySettings& start,
                   RENDER3D::RenderQualitySettings& out, std::string& error) {
    const Step* baseline = step.role == Role::Reference ? &step : FindStep(preset, step.baseline);
    out = start;
    for (const Step* layer : {baseline, step.role == Role::Reference ? nullptr : &step}) {
        if (layer == nullptr)
            continue;
        for (const Override& item : layer->overrides) {
            if (item.field != nullptr && !JSONFIELDS::SetField(out, RENDER3D::kRenderQualitySettingsFields, item.field,
                                                               ParseOverrideValue(item.value), error))
                return false;
        }
    }
    out = RENDER3D::NormalizeRenderQualitySettings(out);
    return true;
}

// ---- 区間の計画 ----
struct PlanItem {
    size_t step = 0;
    bool baseline = false;
    uint32_t round = 0;
};

// 基準ごとに「基準 変種 基準 変種 … 基準」と並べる（変種は前後の基準に挟まれる）。奇数回は変種を逆順にして、
// 一方向の揺れを相殺する。変種の無い基準も、通常の描画との差を出すために 1 区間は測る。
std::vector<PlanItem> BuildPlan(const Preset& preset, const std::vector<bool>& unchanged, uint32_t rounds) {
    std::vector<PlanItem> plan;
    for (uint32_t round = 0; round < rounds; ++round) {
        for (size_t reference = 0; reference < preset.steps.size(); ++reference) {
            if (preset.steps[reference].role != Role::Reference)
                continue;
            plan.push_back({reference, true, round});
            std::vector<size_t> variants;
            for (size_t index = 0; index < preset.steps.size(); ++index) {
                const Step& step = preset.steps[index];
                if (step.role != Role::Reference && !unchanged[index] &&
                    std::string_view(step.baseline) == preset.steps[reference].id)
                    variants.push_back(index);
            }
            if (round % 2 == 1)
                std::reverse(variants.begin(), variants.end());
            for (const size_t variant : variants) {
                plan.push_back({variant, false, round});
                plan.push_back({reference, true, round});
            }
        }
    }
    return plan;
}

// 設定が効くのを待つ上限（フレーム数）。診断用 PSO の初回作成は 1 フレームの中で終わる。
constexpr uint32_t kEffectTimeoutFrames = 240;
// 標本のうち GPU の計時が届いた割合がこれ未満の区間は使わない。
constexpr double kMinimumGpuCoverage = 0.9;
// 計画を始める前に最初の基準を標本の長さの窓で測り続け、続く 2 窓の対象 pass の平均の差が許容（0.05 ms と 2% の
// 大きい方）以内になったら始める（読み込みや画質の切り替えの直後の揺れを測らない）。上限の窓数に達しても始める。
constexpr uint32_t kMaximumSettleWindows = 12;
constexpr double kSettleToleranceMs = 0.05;
constexpr double kSettleToleranceShare = 0.02;

enum class Phase : uint8_t { Starting, WaitingForEffect, Sampling };

// 区間の捕獲が終わった時に、その結果を受け取る場所（捕獲の側から呼ばれるので、分解が先に終わっても壊れないよう共有する）。
struct SegmentCapture {
    bool done = false;
    Json report;
    std::string message;
};

struct Session {
    Request request;
    const Preset* preset = nullptr;
    RENDER3D::RenderQualitySettings start{};
    std::vector<bool> unchanged;
    std::vector<PlanItem> plan;
    size_t index = 0;
    Phase phase = Phase::Starting;
    uint32_t waitedFrames = 0;
    // 今の区間で forward が描くはずのモード。
    double expectedMode = 0.0;
    std::string sceneId;
    bool sceneKnown = false;
    Json segments = Json::array();
    Json context = Json::object();
    std::string waitNote;
    std::string message;
    std::shared_ptr<SegmentCapture> segmentCapture;
    // 計画の前の落ち着き待ち。その間の窓は区間として記録しない。
    bool settling = true;
    uint32_t settleWindows = 0;
    double settlePrevious = -1.0;

    const Step& CurrentStep() const { return preset->steps[plan[index].step]; }
    std::string Describe() const {
        const Step& step = CurrentStep();
        return std::string(settling ? "settling: " : "") + (plan[index].baseline ? "baseline " : "") + step.label +
               " (round " + std::to_string(plan[index].round + 1) + "/" + std::to_string(request.rounds) + ")";
    }
};

std::unique_ptr<Session> gSession;
Status gLast;
bool gExitRequested = false;
bool gCommandLine = false;
int gExitCode = 0;

Status StatusOf(const Session& session) {
    Status status;
    status.state = CAPTURE::CaptureState::Running;
    status.preset = session.preset->name;
    status.segmentsDone = static_cast<uint32_t>(session.index);
    status.segmentsTotal = static_cast<uint32_t>(session.plan.size());
    status.current = session.index < session.plan.size() ? session.Describe() : std::string();
    status.output = session.request.output;
    return status;
}

Json DescribeSteps(const Preset& preset, const std::vector<bool>& unchanged) {
    Json steps = Json::array();
    for (size_t index = 0; index < preset.steps.size(); ++index) {
        const Step& step = preset.steps[index];
        Json overrides = Json::object();
        for (const Override& item : step.overrides) {
            if (item.field != nullptr)
                overrides[item.field] = ParseOverrideValue(item.value);
        }
        Json entry = {{"id", step.id},
                      {"label", step.label},
                      {"role", TEXT::EnumToName(kRoleNames, step.role)},
                      {"overrides", std::move(overrides)}};
        if (step.baseline != nullptr)
            entry["baseline"] = step.baseline;
        if (unchanged[index])
            entry["status"] = "unchanged";
        steps.push_back(std::move(entry));
    }
    return steps;
}

Json BuildReport(const Session& session, bool complete) {
    Json report = {{"schemaVersion", DIAGNOSTICS::kBreakdownSchemaVersion},
                   {"kind", "breakdown"},
                   {"preset", session.preset->name},
                   {"targetPass", session.preset->targetPass},
                   {"totalReference", session.preset->totalReference},
                   {"productionReference", session.preset->productionReference},
                   {"origin", session.request.commandLine ? "commandLine" : "operation"},
                   {"capturedAt", CAPTURE::LocalTimestamp(false)},
                   {"complete", complete},
                   {"message", session.message},
                   {"rounds", session.request.rounds},
                   {"warmupFrames", session.request.warmupFrames},
                   {"sampleFrames", session.request.sampleFrames},
                   {"settleWindows", session.settleWindows},
                   {"startRenderQuality", RENDER3D::SerializeRenderQualitySettings(session.start)},
                   {"context", session.context},
                   {"steps", DescribeSteps(*session.preset, session.unchanged)},
                   {"segments", session.segments}};
    report["analysis"] = DIAGNOSTICS::AnalyzeBreakdown(report);
    return report;
}

// 区間の結果を記録する。capture が使えなければ status だけ（note に理由）。
void RecordSegment(Session& session, const char* status, const Json& capture, const std::string& note) {
    const PlanItem& item = session.plan[session.index];
    const Step& step = session.CurrentStep();
    Json segment = {{"index", session.index},
                    {"round", item.round},
                    {"config", step.id},
                    {"kind", item.baseline ? "baseline" : "variant"},
                    {"status", status}};
    if (!item.baseline)
        segment["baseline"] = step.baseline;
    if (!note.empty())
        segment["note"] = note;
    if (capture.is_object()) {
        const Json& passes = capture.value("gpuPasses", Json::object());
        const Json& target = passes.value(session.preset->targetPass, Json::object());
        const Json& frame = passes.value("Command Frame", Json::object());
        if (target.value("meanMs", Json()).is_number())
            segment["targetMs"] = target["meanMs"];
        if (frame.value("meanMs", Json()).is_number())
            segment["frameMs"] = frame["meanMs"];
        segment["samples"] = capture.value("sampleFramesObserved", 0);
        segment["gpuFrames"] = capture.value("gpuFramesObserved", 0);
        // 画質は区間ごとに上書きしたもの。結果には分解を始めた時の画質（startRenderQuality）を書く。
        if (session.context.empty() && capture.contains("context")) {
            session.context = capture["context"];
            session.context.erase("renderQuality");
        }
    }
    session.segments.push_back(std::move(segment));
}

void LogLines(const Json& report) {
    for (const std::string& line : DIAGNOSTICS::FormatBreakdownTable(report)) {
        CORE::LogInfo("[Breakdown] " + line);
        std::fprintf(stdout, "%s\n", line.c_str());
    }
    std::fflush(stdout);
}

// 結果を書いて、最後の状態にする。画質は呼び出し側が戻しておく。
void Conclude(bool complete) {
    Session& session = *gSession;
    Json report = BuildReport(session, complete);
    std::error_code directoryError;
    if (session.request.output.has_parent_path())
        std::filesystem::create_directories(session.request.output.parent_path(), directoryError);
    std::string writeMessage;
    const bool written = SERIALIZATION::JSON::WriteJsonFile(session.request.output, report, &writeMessage);
    gLast = StatusOf(session);
    gLast.segmentsDone = static_cast<uint32_t>(session.index);
    gLast.current.clear();
    gLast.state = written ? CAPTURE::CaptureState::Finished : CAPTURE::CaptureState::Failed;
    gLast.message = written ? session.message : writeMessage;
    if (written) {
        CORE::LogInfo("[Breakdown] report saved: " + TEXT::PathToUtf8(session.request.output));
        LogLines(report);
    } else {
        CORE::LogError("[Breakdown] report failed: " + writeMessage);
    }
    gLast.report = std::move(report);
    gCommandLine = session.request.commandLine;
    gExitRequested = session.request.exitWhenFinished;
    gExitCode = written && complete ? 0 : 2;
    gSession.reset();
}

// 元の画質に戻して打ち切る。そこまでの区間は書き出す。
void Abort(const std::string& reason) {
    if (gSession->phase == Phase::Sampling)
        CAPTURE::Cancel();
    RENDER3D::SetRenderQualitySettings(gSession->start);
    gSession->message = reason;
    CORE::LogWarn("[Breakdown] stopped: " + reason);
    Conclude(false);
}

// 計画の区間の画質を適用する。効くのは次のフレームから。
bool ApplyCurrent(Session& session) {
    RENDER3D::RenderQualitySettings settings{};
    std::string error;
    if (!BuildSettings(*session.preset, session.CurrentStep(), session.start, settings, error)) {
        session.message = error;
        return false;
    }
    session.expectedMode = static_cast<double>(settings.forwardCostMode);
    RENDER3D::SetRenderQualitySettings(settings);
    session.phase = Phase::WaitingForEffect;
    session.waitedFrames = 0;
    session.waitNote.clear();
    return true;
}

// 画質が実行時へ反映され、前向きの後端が頼んだモードで描いたと報告したフレームか。
bool EffectConfirmed(Session& session, uint64_t frameId) {
    if (!SERVICES::IsRenderQualityApplied()) {
        session.waitNote = "the render quality was not applied by the frame loop";
        return false;
    }
    const std::vector<METRIC::MetricReading> metrics = METRIC::Snapshot(frameId);
    const METRIC::MetricReading* mode = METRIC::FindReading(metrics, METRIC::kForwardCostMode);
    if (mode == nullptr || mode->status != METRIC::MetricStatus::Fresh || mode->sourceFrame != frameId) {
        session.waitNote = "the forward pass did not run in the frame";
        return false;
    }
    if (mode->value != session.expectedMode) {
        session.waitNote =
            "the forward pass drew with mode " +
            std::string(TEXT::EnumToName(RENDER3D::kForwardShadingCostModeNames,
                                         static_cast<ForwardShadingCostMode>(static_cast<uint8_t>(mode->value)), "?")) +
            " instead of the requested one";
        return false;
    }
    return true;
}

const char* SegmentStatus(const Json& capture) {
    const double samples = capture.value("sampleFramesObserved", 0.0);
    if (!capture.value("captureComplete", false))
        return "incomplete";
    if (!capture.value("configurationStable", false) || capture.value("rejectedRenderSamples", 0) != 0)
        return "unstable";
    if (capture.value("gpuFramesObserved", 0.0) < samples * kMinimumGpuCoverage)
        return "incomplete";
    return "ok";
}

// 区間の結果を記録して次へ進む。全部終われば画質を戻して書き出す。
void Advance(Session& session) {
    ++session.index;
    if (session.index >= session.plan.size()) {
        RENDER3D::SetRenderQualitySettings(session.start);
        Conclude(true);
        return;
    }
    if (!ApplyCurrent(session))
        Abort(session.message);
}

// 落ち着き待ちの窓を 1 つ終えた。前の窓との差が許容以内か、窓が上限に達したら計画を始める。
void Settle(Session& session, const Json& capture) {
    ++session.settleWindows;
    double mean = -1.0;
    if (capture.is_object()) {
        const Json& passes = capture.value("gpuPasses", Json::object());
        const Json& target = passes.value(session.preset->targetPass, Json::object());
        if (const Json& meanMs = target.value("meanMs", Json()); meanMs.is_number())
            mean = meanMs.get<double>();
    }
    const bool steady = mean >= 0.0 && session.settlePrevious >= 0.0 &&
                        std::abs(mean - session.settlePrevious) <=
                            (std::max)(kSettleToleranceMs, kSettleToleranceShare * session.settlePrevious);
    session.settlePrevious = mean;
    session.settling = !steady && session.settleWindows < kMaximumSettleWindows;
}

void Tick(Session& session, uint64_t frameId) {
    if (session.phase == Phase::Starting) {
        if (!ApplyCurrent(session))
            Abort(session.message);
        return;
    }
    if (session.phase == Phase::WaitingForEffect) {
        ++session.waitedFrames;
        if (EffectConfirmed(session, frameId)) {
            CAPTURE::Request request;
            request.warmupFrames = session.request.warmupFrames;
            request.sampleFrames = session.request.sampleFrames;
            session.segmentCapture = std::make_shared<SegmentCapture>();
            request.onFinished = [result = session.segmentCapture](const Json& report, const std::string& message) {
                result->done = true;
                result->report = report;
                result->message = message;
            };
            std::string error;
            if (CAPTURE::Start(std::move(request), error)) {
                session.phase = Phase::Sampling;
                return;
            }
            session.waitNote = error;
        }
        if (session.waitedFrames >= kEffectTimeoutFrames) {
            CORE::LogWarn("[Breakdown] " + std::string(session.CurrentStep().label) +
                          " did not take effect: " + session.waitNote);
            RecordSegment(session, "notApplied", Json(), session.waitNote);
            Advance(session);
        }
        return;
    }
    if (!session.segmentCapture->done)
        return;
    if (session.settling) {
        Settle(session, session.segmentCapture->report);
        // 画質はそのまま。次のフレームから次の窓（落ち着いたら最初の区間）を捕る。
        session.phase = Phase::WaitingForEffect;
        session.waitedFrames = 0;
        return;
    }
    RecordSegment(session, SegmentStatus(session.segmentCapture->report), session.segmentCapture->report,
                  session.segmentCapture->message);
    Advance(session);
}

} // namespace

std::vector<std::string_view> PresetNames() {
    std::vector<std::string_view> names;
    for (const Preset& preset : kPresets)
        names.push_back(preset.name);
    return names;
}

bool Start(Request request, std::string& error) {
    if (gSession || CAPTURE::GetStatus().state == CAPTURE::CaptureState::Running) {
        error = "A performance capture or breakdown is already running.";
        return false;
    }
    const Preset* preset = FindPreset(request.preset);
    if (preset == nullptr) {
        error = "Unknown breakdown preset: " + request.preset;
        return false;
    }
    if (request.output.empty()) {
        error = "A performance breakdown needs an output path.";
        return false;
    }
    request.rounds = std::clamp(request.rounds, kMinimumRounds, kMaximumRounds);
    auto session = std::make_unique<Session>();
    session->request = std::move(request);
    session->preset = preset;
    session->start = RENDER3D::GetRenderQualitySettings();
    // 設定が今のままの項目は測る意味が無い（例：GI が既に切ってある場面の「GI を切る」）。
    session->unchanged.assign(preset->steps.size(), false);
    for (size_t index = 0; index < preset->steps.size(); ++index) {
        const Step& step = preset->steps[index];
        if (step.role == Role::Reference)
            continue;
        RENDER3D::RenderQualitySettings variant{};
        RENDER3D::RenderQualitySettings baseline{};
        if (!BuildSettings(*preset, step, session->start, variant, error) ||
            !BuildSettings(*preset, *FindStep(*preset, step.baseline), session->start, baseline, error))
            return false;
        session->unchanged[index] = variant == baseline;
    }
    session->plan = BuildPlan(*preset, session->unchanged, session->request.rounds);
    gLast = {};
    gSession = std::move(session);
    CORE::LogInfo("[Breakdown] started: preset " + std::string(preset->name) + ", " +
                  std::to_string(gSession->plan.size()) + " segments");
    return true;
}

void StartFromCommandLine() {
    const auto output = ReadRuntimeCommandLinePath(L"--benchmark-breakdown");
    if (!output)
        return;
    Request request;
    request.output = *output;
    if (const auto preset = ReadRuntimeCommandLinePath(L"--benchmark-breakdown-preset"))
        request.preset = TEXT::PathToUtf8(*preset);
    request.warmupFrames = ReadRuntimeCommandLineUint32(L"--benchmark-warmup-frames").value_or(request.warmupFrames);
    request.sampleFrames = ReadRuntimeCommandLineUint32(L"--benchmark-sample-frames").value_or(request.sampleFrames);
    request.rounds = ReadRuntimeCommandLineUint32(L"--benchmark-breakdown-rounds").value_or(request.rounds);
    request.exitWhenFinished = HasRuntimeCommandLineFlag(L"--benchmark-exit");
    request.commandLine = true;
    std::string error;
    if (!Start(std::move(request), error)) {
        CORE::LogError("[Breakdown] " + error);
        // 開始できなかったコマンドラインの分解は、ホストを終了させて失敗を終了コードに出す。
        gCommandLine = true;
        gExitCode = 2;
        gExitRequested = HasRuntimeCommandLineFlag(L"--benchmark-exit");
    }
}

void OnPresentedFrame(uint64_t frameId, bool applicationFrame, const IScene* scene) {
    if (!gSession || !applicationFrame)
        return;
    Session& session = *gSession;
    const std::string sceneId = scene != nullptr ? scene->GetSceneId() : std::string();
    if (!session.sceneKnown) {
        session.sceneId = sceneId;
        session.sceneKnown = true;
    } else if (sceneId != session.sceneId) {
        Abort("The scene changed while the breakdown ran.");
        return;
    }
    Tick(session, frameId);
}

bool IsRunning() noexcept { return gSession != nullptr; }

Status GetStatus() { return gSession ? StatusOf(*gSession) : gLast; }

bool Cancel() {
    if (!gSession)
        return false;
    Abort("The breakdown was cancelled.");
    return true;
}

bool ShouldExitHost() noexcept { return gExitRequested; }

int ExitCode() noexcept { return gCommandLine ? gExitCode : 0; }

void Shutdown() {
    if (!gSession)
        return;
    Cancel();
}

} // namespace HIKARI::RUNTIME::PERFORMANCE_BREAKDOWN
