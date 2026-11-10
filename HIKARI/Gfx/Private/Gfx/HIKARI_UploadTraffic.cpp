#include "Gfx/HIKARI_UploadTraffic.h"

#include <atomic>
#include <iterator>
#include <string>

#include "Diagnostics/HIKARI_MetricRegistry.h"

namespace HIKARI::GFX::UPLOAD_TRAFFIC {

namespace {
constexpr const char* kMetricOwner = "Gfx.UploadTraffic";
constexpr const char* kModule = "Gfx";

// 所有者の表。指標 id の中の名前、種類、説明を 1 行にまとめる。Owner の順に並べる。
struct OwnerInfo {
    const char* id;
    Kind kind;
    const char* help;
};
constexpr OwnerInfo kOwners[] = {
    {"viewConstants", Kind::PerFrame, "Per-view camera, light, shadow and sky constants."},
    {"passConstants", Kind::PerFrame, "Per-pass constants (GI, clouds, volumetrics, reflections, shadows, culling)."},
    {"ui2D", Kind::PerFrame, "2D renderer constants and vertices."},
    {"transientGeometry", Kind::PerFrame, "Vertices built each frame: debug primitives, telegraphs, UI effects, text."},
    {"particles", Kind::PerFrame, "GPU particle emitter and spawn data."},
    {"fluids", Kind::PerFrame, "Fluid domain substep constants and source tables."},
    {"gpuScene", Kind::SceneData, "GPU scene instance records (full or dirty-range patches)."},
    {"massScene", Kind::SceneData, "Mass render proxy records (full or dirty-range patches)."},
    {"surfaceFeatures", Kind::SceneData, "Terrain and water surface records and their float pools."},
    {"worldSprites", Kind::SceneData, "World sprite instance records."},
    {"materials", Kind::SceneData, "Material data and material program blocks."},
    {"jointPalettes", Kind::SceneData, "Skinning joint matrices."},
    {"localLights", Kind::SceneData, "Local light and decal records with their cluster headers and indices."},
    {"rayTracingLights", Kind::SceneData, "Ray traced light table and its sampling CDF."},
    {"rayTracingInstances", Kind::SceneData, "Top-level acceleration structure instance descriptions."},
    {"rayTracingHitRecords", Kind::SceneData, "Ray hit records of the runtime ray traced scene."},
    {"drawCommands", Kind::SceneData, "Traditional indirect draw seeds and payloads."},
    {"geometry", Kind::SceneData, "Mesh and cluster geometry uploads."},
    {"textures", Kind::SceneData, "Texture uploads."},
    {"lightProbes", Kind::SceneData, "Baked light probe volume uploads."},
    {"colorGrading", Kind::SceneData, "Color grading LUT uploads."},
    {"water", Kind::SceneData, "Ocean spectrum initial data."},
    {"surfaceExtensions", Kind::PerFrame, "Module surfaces: per-view instance data and edited tile contents."},
};
static_assert(std::size(kOwners) == kOwnerCount, "every Owner needs a row");

std::array<std::atomic<uint64_t>, kOwnerCount> gPending{};
FrameBytes gLastFrame{};

struct Metrics {
    std::array<METRIC::MetricHandle, kOwnerCount> owners{};
    METRIC::MetricHandle sceneData = METRIC::kInvalidMetric;
    METRIC::MetricHandle total = METRIC::kInvalidMetric;
};

const Metrics& GetMetrics() {
    static const Metrics metrics = [] {
        Metrics created;
        for (size_t index = 0; index < kOwnerCount; ++index) {
            created.owners[index] =
                METRIC::Register({std::string("upload.") + kOwners[index].id + ".bytes", kMetricOwner, kModule, "bytes",
                                  METRIC::MetricCategory::Count, kOwners[index].help});
        }
        created.sceneData =
            METRIC::Register({"upload.sceneData.bytes", kMetricOwner, kModule, "bytes", METRIC::MetricCategory::Count,
                              "Scene data written to upload memory this frame (0 on a still scene)."});
        created.total =
            METRIC::Register({"upload.total.bytes", kMetricOwner, kModule, "bytes", METRIC::MetricCategory::Count,
                              "Everything written to upload memory this frame."});
        return created;
    }();
    return metrics;
}
} // namespace

void Record(Owner owner, uint64_t bytes) noexcept {
    if (owner < Owner::Count)
        gPending[static_cast<size_t>(owner)].fetch_add(bytes, std::memory_order_relaxed);
}

void Publish(uint64_t frameIndex) {
    const Metrics& metrics = GetMetrics();
    uint64_t sceneData = 0, total = 0;
    for (size_t index = 0; index < kOwnerCount; ++index) {
        const uint64_t bytes = gPending[index].exchange(0, std::memory_order_relaxed);
        gLastFrame[index] = bytes;
        total += bytes;
        if (kOwners[index].kind == Kind::SceneData)
            sceneData += bytes;
        METRIC::Publish(metrics.owners[index], static_cast<double>(bytes), frameIndex);
    }
    METRIC::Publish(metrics.sceneData, static_cast<double>(sceneData), frameIndex);
    METRIC::Publish(metrics.total, static_cast<double>(total), frameIndex);
}

const FrameBytes& LastFrame() noexcept { return gLastFrame; }

const char* Name(Owner owner) noexcept { return owner < Owner::Count ? kOwners[static_cast<size_t>(owner)].id : ""; }

Kind KindOf(Owner owner) noexcept {
    return owner < Owner::Count ? kOwners[static_cast<size_t>(owner)].kind : Kind::PerFrame;
}

} // namespace HIKARI::GFX::UPLOAD_TRAFFIC
