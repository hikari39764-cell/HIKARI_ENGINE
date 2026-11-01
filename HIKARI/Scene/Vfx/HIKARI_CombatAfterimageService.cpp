#include "Scene/Vfx/HIKARI_CombatAfterimageService.h"

#include <algorithm>
#include <array>
#include <cmath>
#include "Core/Math/HIKARI_MathValidation.h"
#include "RenderContracts/Model/HIKARI_ModelBounds.h"
#include "Scene/Vfx/HIKARI_AfterimageSnapshot.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_PresentationModelService.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"

namespace HIKARI::VFX {
namespace {
using namespace RENDER3D::RUNTIME;
constexpr size_t kSourceBudget = 8u;
constexpr size_t kPartBudget = 16u;
constexpr size_t kHistoryBudget = kVfxAfterimageSampleCountMax;

bool Valid(const CombatAfterimageRequest& r) {
    return r.ownerObject.IsValid() && r.sourceId != 0u && r.silhouetteCount >= kVfxAfterimageSampleCountMin &&
           r.silhouetteCount <= kHistoryBudget && MATH::IsFinite(r.color) && MATH::IsFinite(r.intensity) &&
           r.intensity >= 0 && MATH::IsFinite(r.opacity) && r.opacity >= 0 && MATH::IsFinite(r.uniformScale) &&
           r.uniformScale > 0 && MATH::IsFinite(r.lifetimeUnscaledSeconds) &&
           r.lifetimeUnscaledSeconds >= kVfxAfterimageLifetimeMin &&
           r.lifetimeUnscaledSeconds <= kVfxAfterimageLifetimeMax;
}

struct Sample {
    std::vector<AFTERIMAGE::Part> parts{};
    size_t partCount = 0;
    uint64_t order = 0;
    float age = 0;
    bool active = false;
};

void ReleaseSample(Sample& sample) {
    for (auto& part : sample.parts) {
        part.geometry.reset();
        part.materials.reset();
        part.desc.model = nullptr;
        part.desc.renderModel = nullptr;
        part.desc.materialOverrides.clear();
    }
    sample.partCount = 0;
}

struct Source {
    CombatAfterimageRequest request{};
    std::array<Sample, kHistoryBudget> samples{};
    Sample staging{};
    bool occupied = false;
    bool pending = false;
    bool paused = false;
    uint64_t lastPublishedFrame = 0;
    uint64_t order = 0;
};

void ClearSource(Source& source) {
    source.pending = false;
    source.paused = false;
    source.occupied = false;
    for (auto& sample : source.samples)
        sample.active = false;
}
} // namespace

struct CombatAfterimageService::State {
    std::array<Source, kSourceBudget> sources{};
    AFTERIMAGE::SnapshotCache assets{};
    std::vector<uint64_t> published{};
    std::vector<uint64_t> nextPublished{};
    uint64_t nextId = 0xB100000000000001ull;
    uint64_t tickFrame = 0;
    bool ticked = false;

    Source* Find(RuntimeObjectHandle owner, uint64_t id) {
        const auto found = std::find_if(sources.begin(), sources.end(), [&](const Source& value) {
            return value.occupied && value.request.ownerObject == owner && value.request.sourceId == id;
        });
        return found == sources.end() ? nullptr : &*found;
    }

    bool CapturePart(const SceneRenderObjectDesc& desc, Sample& sample) {
        if (!desc.visible)
            return true;
        if (sample.partCount == kPartBudget)
            return false;
        if (sample.parts.size() <= sample.partCount)
            sample.parts.emplace_back();
        if (!assets.Capture(desc, sample.parts[sample.partCount]))
            return false;
        ++sample.partCount;
        return true;
    }

