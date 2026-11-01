#include "Render3D/Shadow/HIKARI_LocalLightShadowRenderer.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "Core/Hashing/HIKARI_FnvHash.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Core/MeshRenderer/Data/HIKARI_MeshDrawDataBuilder.h"
#include "Render3D/Shadow/Internal/HIKARI_ShadowRendererInternal.h"
#include "Render3D/WorldSprite/HIKARI_WorldSpritePass.h"

namespace HIKARI::SHADOW {
namespace {
using namespace RENDER3D::LIGHTING;
using namespace INTERNAL;
// 図集の一辺の枚数は面の上限が収まる最小の正方形（8 面 3、16 面 4、32 面 6）。
uint32_t AtlasGrid(uint32_t faces) {
    uint32_t grid = 1u;
    while (grid * grid < faces) {
        ++grid;
    }
    return grid;
}

struct CachedLight {
    std::string id;
    uint32_t sourceIndex = 0;
    uint32_t firstFace = 0;
    uint32_t faceCount = 0;
    uint64_t content = 0;
    bool valid = false;
};
struct State {
    ShadowRendererState renderer;
    std::vector<CachedLight> cache;
    LocalLightShadowFrame frame;
    LocalLightShadowStats stats;
    const SceneLightingSet* lighting = nullptr;
    MATH::Mat4 cameraViewProj{};
    uint64_t frameFence = 0;
    uint64_t depthBytes = 0;
    uint32_t atlasGrid = 0;
};
State& GetState() {
    static State state;
    return state;
}
struct Candidate {
    uint32_t sourceIndex = 0;
    uint32_t faces = 0;
    int priority = 0;
    double importance = 0;
};
struct Job {
    CachedLight cache;
    LocalLightShadowFrameSet views;
    const Candidate* candidate = nullptr;
    float fade = 1.0f;
    bool render = true;
    bool complete = true;
};
// 名額の境目の灯の影の濃さ。同じ優先度で外れた灯のうち、この灯の場所に入れるもの（面の数が同じか少ない）の
// 重要度を基準に、それと同じなら 0、2 倍以上で 1。重要度は相机の動きに連れて連続に変わり、入れ替わるのは
// 入る灯と出る灯の重要度が等しい瞬間なので、その時どちらの影も 0 で、名額が動いても影は跳ばない。
float ShadowFade(const Candidate& candidate, const std::vector<const Candidate*>& rejected) {
    double threshold = 0.0;
    for (const Candidate* other : rejected)
        if (other->priority == candidate.priority && other->faces <= candidate.faces)
            threshold = (std::max)(threshold, other->importance);
    return threshold > 0.0 ? float(std::clamp(candidate.importance / threshold - 1.0, 0.0, 1.0)) : 1.0f;
}
bool SameLight(const CachedLight& cached, uint32_t sourceIndex, const SceneLightProvider& light) {
    return cached.valid && cached.sourceIndex == sourceIndex && cached.id == light.id;
}
bool HasRelevantDynamicCasters(const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& source,
                               const SceneLightProvider& light) {
    if (source.HasMassInstances())
        return true;
    const auto& pass = source.GetPass(RENDER3D::GPUDRIVEN::GpuDrivenPassKind::Shadow);
    const auto relevant = [&](const auto* instances, uint32_t count) {
        if (!instances || instances->size() < count)
            return count != 0;
        return std::any_of(instances->begin(), instances->end(), [&](const auto& instance) {
            return IsPrimaryShadowCaster(instance) && MayAffectLocalShadow(instance, light.position, light.range);
        });
    };
    return relevant(pass.instances, pass.gpuSceneInstanceCount) ||
           relevant(pass.traditionalIndirect.instances, pass.traditionalIndirect.gpuSceneInstanceCount);
}
} // namespace

LocalShadowBudget ResolveLocalShadowBudget(RENDER3D::LocalShadowQuality quality) {
    switch (quality) {
    case RENDER3D::LocalShadowQuality::Off: return {256, 0};
    case RENDER3D::LocalShadowQuality::Low: return {256, 8};
    // 静止した灯は前の面を使い回すので、面の数を増やしても動かない場面の負担は初回だけ。
    case RENDER3D::LocalShadowQuality::High: return {768, 32};
    default: return {512, 16};
    }
}

void ResetLocalLightShadows() {
    auto& state = GetState();
    state.frame = {};
    state.cache.clear();
    state.lighting = nullptr;
    state.frameFence = 0;
    InvalidateShadowSourceCache(state.renderer);
}

const LocalLightShadowStats& GetLocalLightShadowStats() { return GetState().stats; }

const LocalLightShadowFrame* GetLocalLightShadowFrame(const GFX::Context& graphics, const SceneLightingSet& lighting,
                                                      const Camera3D& camera) {
    const auto& state = GetState();
    const auto matrix = camera.GetViewProj();
    return state.frame.assignmentCount && state.lighting == &lighting &&
                   state.frameFence == graphics.currentFrameRetireFenceValue &&
                   std::memcmp(&matrix, &state.cameraViewProj, sizeof(matrix)) == 0
               ? &state.frame
               : nullptr;
}

void RenderLocalLightShadows(const GFX::Context& graphics, const SceneLightingSet& lighting, const Camera3D& camera,
                             const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource& scene, LocalShadowBudget budget,
                             RENDER3D::WORLD_SPRITES::WorldSpritePass* sprites) {
    auto& state = GetState();
    auto& renderer = state.renderer;
    state.frame = {};
    state.stats = {};
    state.stats.allocatedDepthBytes = state.depthBytes;
    state.lighting = &lighting;
    state.cameraViewProj = camera.GetViewProj();
    state.frameFence = graphics.currentFrameRetireFenceValue;
    if (!graphics.device || !graphics.cmdList || !budget.maximumFaces)
        return;
    budget.maximumFaces = (std::min)(budget.maximumFaces, kMaximumLocalShadowFaceCount);
    budget.tileResolution = std::clamp(budget.tileResolution, 64u, 1024u);
    state.stats.budgetFaces = budget.maximumFaces;

    std::array<uint32_t, kMaximumLocalLightCount> sourceIndices{};
    std::array<float, kMaximumLocalLightCount> screenCoverage{};
    const auto selection =
        SelectVisibleLocalLights(lighting, camera, uint32_t((std::max)(1, graphics.backBufferWidth)),
                                 uint32_t((std::max)(1, graphics.backBufferHeight)), sourceIndices, screenCoverage);
    std::vector<Candidate> candidates;
    candidates.reserve(selection.lightCount);
    for (uint32_t index = 0; index < selection.lightCount; ++index) {
        const uint32_t sourceIndex = sourceIndices[index];
        const auto& light = lighting.lights[sourceIndex];
        if (!light.castsShadows)
            continue;
        const auto delta = light.position - camera.GetPosition();
        const double distanceSquared =
            double(delta.x) * delta.x + double(delta.y) * delta.y + double(delta.z) * delta.z;
        // 画面に掛かる割合 × 近さ × 強さ。どれも相机に連れて連続に変わり、近い灯どうしでも同点にならない
        // （近さを 1 で頭打ちにすると影響範囲内の灯が皆同点になり、作った順で名額が決まって跳ぶ）。
        const double importance = double(screenCoverage[index]) * double(light.range) * light.range /
                                  (distanceSquared + 1.0) * light.intensity;
        candidates.push_back({sourceIndex, light.type == SceneLightType::Spot ? 1u : 6u, light.priority, importance});
    }
    state.stats.requestedLights = uint32_t(candidates.size());
    if (candidates.empty())
        return;
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.priority != b.priority)
            return a.priority > b.priority;
        if (a.importance != b.importance)
            return a.importance > b.importance;
        return a.sourceIndex < b.sourceIndex;
    });
    std::vector<Job> jobs;
    jobs.reserve(kMaximumLocalShadowFaceCount);
    std::vector<const Candidate*> rejected;
    uint32_t nextFace = 0;
    for (const auto& candidate : candidates) {
        if (candidate.faces > budget.maximumFaces - nextFace) {
            rejected.push_back(&candidate);
            continue;
        }
        const auto& light = lighting.lights[candidate.sourceIndex];
        auto frames = BuildLocalLightShadowFrameSet(light, budget.tileResolution);
        if (frames.faceCount != candidate.faces) {
            ++state.stats.failedLights;
            continue;
        }
        Job job;
        job.cache = {light.id, candidate.sourceIndex, nextFace, candidate.faces, 0, false};
        job.views = frames;
        job.candidate = &candidate;
        jobs.push_back(std::move(job));
        nextFace += candidate.faces;
    }
    // 影が 0 になる灯は描かない（名額の判定には数えたまま）。
    for (auto& job : jobs)
        job.fade = ShadowFade(*job.candidate, rejected);
    std::erase_if(jobs, [&](const Job& job) {
        if (job.fade * 255.0f >= 0.5f)
            return false;
        rejected.push_back(job.candidate);
        return true;
    });
    for (const Candidate* candidate : rejected) {
        ++state.stats.budgetRejectedLights;
        state.stats.budgetRejectedLightIds.push_back(lighting.lights[candidate->sourceIndex].id);
    }
    // 前の帧にも影を持っていた灯は同じ面を使い続ける（順位が入れ替わっただけで描き直さない）。残りは空いた面へ
    // 先頭から詰め、続きの面が空いていなければ全部を順に並べ直す（面の合計は上で上限内に収めてある）。
    std::array<bool, kMaximumLocalShadowFaceCount> usedFaces{};
    const auto isFree = [&](uint32_t first, uint32_t count) {
        return first + count <= budget.maximumFaces &&
               std::none_of(usedFaces.begin() + first, usedFaces.begin() + first + count,
                            [](bool used) { return used; });
    };
    for (auto& job : jobs) {
        const auto old = std::find_if(state.cache.begin(), state.cache.end(), [&](const auto& entry) {
            return SameLight(entry, job.cache.sourceIndex, lighting.lights[job.cache.sourceIndex]) &&
                   entry.faceCount == job.cache.faceCount;
        });
        job.cache.firstFace =
            old != state.cache.end() && isFree(old->firstFace, old->faceCount) ? old->firstFace : UINT32_MAX;
        if (job.cache.firstFace != UINT32_MAX)
            std::fill_n(usedFaces.begin() + job.cache.firstFace, job.cache.faceCount, true);
    }
    bool packed = true;
    for (auto& job : jobs) {
        if (job.cache.firstFace != UINT32_MAX)
            continue;
        uint32_t first = 0;
        while (first < budget.maximumFaces && !isFree(first, job.cache.faceCount))
            ++first;
        if (first >= budget.maximumFaces) {
            packed = false;
            break;
        }
        job.cache.firstFace = first;
        std::fill_n(usedFaces.begin() + first, job.cache.faceCount, true);
    }
    if (!packed) {
        uint32_t face = 0;
        for (auto& job : jobs) {
            job.cache.firstFace = face;
            face += job.cache.faceCount;
        }
    }
    state.stats.selectedLights = uint32_t(jobs.size());
    if (jobs.empty())
        return;

    renderer.gpuDrivenSceneSource = &scene;
    ClearShadowFrameSubmissions(renderer);
    renderer.worldSprites = sprites;
    const bool hasSprites = sprites && sprites->HasShadowWork();
    if (!BuildShadowGpuDrivenSceneSources(renderer) && !hasSprites)
        return;
    const auto failAll = [&] {
        state.stats.failedLights = uint32_t(jobs.size());
        state.cache.clear();
    };
    if (!EnsureShadowRendererInitialized(renderer, graphics)) {
        failAll();
        return;
    }
    const uint32_t atlasGrid = AtlasGrid(budget.maximumFaces);
    if (!renderer.shadowMap || renderer.resolution != budget.tileResolution || state.atlasGrid != atlasGrid) {
        state.cache.clear();
        state.atlasGrid = atlasGrid;
        state.depthBytes = 0;
        state.stats.allocatedDepthBytes = 0;
        if (!CreateShadowMapResources(renderer, graphics,
                                      {budget.tileResolution, atlasGrid, false, "shadow/local_atlas"})) {
            failAll();
            return;
        }
        const auto description = renderer.shadowMap->GetDesc();
        state.depthBytes = graphics.device->GetResourceAllocationInfo(0, 1, &description).SizeInBytes;
    }
    state.stats.allocatedDepthBytes = state.depthBytes;
    ActivateShadowFrameResources(renderer, graphics.frameIndex);
    MESHRENDERER::FillCameraCB(state.cameraViewProj, MATH::Inverse(state.cameraViewProj), camera.GetPosition(),
                               uint32_t((std::max)(1, graphics.backBufferWidth)),
                               uint32_t((std::max)(1, graphics.backBufferHeight)), renderer.materialCamera);
    if (!UploadShadowMaterialCamera(renderer, graphics)) {
        failAll();
        return;
    }

    bool needsRender = false;
    for (auto& job : jobs) {
        const bool dynamicWork =
            renderer.frameHasDynamicShadowWork &&
            HasRelevantDynamicCasters(renderer.dynamicShadowSceneSource, lighting.lights[job.cache.sourceIndex]);
        uint64_t content =
            AppendShadowHashValue(HASH::kFnv1a64OffsetBasis, renderer.staticShadowSceneSource.sourceVersion);
        content = AppendShadowHashValue(content, renderer.staticShadowSceneSource.layoutVersion);
        content = AppendShadowHashValue(content, dynamicWork ? 1u : 0u);
        content = AppendShadowHashValue(content, hasSprites ? 1u : 0u);
        content = AppendShadowHashValue(content, job.cache.firstFace);
        for (uint32_t face = 0; face < job.views.faceCount; ++face)
            content = AppendShadowHashBytes(content, &job.views.faces[face].viewProj, sizeof(MATH::Mat4));
        job.cache.content = content;
        const auto old = std::find_if(state.cache.begin(), state.cache.end(), [&](const auto& entry) {
            return SameLight(entry, job.cache.sourceIndex, lighting.lights[job.cache.sourceIndex]) &&
                   entry.content == content && entry.faceCount == job.cache.faceCount;
        });
        // 選択時に全動的面を予算へ算入し、部分的な六面や古い動的 caster を公開しない。
        job.render = dynamicWork || hasSprites || old == state.cache.end();
        needsRender |= job.render;
    }
    const auto renderSource = [&](const auto& source, bool clearDepth) {
        if (!PrepareShadowSourceForDraw(renderer, graphics, source)) {
            for (auto& job : jobs)
                if (job.render)
                    job.complete = false;
            return;
        }
        for (auto& job : jobs)
            if (job.render && job.complete) {
                for (uint32_t face = 0; face < job.views.faceCount; ++face) {
                    const uint32_t tile = job.cache.firstFace + face;
                    const LONG left = LONG(tile % atlasGrid * budget.tileResolution),
                               top = LONG(tile / atlasGrid * budget.tileResolution);
                    if (!RecordShadowView(
                            renderer, graphics, job.views.faces[face],
                            {left, top, left + LONG(budget.tileResolution), top + LONG(budget.tileResolution)},
                            clearDepth,
                            &source == &renderer.dynamicShadowSceneSource
                                ? GFX::GPU_PROFILE::Pass::TraditionalDrawShadowDynamic
                                : GFX::GPU_PROFILE::Pass::TraditionalDrawShadowStatic,
                            &source == &renderer.dynamicShadowSceneSource
                                ? GFX::GPU_PROFILE::Pass::MeshletDrawShadowDynamic
                                : GFX::GPU_PROFILE::Pass::MeshletDrawShadowStatic)) {
                        job.complete = false;
                        break;
                    }
                }
            }
    };
    if (needsRender) {
        if ((renderer.frameHasStaticShadowWork || renderer.frameHasDynamicShadowWork) &&
            !PrepareShadowDeformation(renderer, graphics)) {
            failAll();
            return;
        }
        if (renderer.frameHasStaticShadowWork)
            renderSource(renderer.staticShadowSceneSource, true);
        if (renderer.frameHasDynamicShadowWork)
            renderSource(renderer.dynamicShadowSceneSource, !renderer.frameHasStaticShadowWork);
        if (hasSprites)
            for (auto& job : jobs)
                if (job.render && job.complete) {
                    for (uint32_t face = 0; face < job.views.faceCount; ++face) {
                        const uint32_t tile = job.cache.firstFace + face;
                        const LONG size = LONG(budget.tileResolution);
                        const LONG x = LONG(tile % atlasGrid) * size, y = LONG(tile / atlasGrid) * size;
                        if (!RecordWorldSpriteShadowView(
                                renderer, graphics, job.views.faces[face], {x, y, x + size, y + size},
                                !renderer.frameHasStaticShadowWork && !renderer.frameHasDynamicShadowWork)) {
                            job.complete = false;
                            break;
                        }
                    }
                }
        FinishFinalShadowMap(renderer, graphics, graphics.cmdList);
    }
    state.cache.clear();
    state.frame.atlas = renderer.shadowSrvResource;
    state.frame.data.tileResolution = budget.tileResolution;
    state.frame.data.tilesPerRow = atlasGrid;
    state.frame.data.textureDescriptorIndex =
        RENDER3D::GetTextureResourceSrvDescriptorIndexFromBackendHandle(renderer.shadowSrvHandle);
    for (auto& job : jobs) {
        if (!job.complete) {
            ++state.stats.failedLights;
            continue;
        }
        job.cache.valid = true;
        state.cache.push_back(job.cache);
        state.stats.usedFaces += job.cache.faceCount;
        state.stats.shadowedLightIds.push_back(lighting.lights[job.cache.sourceIndex].id);
        state.frame.assignments[state.frame.assignmentCount++] = {job.cache.sourceIndex, job.cache.firstFace, job.fade};
        state.frame.data.faceCount = (std::max)(state.frame.data.faceCount, job.cache.firstFace + job.cache.faceCount);
        for (uint32_t face = 0; face < job.views.faceCount; ++face)
            state.frame.data.viewProj[job.cache.firstFace + face] = job.views.faces[face].viewProj;
        if (job.render)
            state.stats.renderedFaces += job.views.faceCount;
        else
            state.stats.reusedFaces += job.views.faceCount;
    }
}

} // namespace HIKARI::SHADOW
