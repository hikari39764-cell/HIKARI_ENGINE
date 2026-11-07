#include "Render3D/Vfx/HIKARI_FluidVolumePass.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <span>

#include <d3dx12.h>

#include "Core/HIKARI_Logger.h"
#include "Gfx/HIKARI_DynamicUploadBuffer.h"
#include "Gfx/HIKARI_GpuFrameProfiler.h"
#include "Gfx/HIKARI_PixProfiler.h"
#include "Render3D/Lighting/Atmosphere/HIKARI_AtmosphereParameters.h"
#include "Render3D/Vfx/Fluid/HIKARI_FluidRender.h"
#include "Render3D/Vfx/Fluid/HIKARI_FluidSolver.h"

namespace HIKARI::RENDER3D::FLUID {
namespace {
using INTERNAL::FluidDomainResources;
using INTERNAL::FluidRecorder;
using PARTICLES::ParticleEmitterInstanceKey;

// 作り直した直後の預熱で 1 フレームに進める子步の上限と、仕事（格の数 × 子步）の上限（預熱の間は域を描かない）。
// 128³ の域は 48 子步（1 子步約 3 ms）。それより大きな域は子步を減らし、1 フレームが秒を超えて止まらないようにする
// （256³ で 48 子步だと 1 フレーム 1 秒を超えた）。
constexpr uint32_t kPrewarmStepsPerFrame = 48u;
constexpr uint64_t kPrewarmCellStepsPerFrame = uint64_t{kPrewarmStepsPerFrame} * 128u * 128u * 128u;
// 預熱を終える前に作り直された回数がこれに届いたら、描かれないままの域として警告する。
constexpr uint32_t kUnfinishedRestartWarning = 4u;
// 局所空間の慣性：挂点の速度はフレームの長さで重みを付けて均す（時定数 [s]）。挂点の位置は 1 フレーム遅れることがあり
// （VFX は LateUpdate で動く物体を前のフレームの位置で読む）、フレームの長さが揺れると移動量 / 時間が大きく跳ねるため。
// 1 フレームの速度の変化の上限 [m/s]（顿帧の後で烟が吹き飛ばないように）。kFluidTeleportSpeed を超えたフレームは
// 見積もりを止め、烟を押さない。
constexpr float kMotionSmoothingSeconds = 0.1f;
constexpr float kMaximumFrameVelocityChange = 12.0f;
// 占有：この密度を超える砖块を「烟がある」とみなす。
constexpr float kOccupiedDensity = 1.0e-3f;

struct alignas(16) DomainConstants {
    uint32_t size[4];
    float cell[4];
    float motion[4];
    float dissipation[4];
    float up[4];
    float inertia[4];
    uint32_t interactions[4];
};
static_assert(sizeof(DomainConstants) == 112u);

// 受け取ったフレームの後の子步へ渡し続ける一度きりの事件：冲量（向きのあるものは最初の子步だけ、向きの無いもの（湧き出し）
// は残りの時間がある間）と注入（提交したフレームの時間ぶんを、子步ごとに最大 1 子步ずつ入れる）。子步の無いフレームの分は
// 次のフレームの子步へ持ち越すので、注入はフレームと子步の刻みがずれても落ちず、重ならない。
struct CarriedInteraction {
    FluidInteraction interaction{};
    double remainingSeconds = 0.0;
    // 注入：提交したフレームの時間（動いた線分の長さを決める）。
    float frameSeconds = 0.0f;
};

// 源ごと（FluidSourceSubmission::id）の、まだ子步に渡していない動きの始点（最後に走った子步の終わり。域の局所の位置と
// 向き、世界の拡縮）と、子步の無かったフレームから溜めた burst。次に走る子步たちが始点から今の位置までを等分して掃く。
struct SourceTrack {
    uint64_t id = 0u;
    MATH::Vec3 position{};
    MATH::Quat rotation = MATH::Quat::Identity();
    MATH::Vec3 scale{1.0f, 1.0f, 1.0f};
    float elapsedSeconds = 0.0f;
    float pendingBurst = 0.0f;
    bool seen = false;
};

// このフレームの源 1 つの今の位置（域の局所）と、追っている始点の番号（追わない源は -1）。
struct SourceMotion {
    int track = -1;
    MATH::Vec3 position{};
    MATH::Quat rotation = MATH::Quat::Identity();
};

struct DomainState {
    ParticleEmitterInstanceKey key{};
    FluidDomainResources resources{};
    MATH::Vec3 layoutSize{};
    uint32_t layoutResolution = 0u;
    FluidStepClock clock{};
    // 残りの預熱の子步（秒から子步の長さを引いていくと丸めの残りが 0 にならず、預熱が終わらない組み合わせがあった）。
    uint64_t prewarmSteps = 0u;
    // 預熱を終える前に作り直された回数（描いた時に 0 へ戻す）。
    uint32_t unfinishedRestarts = 0u;
    uint32_t velocityIndex = 0u;
    uint32_t densityIndex = 0u;
    // 密度が変わるたびに増やす（子步を進めた・作り直した）。描く側は同じ版なら光照体積を書き直さない。
    uint64_t densityVersion = 0u;
    bool needsClear = true;
    // 作り直した回数（描画の履歴が古い烟を混ぜないよう、描く側へ渡す）。
    uint32_t generation = 0u;
    // 最後に時間を進めた描画入力の帧号。同じ入力を別の視図や交付待ちで再び受けても二度は進めない。
    uint64_t lastSimulatedFrame = UINT64_MAX;
    // 解く空間（世界空間の域は作り直した時の変換に固定、局所空間の域は毎フレームの変換）。
    MATH::Vec3 framePosition{};
    MATH::Quat frameRotation = MATH::Quat::Identity();
    MATH::Vec3 previousPosition{};
    // 均した挂点の速度（世界 [m/s]、作り直した時は静止。動いている物体に付けた烟は最初に後ろへ流れる）。
    MATH::Vec3 smoothedVelocity{};
    bool motionInitialized = false;
    // まだ子步に渡していない挂点の速度の変化（世界 [m/s]）。次に走る子步が一度だけ逆向きに入れるので、子步の数が
    // フレームごとに変わっても烟が受ける速さの合計は挂点の速度の変化と等しい。
    MATH::Vec3 pendingInertia{};
    // 解いたゲーム時間（細部のノイズを流れに沿って運ぶ時計。float の精度のため周期で折る）。
    float simulationSeconds = 0.0f;
    std::vector<SourceTrack> tracks{};
    // 子步へ渡す冲量と注入（子步の無いフレームの分は次へ持ち越す）と、最後に押すものを受け取ったフレーム。
    std::vector<CarriedInteraction> carried{};
    uint64_t lastInteractionFrame = UINT64_MAX;
    std::array<uint64_t, GFX::kFrameResourceCount> readbackFence{};
    std::array<uint64_t, GFX::kFrameResourceCount> readbackSteps{};
    std::array<bool, GFX::kFrameResourceCount> readbackDiagnostics{};
    std::array<bool, GFX::kFrameResourceCount> readbackPending{};
    // 回読に汇の吸い込み量が入っているか（そのフレームに数えた子步があったか）と、押すものの表の行ごとの reportKey。
    std::array<bool, GFX::kFrameResourceCount> readbackSinks{};
    std::array<std::array<uint64_t, kMaximumFluidInteractionsPerDomain>, GFX::kFrameResourceCount> readbackSinkKeys{};
    FluidDomainStats stats{};
};

// 一度に溜めておく汇の報告の上限（受け取る場面が無い間に増え続けないように）。
constexpr size_t kMaximumPendingSinkReports = 4096u;

bool IsFenceComplete(const GFX::Context& context, uint64_t fence) noexcept {
    return fence == 0u || (context.frameFence != nullptr && context.frameFence->GetCompletedValue() >= fence);
}

// 子步の長さ [s]（作者の substepRate を 15〜240 Hz に収める）。
double SubstepSeconds(const FluidDomainSettings& settings) noexcept {
    return 1.0 / static_cast<double>(std::clamp(settings.substepRate, 15.0f, 240.0f));
}

// 預熱の 1 フレームの子步の数：仕事の上限を域の格の数で割る（1〜kPrewarmStepsPerFrame）。
uint32_t PrewarmStepsPerFrame(const FluidGridLayout& layout) noexcept {
    const uint64_t cells = uint64_t{layout.cells[0]} * layout.cells[1] * layout.cells[2];
    return static_cast<uint32_t>(
        std::clamp<uint64_t>(kPrewarmCellStepsPerFrame / (std::max<uint64_t>)(cells, 1u), 1u, kPrewarmStepsPerFrame));
}

// 預熱を終える前に作り直され続けて描かれない域の警告（どの層の、どんな域か、作り直しの理由）。
void WarnUnfinishedPrewarm(const FluidDomainSubmission& submission, const FluidGridLayout& layout, bool relayout) {
    const FluidDomainSettings& settings = submission.settings;
    char message[512];
    std::snprintf(message, sizeof(message),
                  "[FluidSmoke] Fluid domain (VFX layer %u, %.1f x %.1f x %.1f m, %u x %u x %u cells, prewarm %.1f s "
                  "at %.0f Hz) restarted %u times before its prewarm finished and is not drawn: %s.",
                  submission.key.emitterIndex, settings.domainSize.x, settings.domainSize.y, settings.domainSize.z,
                  layout.cells[0], layout.cells[1], layout.cells[2], settings.prewarmSeconds,
                  1.0 / SubstepSeconds(settings), kUnfinishedRestartWarning,
                  relayout ? "the grid keeps changing (domain size, resolution or fluid quality)"
                           : "playback keeps restarting it (seek, time jump or replay every frame)");
    HIKARI_LOG_WARN(message);
}
} // namespace

struct FluidVolumePass::Impl {
    INTERNAL::FluidSolverPipeline pipeline{};
    std::array<DX::DynamicUploadBuffer, GFX::kFrameResourceCount> uploads{};
    std::vector<std::unique_ptr<DomainState>> domains{};
    GFX::Context context{};
    FluidVolumePassStats stats{};
    uint64_t statsFrameFence = 0u;
    std::optional<FluidDebugSliceRequest> sliceRequest{};
    INTERNAL::FluidVolume slice{};
    FluidDebugSliceView sliceView{};
    FluidFlowView flowView{};
    INTERNAL::FluidRenderer renderer{};
    // 最後の PrepareFrame で解いた、描く域（預熱の済んだもの）。続く RenderVolumes がその視図へ描く。
    std::vector<INTERNAL::FluidRenderDomain> renderList{};
    // 域ごとに選んだ、このフレームの子步すべてへ渡す障碍物と力（作業用）。
    std::vector<FluidInteraction> steadyInteractions{};
    // 域ごとに選んだ、このフレームの源（域自身の源と、届く場面の源）とその動き（作業用）。
    std::vector<FluidSourceSubmission> frameSources{};
    std::vector<SourceMotion> frameMotion{};
    // 回読が届いた汇の吸い込み量（TakeSinkReports で場面へ渡す）。
    std::vector<FluidSinkReport> sinkReports{};