    bool CaptureFinal(World& world, SceneRenderCache& scene, Source& source) {
        const auto* owner = world.FindObject(source.request.ownerObject);
        if (!owner)
            return false;
        const auto* model = owner->GetComponent<ModelComponent>();
        const auto* body = scene.Find({owner->GetRenderStableId()});
        if (!model || !body || !body->desc.visible || !body->desc.model || !body->desc.model->HasSkinnedMesh())
            return false;
        auto& sample = source.staging;
        sample.partCount = 0;
        if (!CapturePart(body->desc, sample))
            return false;
        if (const auto* models = world.Services().Find<PresentationModelService>()) {
            for (const auto& record : models->GetFrameRecords()) {
                if (record.ownerObjectHandle != source.request.ownerObject.ToValue() || !record.visible)
                    continue;
                const auto* clothing = scene.Find({record.renderObjectId});
                if (!clothing || !CapturePart(clothing->desc, sample))
                    return false;
            }
        }
        if (sample.partCount == 0)
            return false;
        if (source.request.uniformScale != 1.0f) {
            const auto position = body->desc.worldTransform.GetWorldMatrix().TransformPoint({0, 0, 0, 1});
            const MATH::Vec3 origin{position.x, position.y, position.z};
            const float scale = source.request.uniformScale;
            const auto adjustment =
                MATH::Mat4::TRS(origin, MATH::Quat::Identity(), {scale, scale, scale}) *
                MATH::Mat4::TRS({-origin.x, -origin.y, -origin.z}, MATH::Quat::Identity(), {1, 1, 1});
            for (size_t i = 0; i < sample.partCount; ++i) {
                auto& desc = sample.parts[i].desc;
                const auto matrix = adjustment * desc.worldTransform.GetWorldMatrix();
                desc.worldTransform.useExplicitMatrix = true;
                desc.worldTransform.explicitMatrix = matrix;
                desc.worldBounds = BOUNDS::TransformBounds(desc.localBounds, matrix);
            }
        }
        auto found =
            std::find_if(source.samples.begin(), source.samples.end(), [](const Sample& item) { return !item.active; });
        if (found == source.samples.end())
            found = std::min_element(source.samples.begin(), source.samples.end(),
                                     [](const auto& a, const auto& b) { return a.order < b.order; });
        std::swap(*found, sample);
        sample.active = false;
        found->age = 0;
        found->order = ++source.order;
        found->active = true;
        size_t count = static_cast<size_t>(
            std::count_if(source.samples.begin(), source.samples.end(), [](const auto& item) { return item.active; }));
        while (count > source.request.silhouetteCount) {
            auto oldest = source.samples.end();
            for (auto it = source.samples.begin(); it != source.samples.end(); ++it)
                if (it->active && (oldest == source.samples.end() || it->order < oldest->order))
                    oldest = it;
            oldest->active = false;
            --count;
        }
        return true;
    }
};

CombatAfterimageService::CombatAfterimageService() : state_(std::make_unique<State>()) {}
CombatAfterimageService::~CombatAfterimageService() = default;

bool CombatAfterimageService::Capture(World& world, const CombatAfterimageRequest& request) noexcept {
    if (!world.IsOwnerThread() || !Valid(request) || !world.FindObject(request.ownerObject))
        return false;
    auto* source = state_->Find(request.ownerObject, request.sourceId);
    if (source) {
        if (source->paused ||
            (source->request.frameIndex == request.frameIndex && source->request.sequence == request.sequence &&
             source->request.randomSeed == request.randomSeed))
            return false;
    } else {
        const auto free = std::find_if(state_->sources.begin(), state_->sources.end(),
                                       [](const auto& value) { return !value.occupied; });
        if (free == state_->sources.end())
            return false;
        source = &*free;
        source->occupied = true;
        source->lastPublishedFrame = 0;
    }
    source->request = request;
    source->pending = true;
    return true;
}

bool CombatAfterimageService::Capture(World& world, const CombatAfterimageRequest& request,
                                      const VfxSpecialOutputData& output) noexcept {
    if (!output.enabled || output.type != VfxSpecialOutputType::SkinnedAfterimage)
        return false;
    auto configured = request;
    const auto& profile = output.skinnedAfterimage;
    configured.silhouetteCount = profile.sampleCount;
    configured.lifetimeUnscaledSeconds = profile.lifetimeSeconds;
    configured.opacity = profile.opacity * request.opacity * profile.color.w;
    configured.intensity = profile.intensity * request.intensity;
    configured.color = {profile.color.x, profile.color.y, profile.color.z};
    return Capture(world, configured);
}

bool CombatAfterimageService::SetPaused(RuntimeObjectHandle owner, uint64_t sourceId, bool paused) noexcept {
    auto* source = state_->Find(owner, sourceId);
    if (!source)
        return false;
    source->paused = paused;
    if (paused)
        source->pending = false;
    return true;
}

bool CombatAfterimageService::Clear(RuntimeObjectHandle owner, uint64_t sourceId) noexcept {
    if (!owner.IsValid() || sourceId == 0)
        return false;
    if (auto* source = state_->Find(owner, sourceId))
        ClearSource(*source);
    return true;
}

void CombatAfterimageService::Tick(uint64_t frameIndex, float dt) noexcept {
    if (state_->ticked && frameIndex <= state_->tickFrame)
        return;
    state_->ticked = true;
    state_->tickFrame = frameIndex;
    dt = MATH::IsFinite(dt) ? std::clamp(dt, 0.0f, 0.25f) : 0;
    for (auto& source : state_->sources) {
        if (!source.occupied || source.paused)
            continue;
        for (auto& sample : source.samples) {
            if (sample.active) {
                sample.age += dt;
                sample.active = sample.age < source.request.lifetimeUnscaledSeconds;
            }
        }
    }
}

void CombatAfterimageService::Publish(World& world, SceneRenderCache& scene, uint64_t frameIndex) noexcept {
    try {
        state_->nextPublished.clear();
        for (auto& source : state_->sources) {
            if (!source.occupied)
                continue;
            if (!world.FindObject(source.request.ownerObject)) {
                ClearSource(source);
                continue;
            }
            if (source.pending && !source.paused && source.lastPublishedFrame != frameIndex) {
                (void)state_->CaptureFinal(world, scene, source);
                source.lastPublishedFrame = frameIndex;
                source.pending = false;
            }
            bool active = false;
            for (auto& sample : source.samples) {
                if (!sample.active)
                    continue;
                active = true;
                const float remaining =
                    std::clamp(1.0f - sample.age / source.request.lifetimeUnscaledSeconds, 0.0f, 1.0f);
                const float alpha = std::clamp(source.request.opacity * remaining * remaining, 0.0f, 1.0f);
                const auto color = source.request.color * source.request.intensity;
                for (size_t i = 0; i < sample.partCount; ++i) {
                    auto& part = sample.parts[i];
                    auto& id = part.desc.id.value;
                    if (id < 0xB100000000000000ull ||
                        (scene.Find({id}) && std::find(state_->published.begin(), state_->published.end(), id) ==
                                                 state_->published.end())) {
                        do {
                            id = state_->nextId++;
                        } while (id == 0 || scene.Find({id}));
                    }
                    AFTERIMAGE::ApplyStyle(part, color, alpha);
                    scene.Upsert(part.desc);
                    state_->nextPublished.push_back(id);
                }
            }
            if (!active && !source.pending && !source.paused)
                ClearSource(source);
        }
        for (const auto id : state_->published)
            if (std::find(state_->nextPublished.begin(), state_->nextPublished.end(), id) ==
                state_->nextPublished.end())
                scene.Remove({id});
        state_->published.swap(state_->nextPublished);
        scene.PreRenderSync();
        // Remove render references before releasing leases; preserve vector and
        // pose capacities for the next capture in each bounded ring slot.
        for (auto& source : state_->sources) {
            for (auto& sample : source.samples)
                if (!sample.active)
                    ReleaseSample(sample);
            ReleaseSample(source.staging);
        }
    } catch (...) {
        for (const auto id : state_->published)
            scene.Remove({id});
        for (const auto id : state_->nextPublished)
            scene.Remove({id});
        state_->published.clear();
        state_->nextPublished.clear();
    }
}

void CombatAfterimageService::Reset() noexcept {
    for (auto& source : state_->sources) {
        ClearSource(source);
        source = {};
    }
    state_->assets.Clear();
    state_->ticked = false;
    // Keep removal identities until publication, including scene reset.
}

size_t CombatAfterimageService::GetActiveCount() const noexcept {
    return static_cast<size_t>(std::count_if(state_->sources.begin(), state_->sources.end(),
                                             [](const auto& value) { return value.occupied; }));
}

size_t CombatAfterimageService::GetSampleCount(RuntimeObjectHandle owner, uint64_t sourceId) const noexcept {
    const auto* source = state_->Find(owner, sourceId);
    return source ? static_cast<size_t>(std::count_if(source->samples.begin(), source->samples.end(),
                                                      [](const auto& value) { return value.active; }))
                  : 0u;
}
} // namespace HIKARI::VFX