    DomainState* Find(const ParticleEmitterInstanceKey& key) noexcept {
        for (auto& domain : domains)
            if (domain->key == key)
                return domain.get();
        return nullptr;
    }

    void Retire(size_t index) {
        INTERNAL::RetireFluidDomainResources(context, domains[index]->resources);
        domains.erase(domains.begin() + static_cast<std::ptrdiff_t>(index));
    }

    void ReadCompleted(DomainState& domain);
    bool PrepareDomain(DomainState& domain, const FluidDomainSubmission& submission, const FluidQualityPolicy& policy);
    void GatherSources(DomainState& domain, const FluidDomainSubmission& submission,
                       std::span<const FluidSourceSubmission> sceneSources, double step, bool advance);
    void Solve(DomainState& domain, const FluidDomainSubmission& submission,
               std::span<const FluidSourceSubmission> sceneSources, const FluidQualityPolicy& policy,
               const FluidFrameSettings& settings, bool advance);
};

// 回読の輪で GPU が終えた分を読む：汇の吸い込み量（フレームごとに別の量なので古い回読でも読む）と、砖块ごとの最大密度
// （占有）、診断を記録した時は散度・密度の総量。
void FluidVolumePass::Impl::ReadCompleted(DomainState& domain) {
    for (uint32_t slot = 0u; slot < GFX::kFrameResourceCount; ++slot) {
        if (!domain.readbackPending[slot] || !IsFenceComplete(context, domain.readbackFence[slot]))
            continue;
        // 届いた槽は必ず空ける（次のフレームの回読に使う）。
        domain.readbackPending[slot] = false;
        ID3D12Resource* readback = domain.resources.readback[slot].Get();
        const D3D12_RANGE range{0, domain.resources.statsBytes};
        void* mapped = nullptr;
        if (FAILED(readback->Map(0, &range, &mapped)) || mapped == nullptr)
            continue;
        const auto* words = static_cast<const float*>(mapped);
        if (domain.readbackSinks[slot]) {
            const float* removed = words + domain.resources.SinkOffset() / 4u;
            for (uint32_t row = 0u; row < kMaximumFluidInteractionsPerDomain; ++row) {
                const uint64_t key = domain.readbackSinkKeys[slot][row];
                if (key != 0u && removed[row] > 0.0f && sinkReports.size() < kMaximumPendingSinkReports)
                    sinkReports.push_back({key, static_cast<double>(removed[row])});
            }
        }
        // 占有と診断は読んだものより古い回読を捨てる。
        if (domain.readbackSteps[slot] < domain.stats.readbackSteps) {
            const D3D12_RANGE none{0, 0};
            readback->Unmap(0, &none);
            continue;
        }
        const uint32_t bricks = domain.resources.BrickCount();
        uint32_t active = 0u;
        for (uint32_t brick = 0u; brick < bricks; ++brick)
            active += words[brick] > kOccupiedDensity ? 1u : 0u;
        domain.stats.activeBricks = active;
        domain.stats.totalBricks = bricks;
        domain.stats.readbackSteps = domain.readbackSteps[slot];
        domain.stats.diagnosticsValid = domain.readbackDiagnostics[slot];
        if (domain.readbackDiagnostics[slot]) {
            const float* diagnostics = words + bricks;
            float maxDivergence = 0.0f;
            double total = 0.0;
            for (uint32_t brick = 0u; brick < bricks; ++brick) {
                maxDivergence = (std::max)(maxDivergence, diagnostics[brick * 2u]);
                total += diagnostics[brick * 2u + 1u];
            }
            domain.stats.maxDivergence = maxDivergence;
            domain.stats.densityTotal = static_cast<float>(total);
        }
        const D3D12_RANGE written{0, 0};
        readback->Unmap(0, &written);
    }
}

// 作り直しの判断：初めて・再生側の作り直し・格子の大きさ（域の大きさ、分辨率、画質の倍率）が変わった時。
bool FluidVolumePass::Impl::PrepareDomain(DomainState& domain, const FluidDomainSubmission& submission,
                                          const FluidQualityPolicy& policy) {
    // 域の大きさは作者の domainSize だけで決める（層の拡縮・その動画で格子を作り直さない）。
    const MATH::Vec3 size = submission.settings.domainSize;
    const uint32_t resolution = std::clamp(
        static_cast<uint32_t>(std::lround(static_cast<float>(submission.settings.resolution) * policy.resolutionScale)),
        kFluidResolutionMin, kFluidResolutionMax);
    const bool cellColor = submission.settings.cellColor;
    const bool relayout =
        !domain.resources.stats || domain.layoutResolution != resolution ||
        domain.resources.layout.cellColor != cellColor || std::abs(domain.layoutSize.x - size.x) > 1.0e-4f ||
        std::abs(domain.layoutSize.y - size.y) > 1.0e-4f || std::abs(domain.layoutSize.z - size.z) > 1.0e-4f;
    if (relayout) {
        INTERNAL::RetireFluidDomainResources(context, domain.resources);
        FluidGridLayout layout = ResolveFluidGridLayout(size, resolution);
        layout.cellColor = cellColor;
        if (!INTERNAL::CreateFluidDomainResources(context.device, layout, domain.resources)) {
            INTERNAL::RetireFluidDomainResources(context, domain.resources);
            ++stats.resourceFailures;
            return false;
        }
        domain.layoutSize = size;
        domain.layoutResolution = resolution;
        domain.readbackPending = {};
    }
    if (relayout || submission.resetSimulation) {
        // 預熱が終わる前の作り直しが続く域（毎フレームの拖时间や格子の変更）は描かれない。黙って消えないよう警告する。
        if (domain.generation != 0u && domain.prewarmSteps > 0u &&
            ++domain.unfinishedRestarts == kUnfinishedRestartWarning)
            WarnUnfinishedPrewarm(submission, domain.resources.layout, relayout);
        domain.needsClear = true;
        ++domain.generation;
        domain.clock = {};
        const double prewarm = static_cast<double>((std::max)(0.0f, submission.settings.prewarmSeconds));
        domain.prewarmSteps =
            static_cast<uint64_t>((std::max)(0.0, std::ceil(prewarm / SubstepSeconds(submission.settings) - 1.0e-6)));
        domain.velocityIndex = domain.densityIndex = 0u;
        domain.framePosition = submission.worldPosition;
        domain.frameRotation = submission.worldRotation;
        domain.motionInitialized = false;
        domain.smoothedVelocity = {};
        domain.pendingInertia = {};
        domain.tracks.clear();
        domain.carried.clear();
        domain.stats = {};
        // 再起動の前に記録した回読（大きな子步数）が新しい統計を上書きしないよう、輪ごと捨てる。
        domain.readbackPending = {};
    }
    return true;
}

// このフレームの源を選び（域自身の源、続いて形の届く場面の源。表の上限まで）、追っている源の始点を進める：新しい源は
// 今の位置から、瞬間移動（生まれ直し・大きく飛んだ）は掃かずに今の位置から始め、出てこなくなった源は忘れる。
void FluidVolumePass::Impl::GatherSources(DomainState& domain, const FluidDomainSubmission& submission,
                                          std::span<const FluidSourceSubmission> sceneSources, double step,
                                          bool advance) {
    const auto toLocal = [&](const MATH::Vec3& world) {
        return INTERNAL::RotateToFluidLocal(domain.frameRotation, world - domain.framePosition);
    };
    const auto find = [&](uint64_t id) -> int {
        for (size_t index = 0u; index < domain.tracks.size(); ++index)
            if (domain.tracks[index].id == id)
                return static_cast<int>(index);
        return -1;
    };
    frameSources.clear();
    for (const FluidSourceSubmission& source : submission.sources)
        if (frameSources.size() < kMaximumFluidSourcesPerDomain)
            frameSources.push_back(source);
    const float domainRadius = 0.5f * MATH::Length(domain.layoutSize);
    for (const FluidSourceSubmission& source : sceneSources) {
        if (frameSources.size() >= kMaximumFluidSourcesPerDomain)
            break;
        // 今の位置か、まだ掃いていない始点から届けば入れる（域を横切って出ていく速い源の尾迹も残す）。
        const float reach = domainRadius + INTERNAL::FluidSourceReach(source, domain.resources.layout.cellSize);
        const int track = find(source.id);
        if (MATH::Length(toLocal(source.worldPosition)) <= reach ||
            (track >= 0 && MATH::Length(domain.tracks[static_cast<size_t>(track)].position) <= reach))
            frameSources.push_back(source);
    }
    const MATH::Quat inverseFrame{-domain.frameRotation.x, -domain.frameRotation.y, -domain.frameRotation.z,
                                  domain.frameRotation.w};
    frameMotion.assign(frameSources.size(), {});
    for (size_t index = 0u; index < frameSources.size(); ++index) {
        frameMotion[index].position = toLocal(frameSources[index].worldPosition);
        frameMotion[index].rotation = MATH::MultiplyQ(inverseFrame, frameSources[index].worldRotation);
    }
    if (advance) {
        for (SourceTrack& track : domain.tracks)
            track.seen = false;
        for (size_t index = 0u; index < frameSources.size(); ++index) {
            const FluidSourceSubmission& source = frameSources[index];
            if (source.id == 0u)
                continue;
            const SourceMotion& motion = frameMotion[index];
            int track = find(source.id);
            if (track < 0) {
                domain.tracks.push_back({source.id, motion.position, motion.rotation, source.worldScale});
                track = static_cast<int>(domain.tracks.size() - 1u);
            }
            SourceTrack& tracked = domain.tracks[static_cast<size_t>(track)];
            tracked.seen = true;
            tracked.elapsedSeconds += submission.deltaSeconds;
            tracked.pendingBurst += source.burstDensity;
            const float limit = kFluidTeleportSpeed * (std::max)(tracked.elapsedSeconds, static_cast<float>(step));
            if (MATH::Length(motion.position - tracked.position) > limit) {
                tracked.position = motion.position;
                tracked.rotation = motion.rotation;
                tracked.scale = source.worldScale;
            }
        }
        std::erase_if(domain.tracks, [](const SourceTrack& track) { return !track.seen; });
    }
    for (size_t index = 0u; index < frameSources.size(); ++index)
        frameMotion[index].track = frameSources[index].id != 0u ? find(frameSources[index].id) : -1;
}

void FluidVolumePass::Impl::Solve(DomainState& domain, const FluidDomainSubmission& submission,
                                  std::span<const FluidSourceSubmission> sceneSources, const FluidQualityPolicy& policy,
                                  const FluidFrameSettings& settings, bool advance) {
    const FluidDomainSettings& authored = submission.settings;
    const FluidGridLayout& layout = domain.resources.layout;
    const double step = SubstepSeconds(authored);
    // 局所空間の域は毎フレーム層の変換へ付いていき、挂点の速度の変化を逆向きに烟へ入れる（冲刺の拖尾、急停の回卷）。
    if (submission.localSpace) {
        domain.framePosition = submission.worldPosition;
        domain.frameRotation = submission.worldRotation;
        if (advance && submission.deltaSeconds > 0.0f) {
            const float dt = submission.deltaSeconds;
            const MATH::Vec3 velocity = (submission.worldPosition - domain.previousPosition) * (1.0f / dt);
            // 作り直した最初のフレームは前の位置が無い。瞬間移動のフレームは見積もりを止め、烟を押さない。
            if (domain.motionInitialized && MATH::Length(velocity) <= kFluidTeleportSpeed) {
                const float weight = 1.0f - std::exp(-dt / kMotionSmoothingSeconds);
                MATH::Vec3 change = (velocity - domain.smoothedVelocity) * weight;
                const float magnitude = MATH::Length(change);
                if (magnitude > kMaximumFrameVelocityChange)
                    change = change * (kMaximumFrameVelocityChange / magnitude);
                domain.smoothedVelocity = domain.smoothedVelocity + change;
                domain.pendingInertia = domain.pendingInertia + change;
            }
            domain.motionInitialized = true;
        }
        domain.previousPosition = submission.worldPosition;
    }
    GatherSources(domain, submission, sceneSources, step, advance);
    // 押すもの：域の外接球に届くものだけ。障碍物と力はこのフレームの子步すべてへ、冲量と注入は新しいフレームの分を溜めて
    // 次の子步から渡す（注入はこのフレームの時間ぶん）。局所空間の域は障碍物と注入の速度を域に対する速度にする。
    const MATH::Vec3 domainVelocity = submission.localSpace ? domain.smoothedVelocity : MATH::Vec3{};
    const float domainRadius = 0.5f * MATH::Length(domain.layoutSize);
    const bool newInteractions = settings.interactionFrame != domain.lastInteractionFrame;
    domain.lastInteractionFrame = settings.interactionFrame;
    steadyInteractions.clear();
    for (const FluidInteraction& interaction : settings.interactions) {
        const MATH::Vec3& extents = interaction.extents;
        const bool obstacle = interaction.kind == FluidInteractionKind::Obstacle;
        const float reach = obstacle && interaction.shape == FluidInteractionShape::Capsule ? extents.x + extents.y
                            : obstacle && interaction.shape == FluidInteractionShape::Box   ? MATH::Length(extents)
                                                                                            : extents.x;
        const bool emit = interaction.kind == FluidInteractionKind::Emit;
        // 注入はこのフレームに動いた線分のどこかが届けば入れる。
        const float swept = emit ? MATH::Length(interaction.displacement) : 0.0f;
        if (MATH::Length(interaction.worldPosition - domain.framePosition) > domainRadius + reach + swept)
            continue;
        if (interaction.kind == FluidInteractionKind::Impulse) {
            if (newInteractions && domain.carried.size() < kMaximumFluidInteractionsPerDomain) {
                const bool radial = MATH::Length(interaction.direction) <= 0.0f;
                domain.carried.push_back({interaction, radial ? kFluidRadialImpulseSeconds : 0.0});
            }
        } else if (emit) {
            if (newInteractions && advance && submission.deltaSeconds > 0.0f &&
                domain.carried.size() < kMaximumFluidInteractionsPerDomain)
                domain.carried.push_back({interaction, submission.deltaSeconds, submission.deltaSeconds});
        } else if (steadyInteractions.size() < kMaximumFluidInteractionsPerDomain) {
            steadyInteractions.push_back(interaction);
        }
    }

    uint32_t prewarmSteps = 0u;
    if (advance && domain.prewarmSteps > 0u) {
        prewarmSteps = static_cast<uint32_t>((std::min<uint64_t>)(domain.prewarmSteps, PrewarmStepsPerFrame(layout)));
        domain.prewarmSteps -= prewarmSteps;
        domain.clock.completedSteps += prewarmSteps;
    }
    const uint32_t steps =
        prewarmSteps + (domain.prewarmSteps > 0u ? 0u
                                                 : domain.clock.Advance(advance ? submission.deltaSeconds : 0.0, step,
                                                                        (std::max)(authored.maxSubsteps, 1u)));
    domain.stats.key = submission.key;
    std::copy(std::begin(layout.cells), std::end(layout.cells), domain.stats.cells);
    domain.stats.levels = layout.levels;
    domain.stats.bytes = domain.resources.bytes;
    domain.stats.completedSteps = domain.clock.completedSteps;
    domain.stats.stepsThisFrame = steps;
    domain.stats.prewarmRemainingSeconds = static_cast<float>(static_cast<double>(domain.prewarmSteps) * step);
    domain.simulationSeconds =
        static_cast<float>(std::fmod(static_cast<double>(domain.clock.completedSteps) * step, 4096.0));

    auto& upload = uploads[context.frameIndex % GFX::kFrameResourceCount];
    ID3D12GraphicsCommandList* commands = context.cmdList;
    const MATH::Vec3 up = INTERNAL::RotateToFluidLocal(domain.frameRotation, MATH::Vec3{0.0f, 1.0f, 0.0f});
    // flags：1 でこの子步の頭に汇の吸い込み量を 0 に戻す（フレームの最初の子步）、2 でこの子步の吸い込みを数える（預熱は数えない）。
    const auto uploadConstants = [&](uint64_t stepIndex, uint32_t interactionCount, const MATH::Vec3& inertia,
                                     uint32_t flags) -> D3D12_GPU_VIRTUAL_ADDRESS {
        DomainConstants constants{};
        constants.size[0] = layout.cells[0];
        constants.size[1] = layout.cells[1];
        constants.size[2] = layout.cells[2];
        constants.size[3] = static_cast<uint32_t>(frameSources.size());
        constants.cell[0] = layout.cellSize;
        constants.cell[1] = 1.0f / layout.cellSize;
        constants.cell[2] = static_cast<float>(step);
        constants.cell[3] = static_cast<float>(std::fmod(static_cast<double>(stepIndex) * step, 65536.0));
        constants.motion[0] = authored.boundary == FluidBoundary::Closed ? 1.0f : 0.0f;
        constants.motion[1] = authored.vorticity;
        constants.motion[2] = authored.buoyancy;
        constants.motion[3] = authored.weight;
        constants.dissipation[0] = authored.densityDissipation;
        constants.dissipation[1] = authored.temperatureDissipation;
        constants.up[0] = up.x;
        constants.up[1] = up.y;
        constants.up[2] = up.z;
        constants.inertia[0] = inertia.x;
        constants.inertia[1] = inertia.y;
        constants.inertia[2] = inertia.z;
        constants.interactions[0] = interactionCount;
        constants.interactions[1] = domain.resources.ActivityOffset();
        constants.interactions[2] = domain.resources.SinkOffset();
        constants.interactions[3] = flags;
        const auto allocation = upload.Allocate(sizeof(constants));
        if (!allocation)
            return 0u;
        std::memcpy(allocation.cpuAddress, &constants, sizeof(constants));
        return allocation.gpuAddress;
    };
    // 子步 stepIndex の源の表。追っている源は始点から今の位置までを、預熱でない子步の数で等分した区間の終わりに置き、
    // その区間を掃かせる（預熱の子步は今の位置で掃かない）。burst は最初の子步だけ。
    const auto uploadSources = [&](uint32_t stepIndex) -> D3D12_GPU_VIRTUAL_ADDRESS {
        const size_t count = (std::max<size_t>)(1u, frameSources.size());
        const auto allocation = upload.Allocate(count * sizeof(INTERNAL::FluidSourceGpu));
        if (!allocation)
            return 0u;
        auto* table = static_cast<INTERNAL::FluidSourceGpu*>(allocation.cpuAddress);
        std::memset(table, 0, count * sizeof(INTERNAL::FluidSourceGpu));
        const float moving = static_cast<float>(steps - prewarmSteps);
        const float from = stepIndex < prewarmSteps ? 1.0f : static_cast<float>(stepIndex - prewarmSteps) / moving;
        const float to = stepIndex < prewarmSteps ? 1.0f : static_cast<float>(stepIndex - prewarmSteps + 1u) / moving;
        for (size_t index = 0; index < frameSources.size(); ++index) {
            FluidSourceSubmission placed = frameSources[index];
            const SourceMotion& motion = frameMotion[index];
            float burst = placed.burstDensity;
            MATH::Vec3 sweep{};
            if (motion.track >= 0) {
                const SourceTrack& track = domain.tracks[static_cast<size_t>(motion.track)];
                burst = track.pendingBurst;
                const MATH::Vec3 begin = MATH::Lerp(track.position, motion.position, from);
                const MATH::Vec3 end = MATH::Lerp(track.position, motion.position, to);
                placed.worldPosition = domain.framePosition + MATH::RotateVector(domain.frameRotation, end);
                placed.worldRotation =
                    MATH::MultiplyQ(domain.frameRotation, MATH::NlerpQ(track.rotation, motion.rotation, to));
                placed.worldScale = MATH::Lerp(track.scale, placed.worldScale, to);
                sweep = MATH::RotateVector(domain.frameRotation, end - begin);
            }
            table[index] = INTERNAL::PackFluidSource(placed, domain.framePosition, domain.frameRotation,
                                                     layout.cellSize, stepIndex == 0u ? burst : 0.0f, sweep);
        }
        return allocation.gpuAddress;
    };
    // 障碍物・力を先に、冲量と注入を後に置く（汇の吸い込み量は表の行ごとに数えるので、冲量と注入が子步の途中で切れても
    // 汇の行が動かないように）。冲量と注入は一度きりの事件なので落とさず、障碍物・力はその残りの席に入る。注入は残りの
    // 時間のうちこの子步の分（最大 1 子步）を入れる。
    const size_t steadyRows =
        (std::min)(steadyInteractions.size(), static_cast<size_t>(kMaximumFluidInteractionsPerDomain));
    const auto uploadInteractions = [&](uint32_t& outCount) -> D3D12_GPU_VIRTUAL_ADDRESS {
        const size_t carried = domain.carried.size();
        const size_t steady = (std::min)(steadyRows, kMaximumFluidInteractionsPerDomain - carried);
        const size_t count = steady + carried;
        outCount = static_cast<uint32_t>(count);
        const auto allocation = upload.Allocate((std::max<size_t>)(count, 1u) * sizeof(INTERNAL::FluidInteractionGpu));
        if (!allocation)
            return 0u;
        auto* table = static_cast<INTERNAL::FluidInteractionGpu*>(allocation.cpuAddress);
        std::memset(table, 0, sizeof(INTERNAL::FluidInteractionGpu));
        for (size_t index = 0; index < count; ++index) {
            if (index < steady) {
                table[index] = INTERNAL::PackFluidInteraction(steadyInteractions[index], domain.framePosition,
                                                              domain.frameRotation, domainVelocity, 0.0f, 1.0f);
                continue;
            }
            const CarriedInteraction& event = domain.carried[index - steady];
            const float share = event.interaction.kind == FluidInteractionKind::Emit
                                    ? static_cast<float>((std::min)(event.remainingSeconds, step) / step)
                                    : 1.0f;
            table[index] = INTERNAL::PackFluidInteraction(event.interaction, domain.framePosition, domain.frameRotation,
                                                          domainVelocity, event.frameSeconds, share);
        }
        return allocation.gpuAddress;
    };

    const D3D12_GPU_VIRTUAL_ADDRESS baseConstants = uploadConstants(domain.clock.completedSteps, 0u, {}, 0u);
    if (baseConstants == 0u)
        return;
    FluidRecorder recorder(commands, pipeline, domain.resources, baseConstants);
    if (domain.needsClear) {
        for (auto& volume : domain.resources.velocity)
            recorder.Clear(volume);
        for (auto& volume : domain.resources.density)
            recorder.Clear(volume);
        for (auto& volume : domain.resources.pressure)
            recorder.Clear(volume);
        domain.needsClear = false;
        ++domain.densityVersion;
    }
    const uint64_t firstStep = domain.clock.completedSteps - steps;
    for (uint32_t index = 0u; index < steps; ++index) {
        uint32_t interactionCount = 0u;
        const D3D12_GPU_VIRTUAL_ADDRESS interactions = uploadInteractions(interactionCount);
        // 挂点の速度の変化は最初の子步で一度に入れる（その子步の加速度 = -Δv / 子步の時間）。
        const MATH::Vec3 inertia = index == 0u
                                       ? INTERNAL::RotateToFluidLocal(domain.frameRotation, domain.pendingInertia) *
                                             static_cast<float>(-authored.inertia / step)
                                       : MATH::Vec3{};
        const uint32_t flags = (index == 0u ? 1u : 0u) | (index >= prewarmSteps ? 2u : 0u);
        const D3D12_GPU_VIRTUAL_ADDRESS constants =
            uploadConstants(firstStep + index, interactionCount, inertia, flags);
        const D3D12_GPU_VIRTUAL_ADDRESS sources = uploadSources(index);
        if (constants == 0u || sources == 0u || interactions == 0u)
            break;
        FluidRecorder stepRecorder(commands, pipeline, domain.resources, constants);
        stepRecorder.Substep(domain.velocityIndex, domain.densityIndex, sources, interactions, policy.vCycles);
        if (index == 0u)
            domain.pendingInertia = {};
        for (CarriedInteraction& event : domain.carried)
            event.remainingSeconds -= step;
        std::erase_if(domain.carried, [](const CarriedInteraction& event) { return event.remainingSeconds <= 1.0e-9; });
    }
    stats.stepsThisFrame += steps;
    if (steps > 0u) {
        ++domain.densityVersion;
        // 子步が今の位置まで掃いたので、始点を今の位置へ進める。
        for (size_t index = 0u; index < frameSources.size(); ++index) {
            if (frameMotion[index].track < 0)
                continue;
            SourceTrack& track = domain.tracks[static_cast<size_t>(frameMotion[index].track)];
            track.position = frameMotion[index].position;
            track.rotation = frameMotion[index].rotation;
            track.scale = frameSources[index].worldScale;
            track.elapsedSeconds = 0.0f;
            track.pendingBurst = 0.0f;
        }
    }

    // 占有・診断・汇の吸い込み量の回読は、子步を進めたフレームだけ（止まっている間は密度が変わらない）。
    const uint32_t slot = context.frameIndex % GFX::kFrameResourceCount;
    if (steps > 0u && !domain.readbackPending[slot]) {
        FluidRecorder statsRecorder(commands, pipeline, domain.resources, baseConstants);
        statsRecorder.Occupancy(domain.densityIndex);
        if (settings.diagnostics)
            statsRecorder.Diagnostics(domain.velocityIndex, domain.densityIndex);
        statsRecorder.CopyStats(domain.resources.readback[slot].Get());
        domain.readbackPending[slot] = true;
        domain.readbackFence[slot] = context.currentFrameRetireFenceValue;
        domain.readbackSteps[slot] = domain.clock.completedSteps;
        domain.readbackDiagnostics[slot] = settings.diagnostics;
        domain.readbackSinks[slot] = steps > prewarmSteps;
        auto& keys = domain.readbackSinkKeys[slot];
        keys.fill(0u);
        for (size_t row = 0u; row < steadyRows; ++row)
            keys[row] = steadyInteractions[row].reportKey;
    }
    if (sliceRequest && sliceRequest->key == domain.key) {
        const uint32_t axis = (std::min)(sliceRequest->axis, 2u);
        const uint32_t u = axis == 0u ? 1u : 0u, v = axis == 2u ? 1u : 2u;
        const uint32_t width = layout.cells[u], height = layout.cells[v];
        if (slice.size[0] != width || slice.size[1] != height) {
            INTERNAL::RetireVolume(context, slice);
            if (!INTERNAL::CreateFluidSliceTexture(context.device, DXGI_FORMAT_R16G16B16A16_FLOAT, width, height,
                                                   slice))
                INTERNAL::RetireVolume(context, slice);
        }
        if (slice.resource) {
            const uint32_t index =
                (std::min)(static_cast<uint32_t>(std::clamp(sliceRequest->position, 0.0f, 1.0f) * layout.cells[axis]),
                           layout.cells[axis] - 1u);
            recorder.Slice(domain.velocityIndex, domain.densityIndex, slice, sliceRequest->field, axis, index,
                           sliceRequest->visualize);
            const auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(slice.resource.Get(), slice.state,
                                                                      D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE |
                                                                          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            commands->ResourceBarrier(1, &barrier);
            slice.state = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            sliceView = {slice.resource.Get(), slice.srv.gpu, width, height, true};
        }
    }
}

FluidVolumePass::FluidVolumePass() : impl_(std::make_unique<Impl>()) {}
FluidVolumePass::~FluidVolumePass() = default;

bool FluidVolumePass::PrepareFrame(const GFX::Context& context, const PARTICLES::ParticleRenderInput* input,
                                   const FluidFrameSettings& settings) {
    Impl& impl = *impl_;
    // 1 フレームに主視図と編集器の視図が続けて呼ぶ。数はフレーム（GPU の退役 fence）ごとに足し合わせる。
    if (context.currentFrameRetireFenceValue != impl.statsFrameFence || context.currentFrameRetireFenceValue == 0u) {
        impl.statsFrameFence = context.currentFrameRetireFenceValue;
        impl.stats.domains = 0u;
        impl.stats.skippedDomains = 0u;
        impl.stats.stepsThisFrame = 0u;
        impl.stats.renderedDomains = 0u;
        impl.sliceView.valid = false;
    }
    impl.stats.perDomain.clear();
    impl.renderList.clear();
    impl.flowView = {};
    if (context.device == nullptr || context.cmdList == nullptr)
        return true;
    if (impl.context.device != nullptr && impl.context.device != context.device) {
        Shutdown();
    }
    impl.context = context;
    // 透明物体の描画は流体の無いフレームでも相機体積を束ねる。
    if (!impl.renderer.BeginFrame(context))
        ++impl.stats.resourceFailures;
    if (input == nullptr) {
        // 入力の無い視図でも、他の視図の域の統計は見せ続ける（編集器が読む）。
        for (const auto& domain : impl.domains)
            impl.stats.perDomain.push_back(domain->stats);
        return true;
    }
    for (const auto& key : input->retiredEmitters)
        for (size_t index = impl.domains.size(); index-- > 0u;)
            if (impl.domains[index]->key == key)
                impl.Retire(index);
    for (auto& domain : impl.domains)
        impl.ReadCompleted(*domain);
    if (input->fluids.empty()) {
        for (const auto& domain : impl.domains)
            impl.stats.perDomain.push_back(domain->stats);
        return true;
    }
    if (!impl.pipeline.Ensure(context.device))
        return false;
    auto& uploads = impl.uploads[context.frameIndex % GFX::kFrameResourceCount];
    if (!uploads.IsInitialized() && !uploads.Init(context.device, 64u * 1024u, GFX::UPLOAD_TRAFFIC::Owner::Fluids))
        return false;
    uploads.ResetForFrame(context.currentFrameRetireFenceValue);

    const FluidQualityPolicy policy = ResolveFluidQualityPolicy(settings.quality);
    GFX::PIX::ScopedGpuEvent event(context.cmdList, GFX::PIX::kColorRender, "FluidSmoke.Simulate");
    GFX::GPU_PROFILE::ScopedGpuTimer timer(context.cmdList, GFX::GPU_PROFILE::Pass::FluidSimulation);
    ID3D12DescriptorHeap* heaps[] = {context.srvHeap};
    if (context.srvHeap != nullptr)
        context.cmdList->SetDescriptorHeaps(1, heaps);
    uint32_t solved = 0u;
    for (const FluidDomainSubmission& submission : input->fluids) {
        DomainState* domain = impl.Find(submission.key);
        if (domain == nullptr) {
            impl.domains.push_back(std::make_unique<DomainState>());
            domain = impl.domains.back().get();
            domain->key = submission.key;
        }
        // 画質の上限を超えた域と、見えない（层の有効区間の外の）域は解かない（状態はそのまま残る）。
        if (!submission.visible || solved >= policy.maxDomains) {
            ++impl.stats.skippedDomains;
            continue;
        }
        if (!impl.PrepareDomain(*domain, submission, policy))
            continue;
        const bool advance = domain->lastSimulatedFrame != input->frameIndex;
        domain->lastSimulatedFrame = input->frameIndex;
        impl.Solve(*domain, submission, input->fluidSources, policy, settings, advance);
        ++solved;
        if (domain->prewarmSteps > 0u || impl.renderList.size() >= INTERNAL::kMaximumRenderedFluidDomains)
            continue;
        domain->unfinishedRestarts = 0u;
        INTERNAL::FluidRenderDomain& render = impl.renderList.emplace_back();
        render.density = &domain->resources.density[domain->densityIndex];
        render.velocity = &domain->resources.velocity[domain->velocityIndex];
        render.light = &domain->resources.light;
        render.lightKey = &domain->resources.lightKey;
        render.densityVersion = domain->densityVersion;
        render.layout = domain->resources.layout;
        render.position = domain->framePosition;
        render.rotation = domain->frameRotation;
        render.settings = submission.settings;
        render.colorMultiplier = submission.colorMultiplier;
        render.simulationSeconds = domain->simulationSeconds;
        render.pendingSeconds =
            static_cast<float>((std::min)(domain->clock.pendingSeconds, SubstepSeconds(submission.settings)));
        render.frameVelocity = submission.localSpace ? domain->smoothedVelocity : MATH::Vec3{};
    }
    impl.flowView = impl.renderer.PrepareFlow(context, impl.renderList);
    impl.stats.domains += solved;
    impl.stats.bytes = 0u;
    for (const auto& domain : impl.domains) {
        impl.stats.bytes += domain->resources.bytes;
        impl.stats.perDomain.push_back(domain->stats);
    }
    return true;
}

bool FluidVolumePass::RenderVolumes(const GFX::Context& context, const FluidRenderView& view,
                                    const std::function<bool()>& bindSceneTarget) {
    Impl& impl = *impl_;
    if (impl.renderList.empty())
        return true;
    if (!impl.renderer.Render(context, view, impl.renderList, ResolveFluidQualityPolicy(view.quality).marchSteps,
                              bindSceneTarget)) {
        ++impl.stats.resourceFailures;
        return false;
    }
    impl.stats.renderedDomains = (std::max)(impl.stats.renderedDomains, static_cast<uint32_t>(impl.renderList.size()));
    return true;
}

bool FluidVolumePass::HasRenderableDomains() const noexcept { return !impl_->renderList.empty(); }

LIGHTING::CloudSolarVisibilityGpuData FluidVolumePass::PrepareSolarVisibility(const GFX::Context& context,
                                                                              const SceneEnvironment& environment) {
    Impl& impl = *impl_;
    std::array<INTERNAL::FluidRenderDomain, INTERNAL::kMaximumRenderedFluidDomains> casters{};
    size_t count = 0u;
    for (const INTERNAL::FluidRenderDomain& domain : impl.renderList)
        if (domain.settings.castShadows && count < casters.size())
            casters[count++] = domain;
    if (count == 0u)
        return {};
    const ATMOSPHERE::AtmosphereSolarState solar = ATMOSPHERE::BuildAtmosphereSolarState(environment);
    if (solar.sunRadiance <= 0.0f)
        return {};
    return impl.renderer.RenderSolarVisibility(context, {casters.data(), count}, solar.sunDirection);
}
FluidCameraVolumeView FluidVolumePass::GetCameraVolume() const noexcept { return impl_->renderer.GetCameraVolume(); }
FluidFlowView FluidVolumePass::GetFlowView() const noexcept { return impl_->flowView; }

void FluidVolumePass::ReleaseNamespace(uint64_t playbackNamespace) {
    impl_->renderList.clear();
    for (size_t index = impl_->domains.size(); index-- > 0u;)
        if (impl_->domains[index]->key.playbackNamespace == playbackNamespace)
            impl_->Retire(index);
}

void FluidVolumePass::Shutdown() {
    Impl& impl = *impl_;
    impl.renderList.clear();
    impl.renderer.Shutdown(impl.context);
    while (!impl.domains.empty())
        impl.Retire(impl.domains.size() - 1u);
    INTERNAL::RetireVolume(impl.context, impl.slice);
    impl.pipeline.Retire(impl.context);
    for (auto& upload : impl.uploads)
        upload.Finalize(&impl.context);
    impl.stats = {};
    impl.sliceView = {};
}

void FluidVolumePass::RequestDebugSlice(const FluidDebugSliceRequest& request) { impl_->sliceRequest = request; }
void FluidVolumePass::ClearDebugSlice() {
    impl_->sliceRequest.reset();
    impl_->sliceView = {};
}
FluidDebugSliceView FluidVolumePass::GetDebugSlice() const noexcept { return impl_->sliceView; }
const FluidVolumePassStats& FluidVolumePass::GetStats() const noexcept { return impl_->stats; }

void FluidVolumePass::TakeSinkReports(std::vector<FluidSinkReport>& out) {
    out.insert(out.end(), impl_->sinkReports.begin(), impl_->sinkReports.end());
    impl_->sinkReports.clear();
}

} // namespace HIKARI::RENDER3D::FLUID
