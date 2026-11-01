#include "HIKARI/Project/FrameContext.h"
#include "Physics/HIKARI_PhysicsSceneBridge.h"
#include "Physics/HIKARI_PhysicsRuntimeStatusService.h"
#include "Scene/Components/HIKARI_ColliderComponent.h"
#include "Scene/Components/HIKARI_PhysicsBodyComponent.h"
#include "Scene/HIKARI_RuntimeWorldServices.h"
#include "Scene/Terrain/HIKARI_TerrainInteractionSourceSystem.h"
#include "Scene/HIKARI_World.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Editor/Authoring/HIKARI_ComponentPropertyBuilder.h"
#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Scene/Navigation/HIKARI_NavigationWorldService.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <DirectXPackedVector.h>
#include <json.hpp>

#include "RenderContracts/Model/HIKARI_ModelAssetTypes.h"
#include "Core/Math/HIKARI_NormalMatrix.h"
#include "Editor/History/HIKARI_TerrainSurfaceEditDelta.h"
#include "Render3D/Procedural/HIKARI_ProceduralMeshBuilder.h"
#include "Render3D/Runtime/HIKARI_SceneSurfaceFloatPool.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceQueryService.h"
#include "Scene/Terrain/HIKARI_TerrainInteraction.h"
#include "Scene/Terrain/HIKARI_SurfacePreset.h"
#include "Scene/Terrain/HIKARI_TerrainSurface.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceComponent.h"
#include "Scene/Terrain/HIKARI_TerrainProcedural.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceResponse.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceSerialization.h"
#include "Scene/Terrain/HIKARI_TerrainChunkSimulation.h"
#include "Scene/Terrain/HIKARI_TerrainSurfaceBake.h"

namespace {

bool Near(float lhs, float rhs, float epsilon = 1.0e-4f) { return std::fabs(lhs - rhs) <= epsilon; }

int Fail(const char* message) {
    std::cerr << "Terrain surface contract failed: " << message << '\n';
    return 1;
}

} // namespace

// 地形変更の公開境界を検証する。query は旧結果を読み、失敗・古い worker は公開しない。
static int NavigationPublication() {
    using namespace HIKARI;
    using namespace NAVIGATION;
    ComponentRegistry components;
    ComponentTypeInfo type;
    type.typeName = "TerrainSurfaceComponent";
    type.factory = [] { return std::make_unique<TerrainSurfaceComponent>(); };
    if (!components.Register(std::move(type)))
        return Fail("navigation component registration");
    World world;
    if (!world.BindComponentRegistry(&components))
        return Fail("navigation world registry");
    auto* object = world.CreateObject("terrain");
    auto* terrain = object->AddComponent<TerrainSurfaceComponent>();
    TERRAIN::TerrainSurface surface;
    surface.chunkCells = 32;
    surface.chunkCellSize = 1.0f;
    surface.chunks[{0, 0}] = TERRAIN::MakeTerrainChunk(32, 1);
    terrain->SetSurface(surface);
    NavigationWorldService nav;
    if (!nav.Synchronize(world) || nav.GetDiagnostics().fullBuildCount != 1)
        return Fail("navigation full build");
    MATH::Vec3 projected, points[8];
    uint8_t flags[8]{};
    const MATH::Vec3 extents{1, 3, 1};
    if (!nav.ProjectPoint(world, {6, 1, 6}, extents, projected))
        return Fail("navigation projection");
    const auto initial = nav.GetSourceRevision();
    if (!nav.Synchronize(world) || nav.GetDiagnostics().unchangedStampFastPaths != 1)
        return Fail("navigation unchanged fast path");
    const auto path = nav.FindPath(world, {6, 1, 6}, {24, 1, 24}, extents, points, flags, 8);
    if (path.status != QueryStatus::Success || path.pointCount < 2 || path.pathLength < 20)
        return Fail("navigation path");
    if (nav.FindPath(world, {6, 1, 6}, {24, 1, 24}, extents, points, flags, 1).status != QueryStatus::BufferTooSmall)
        return Fail("navigation bounded path output");
    float distance = 0;
    if (!nav.Raycast(world, {6, 1, 6}, {24, 1, 24}, extents, projected, distance) || distance < .99f)
        return Fail("navigation raycast");
    for (auto& height : surface.chunks.begin()->second.heights)
        height = .25f;
    terrain->SetSurface(surface);
    if (!nav.Synchronize(world) || nav.GetSourceRevision() == initial ||
        nav.GetDiagnostics().incrementalBuildCount != 1)
        return Fail("navigation incremental publication");
    if (!nav.ProjectPoint(world, {6, 1, 6}, extents, projected) || projected.y < .2f)
        return Fail("navigation changed height");
    for (auto& height : surface.chunks.begin()->second.heights)
        height = .5f;
    terrain->SetSurface(surface);
    const auto liveRevision = nav.GetSourceRevision();
    nav.Synchronize(world, SynchronizationMode::Background);
    if (!nav.GetDiagnostics().buildPending || nav.GetSourceRevision() != liveRevision)
        return Fail("navigation pending keeps live result");
    for (auto& height : surface.chunks.begin()->second.heights)
        height = .75f;
    terrain->SetSurface(surface);
    if (!nav.Synchronize(world) || nav.GetDiagnostics().discardedBuildCount != 1 || nav.GetDiagnostics().buildPending)
        return Fail("navigation rejects stale worker");
    if (!nav.ProjectPoint(world, {6, 1, 6}, extents, projected) || projected.y < .7f)
        return Fail("navigation latest source wins");
    for (auto& height : surface.chunks.begin()->second.heights)
        height = 1.0f;
    terrain->SetSurface(surface);
    nav.Synchronize(world, SynchronizationMode::Background);
    nav.Clear();
    if (nav.GetDiagnostics().buildPending || nav.ProjectPoint(world, {6, 1, 6}, extents, projected) ||
        !nav.Synchronize(world))
        return Fail("navigation clear joins worker and rebuilds");
    const auto goodRevision = nav.GetSourceRevision();
    object->SetLocalScale({1.0e10f, 1, 1.0e10f});
    if (!nav.Synchronize(world) || nav.GetSourceRevision() != goodRevision ||
        nav.GetDiagnostics().lastKnownGoodRetentionCount != 1)
        return Fail("navigation oversized grid retains last good result");
    if (!nav.ProjectPoint(world, {6, 1, 6}, extents, projected))
        return Fail("navigation retained query");
    world.DestroyObject(object);
    if (nav.Synchronize(world) || nav.GetSourceRevision() != 0 ||
        nav.ProjectPoint(world, {6, 1, 6}, extents, projected))
        return Fail("navigation removed terrain clears result");
    nav.Clear();
    return 0;
}

static int ColliderFootprintPublication() {
    using namespace HIKARI;
    using namespace PHYSICS;
    for (auto shapeType : {PhysicsShapeType::Sphere, PhysicsShapeType::Capsule, PhysicsShapeType::Box}) {
        World world;
        bool playing = false;
        RuntimePlayStateService play{&playing};
        PhysicsRuntimeStatusService status;
        world.Services().Register(play);
        world.Services().Register(status);
        auto* terrainObject = world.CreateObject("terrain");
        auto* terrain = terrainObject->AddComponent<TerrainSurfaceComponent>();
        TERRAIN::TerrainSurface surface;
        surface.chunkCells = 32;
        surface.chunkCellSize = 1.0f;
        surface.chunks[{0, 0}] = TERRAIN::MakeTerrainChunk(32, 1);
        terrain->SetSurface(surface);
        auto* object = world.CreateObject("collider");
        object->AddComponent<PhysicsBodyComponent>()->SetMotionType(PhysicsMotionType::Dynamic);
        auto* collider = object->AddComponent<ColliderComponent>();
        collider->Deserialize({{"center", {0.1f, 0.0f, 0.2f}},
                               {"rotation", {15, 20, 10}},
                               {"size", {0.8f, 1.2f, 1.0f}},
                               {"radius", 0.4f},
                               {"height", 1.5f}});
        collider->SetShapeType(shapeType);
        const MATH::Vec3 scale{-2.0f, 1.5f, 0.75f};
        object->SetLocalScale(scale);
        object->SetLocalRotation(MATH::Quat::FromEulerXYZ(0.2f, 0.6f, 0.1f));
        TerrainInteractionSourceSystem producer;
        FrameContext frame;
        frame.unscaledDt = 1.0f / 60.0f;
        const auto observe = [&]() {
            producer.OnWorldAttached(world);
            object->SetLocalPosition({4, 0.1f, 4});
            world.FrameEvents().BeginScope(1);
            producer.Update(world, frame);
            object->SetLocalPosition({4.1f, 0.1f, 4});
            world.FrameEvents().BeginScope(2);
            producer.Update(world, frame);
            const auto events = world.FrameEvents().Read<TERRAIN::TerrainInteractionSample>();
            return events.empty() ? TERRAIN::TerrainInteractionSample{} : events.back();
        };
        const auto authored = observe();
        status.SetBodyDebugShapes(object->GetRuntimeHandle(), {BuildPhysicsShapeDesc(*collider, scale, 1)});
        // Runtime shape dimensions are authoritative even when authoring data changes.
        collider->Deserialize({{"radius", 0.9f}, {"height", 3.0f}, {"size", {3, 3, 3}}});
        const auto runtime = observe();
        const auto& a = authored.footprint;
        const auto& b = runtime.footprint;
        if (!authored.targetSurface.IsValid() || !runtime.targetSurface.IsValid() || a.kind != b.kind ||
            !Near(a.halfExtentsWS.x, b.halfExtentsWS.x) || !Near(a.halfExtentsWS.y, b.halfExtentsWS.y) ||
            !Near(a.verticalRadiusWS, b.verticalRadiusWS) || MATH::Length(a.tangentWS - b.tangentWS) > 1.0e-4f ||
            MATH::Length(authored.positionWS - runtime.positionWS) > 1.0e-4f) {
            return Fail("authored/runtime collider footprint publication differs");
        }
        producer.OnWorldDetached(world);
    }
    return 0;
}

// 地形 field の CPU 焼き込みと float pool の公開を検証する。GPU は焼き込み済みの
// 高さ・勾配と詰めた layer weight を読み、値だけの変更は同じ offset の行範囲として届く。
static int SurfaceFieldPublication() {
    using namespace HIKARI;
    namespace TR = RENDER3D::TERRAIN;
    namespace RR = RENDER3D::RUNTIME;
    constexpr uint32_t kGrid = 4u;
    constexpr uint32_t kStride = kGrid + 1u;
    constexpr std::size_t kCells = static_cast<std::size_t>(kStride) * kStride;
    const auto at = [](uint32_t x, uint32_t z) { return static_cast<std::size_t>(z) * kStride + x; };

    TR::TerrainSurfaceRenderData data{};
    data.gridResolution = kGrid;
    data.extents = {4.0f, 4.0f};
    data.rippleAmount = 0.0f;
    data.edgeDepth = 0.5f;
    data.layerCount = 2u;
    data.layerStyles.assign(2u, TR::TerrainSurfaceLayerRenderStyle{});
    data.layerStyles[1].granularStrength = 0.0f;
    const TERRAIN::RENDER::TerrainSurfaceBakeInputs inputs = TERRAIN::RENDER::ResolveTerrainSurfaceBakeInputs(data);

    // 1 m セルで x 方向に 0.5 m ずつ上がる斜面。coverage 1 なので縁の沈降は無い。
    std::vector<float> source(kCells, 0.0f);
    TR::TerrainSurfaceProjectedFields fields{};
    fields.coverage.assign(kCells, 1.0f);
    fields.deformations.assign(kCells, 0.0f);
    fields.layerWeights.assign(kCells * 2u, 0.0f);
    for (uint32_t z = 0u; z <= kGrid; ++z) {
        for (uint32_t x = 0u; x <= kGrid; ++x) {
            source[at(x, z)] = 0.5f * static_cast<float>(x);
            fields.layerWeights[at(x, z)] = 1.0f;
            fields.layerWeights[kCells + at(x, z)] = x >= 2u ? 0.25f : 0.0f;
        }
    }
    const TR::TerrainSurfaceFieldRect full{0u, 0u, kGrid, kGrid};
    TERRAIN::RENDER::BakeTerrainSurfaceRect(inputs, source, full, fields);
    // 内側は中心差分、端は外側の頂点を繰り返す shader と同じ片側差分。
    if (!Near(fields.heights[at(2u, 2u)], 1.0f) || !Near(fields.slopeX[at(2u, 2u)], 0.5f) ||
        !Near(fields.slopeZ[at(2u, 2u)], 0.0f) || !Near(fields.slopeX[at(0u, 2u)], 0.25f)) {
        return Fail("terrain bake slope");
    }
    fields.coverage[at(2u, 2u)] = 0.0f;
    TERRAIN::RENDER::BakeTerrainSurfaceRect(inputs, source, full, fields);
    // coverage 0 の頂点は edgeDepth だけ沈み、隣は coverage 勾配を法線へ写す。
    if (!Near(fields.heights[at(2u, 2u)], 0.5f) || !Near(fields.slopeX[at(1u, 2u)], 0.5f - 0.5f * 0.5f) ||
        TERRAIN::RENDER::ResolveTerrainSurfaceBakeMargin(inputs) != 1u) {
        return Fail("terrain bake edge sink");
    }

    std::vector<float> pool{};
    RR::SceneSurfaceFloatPoolPacker packer(pool, 4096u);
    const RR::SceneSurfaceTerrainFloatAllocation allocation =
        packer.PackTerrainCore({kGrid, 2u, &fields, &data.layerStyles});
    if (!allocation.resident || allocation.gridResolution != kGrid || allocation.offsets.deformation != 0u ||
        RR::TerrainSurfaceCoreFloatsPerVertex(1u) != 3u || RR::TerrainSurfaceCoreFloatsPerVertex(5u) != 5u) {
        return Fail("terrain float pool allocation");
    }
    const uint32_t slopeBits = std::bit_cast<uint32_t>(pool[allocation.offsets.slope + at(2u, 1u)]);
    const uint32_t weightBits = std::bit_cast<uint32_t>(pool[allocation.offsets.layerWeights + at(3u, 1u)]);
    const float packedSlopeX =
        DirectX::PackedVector::XMConvertHalfToFloat(static_cast<DirectX::PackedVector::HALF>(slopeBits & 0xFFFFu));
    if (!Near(packedSlopeX, 0.5f) || (weightBits & 0xFFu) != 255u || ((weightBits >> 8u) & 0xFFu) != 64u ||
        !Near(pool[allocation.offsets.layerStyles + TR::kTerrainSurfaceLayerStyleFloatCount + 17u], 0.0f)) {
        return Fail("terrain float pool encoding");
    }

    // 値だけの変更は同じ offset の行を書き戻し、plane ごとに 1 範囲を公開する。
    for (uint32_t x = 0u; x <= kGrid; ++x) {
        fields.heights[at(x, 3u)] += 2.0f;
    }
    std::vector<RR::SceneSurfaceFloatRange> ranges{};
    if (!RR::PatchTerrainSurfaceRows(pool, allocation, fields, 3u, 3u, ranges) || ranges.size() != 4u ||
        ranges.front().first != allocation.offsets.height + 3u * kStride || ranges.front().count != kStride ||
        !Near(pool[allocation.offsets.height + at(1u, 3u)], fields.heights[at(1u, 3u)])) {
        return Fail("terrain float pool row patch");
    }

    const std::vector<RR::SceneSurfaceFloatPoolDelta> history{{1u, 2u, {{10u, 5u}}}, {2u, 3u, {{20u, 5u}}}};
    std::vector<RR::SceneSurfaceFloatRange> collected{};
    if (!RR::CollectSceneSurfaceFloatRangesSince(history, 1u, 3u, collected) || collected.size() != 2u ||
        RR::CollectSceneSurfaceFloatRangesSince(history, 0u, 3u, collected) || !collected.empty() ||
        !RR::CollectSceneSurfaceFloatRangesSince(history, 3u, 3u, collected) || !collected.empty()) {
        return Fail("terrain float pool history");
    }
    return 0;
}

int main() {
    if (const int result = SurfaceFieldPublication(); result != 0)
        return result;
    if (const int result = ColliderFootprintPublication(); result != 0)
        return result;
    using namespace HIKARI;
    if (const int result = NavigationPublication())
        return result;

    {
        World world;
        auto* object = world.CreateObject("Shared terrain settings");
        auto* terrain = object->AddComponent<TerrainSurfaceComponent>();
        const auto revision = object->GetRenderStateRevision();
        if (!EDITOR::DescribeComponentProperties(*terrain, {}).ok || object->GetRenderStateRevision() != revision)
            return Fail("reading terrain settings must not invalidate rendering");
        for (const char* property : {"shoreline.wetness", "stylization.textureScale", "grass.windStrength"}) {
            EDITOR::ComponentPropertyMutation edit{{}, property, 0.25f};
            if (!EDITOR::ApplyComponentProperty(*terrain, {}, edit).ok)
                return Fail("shared terrain settings edit");
        }
        if (!Near(terrain->GetSurface().shoreline.wetness, 0.25f) ||
            !Near(terrain->GetStylizationSettings().textureScale, 0.25f) ||
            !Near(terrain->GetGrassSettings().windStrength, 0.25f) || object->GetRenderStateRevision() == revision)
            return Fail("shared terrain settings projection");
    }

    {
        // Saving and reading share fields, while outdated schemas and malformed grids remain guarded.
        TERRAIN::TerrainSurface authored;
        authored.chunkCells = 2;
        authored.chunks[{-1, 2}] = TERRAIN::MakeTerrainChunk(2, 2);
        authored.deform.behavior = TERRAIN::DeformBehaviorId::SoftSnow;
        nlohmann::json expected;
        TERRAIN::SerializeTerrainSurface(authored, expected);
        if (expected["deform"]["behavior"] != 1u)
            return Fail("terrain deform behavior is stored as its number");
        for (const char* block : {"material", "stylization", "deform", "shoreline", "brush"}) {
            for (auto& value : expected[block]) {
                if (value.is_number_float())
                    value = value.get<float>() + 0.125f;
            }
        }
        expected["chunks"][0]["heights"][4] = 0.875f;
        TERRAIN::TerrainSurface restored;
        if (!TERRAIN::DeserializeTerrainSurface(expected, restored))
            return Fail("terrain decode");
        nlohmann::json actual;
        TERRAIN::SerializeTerrainSurface(restored, actual);
        if (actual != expected)
            return Fail("terrain nondefault fields and chunk roundtrip");
        auto malformed = expected;
        malformed["chunks"][0]["cells"] = 0xffffffffu;
        if (!TERRAIN::DeserializeTerrainSurface(malformed, restored) || !restored.chunks.empty())
            return Fail("oversized terrain grid rejection");
        auto outdated = expected;
        outdated["_schemaVersion"] = 3;
        if (TERRAIN::DeserializeTerrainSurface(outdated, restored))
            return Fail("outdated terrain schema rejection");
        if (TERRAIN::DeserializeTerrainSurface(nullptr, restored))
            return Fail("nonobject terrain rejection");

        // Regional updates must match a full combine for the same vertex data, including masks/clamps.
        TERRAIN::TerrainChunkRuntimeState chunk;
        chunk.cells = 2;
        chunk.baseHeights.assign(9, 1.0f);
        chunk.coverage.assign(9, 1.0f);
        TERRAIN::RebuildCombinedHeights(chunk);
        chunk.elasticHeights[4] = -10.0f;
        chunk.plasticHeights[4] = -0.1f;
        chunk.deformableMask[4] = 0.4f;
        auto full = chunk;
        TERRAIN::RebuildCombinedHeights(full);
        TERRAIN::RebuildCombinedHeightsInRegion(chunk, 1, 1, 1, 1);
        if (chunk.heights != full.heights || chunk.deformHeights != full.deformHeights ||
            chunk.elasticHeights != full.elasticHeights || chunk.plasticHeights != full.plasticHeights)
            return Fail("regional/full sand projection parity");
        const auto dormant = TERRAIN::TerrainChunkRuntimeState{};
        auto dormantCopy = dormant;
        if (TERRAIN::RelaxChunk(dormantCopy, .125f) || dormantCopy.stateRevision != dormant.stateRevision)
            return Fail("dormant sand lifecycle");
    }

    {
        // 筆刷だけの変更でも文書更新から脱落させない。
        TerrainSurfaceComponent component;
        for (const auto field : {&TERRAIN::TerrainBrush::shaftDepth, &TERRAIN::TerrainBrush::shaftWallWidth,
                                 &TERRAIN::TerrainBrush::noiseAmplitude, &TERRAIN::TerrainBrush::noiseScale}) {
            auto surface = component.GetSurface();
            surface.brush.*field += 0.25f;
            component.SetSurface(surface);
            if (component.GetSurface().brush != surface.brush)
                return Fail("brush-only document update");
        }
        auto surface = component.GetSurface();
        ++surface.brush.noiseSeed;
        component.SetSurface(surface);
        if (component.GetSurface().brush != surface.brush)
            return Fail("brush seed document update");

        // 負座標の接合部をまたぐ編集・サンプリング・Undo 差分を一度に確認する。
        surface.chunkCells = 4;
        surface.heightScale = 1.0f;
        surface.shoreline.edgeFalloff = 0.0f;
        surface.shoreline.edgeDepth = 0.0f;
        surface.brush.radius = 0.75f;
        surface.brush.strength = 1.0f;
        component.SetSurface(surface);
        const MATH::Vec3 point{-0.125f, 0.0f, -0.125f};
        component.BeginGeometryEditBatch();
        if (!component.SculptAtLocal(point, 1.0f) || !component.EndGeometryEditBatch())
            return Fail("terrain brush edit batch");
        nlohmann::json before, after;
        if (!component.ConsumeGeometryEditDelta(before, after) || before == after)
            return Fail("terrain brush undo delta");
        float height = 0.0f;
        if (!component.SampleLocalSurfaceHeight(point, height) || height <= 0.0f ||
            !component.PaintLayerAtLocal("snow", point) || component.GetLayerCount() < 2)
            return Fail("terrain negative-coordinate sculpt and layer paint");
        const float coverage = component.SampleLocalCoverage(point);
        if (!component.PaintCoverageAtLocal(0.0f, point) || component.SampleLocalCoverage(point) >= coverage)
            return Fail("terrain coverage paint projection");
        nlohmann::json saved;
        component.Serialize(saved);
        TerrainSurfaceComponent restored;
        restored.Deserialize(saved);
        if (!Near(restored.SampleLocalCoverage(point), component.SampleLocalCoverage(point)))
            return Fail("edited terrain reload sampling");
    }

    const TERRAIN::TerrainInteractionSweepPlan ordinarySweep =
        TERRAIN::PlanTerrainInteractionSweep(0.44f, 0.22f, 1.0f / 60.0f);
    const TERRAIN::TerrainInteractionSweepPlan boundedSweep =
        TERRAIN::PlanTerrainInteractionSweep(1.8f, 0.22f, 1.0f / 60.0f);
    const TERRAIN::TerrainInteractionSweepPlan teleportSweep =
        TERRAIN::PlanTerrainInteractionSweep(12.0f, 0.22f, 1.0f / 60.0f);
    if (ordinarySweep.discontinuity || ordinarySweep.sampleCount != 6u || boundedSweep.discontinuity ||
        boundedSweep.sampleCount != TERRAIN::kTerrainInteractionMaxSweepSamples || !teleportSweep.discontinuity ||
        teleportSweep.sampleCount != 1u) {
        return Fail("terrain interaction bounded sweep planning");
    }

    const auto deltaChunk = [](int32_t x, int32_t z, float height) {
        return nlohmann::json{
            {"x", x},
            {"z", z},
            {"cells", 1u},
            {"layerCount", 1u},
            {"heights", {height, height, height, height}},
            {"coverage", {1.0f, 1.0f, 1.0f, 1.0f}},
            {"layerWeights", {1.0f, 1.0f, 1.0f, 1.0f}},
        };
    };
    const nlohmann::json originalTerrainProperties{
        {"presetId", "sand"},
        {"layerPresetIds", {"sand"}},
        {"chunks",
         {
             deltaChunk(0, 0, 0.0f),
             deltaChunk(2, 0, 2.0f),
         }},
    };
    nlohmann::json forwardDelta{
        {"presetId", "grass"},
        {"layerPresetIds", {"grass"}},
        {"chunks",
         {
             nlohmann::json{{"x", 0}, {"z", 0}, {"exists", false}},
             [&] {
                 nlohmann::json value = deltaChunk(-1, 2, 4.0f);
                 value["exists"] = true;
                 return value;
             }(),
             [&] {
                 nlohmann::json value = deltaChunk(2, 0, 3.0f);
                 value["exists"] = true;
                 return value;
             }(),
         }},
    };
    nlohmann::json reverseDelta{
        {"presetId", "sand"},
        {"layerPresetIds", {"sand"}},
        {"chunks",
         {
             [&] {
                 nlohmann::json value = deltaChunk(0, 0, 0.0f);
                 value["exists"] = true;
                 return value;
             }(),
             nlohmann::json{{"x", -1}, {"z", 2}, {"exists", false}},
             [&] {
                 nlohmann::json value = deltaChunk(2, 0, 2.0f);
                 value["exists"] = true;
                 return value;
             }(),
         }},
    };
    nlohmann::json editedTerrainProperties = originalTerrainProperties;
    if (!EDITOR::ApplyTerrainSurfaceEditDelta(editedTerrainProperties, forwardDelta) ||
        editedTerrainProperties["presetId"] != "grass" || editedTerrainProperties["chunks"].size() != 2u ||
        editedTerrainProperties["chunks"][0]["x"] != -1 || editedTerrainProperties["chunks"][1]["x"] != 2 ||
        editedTerrainProperties["chunks"][0].contains("exists") ||
        editedTerrainProperties["chunks"][1].contains("exists")) {
        return Fail("terrain keyed forward delta is not canonical");
    }
    if (EDITOR::ApplyTerrainSurfaceEditDelta(editedTerrainProperties, forwardDelta)) {
        return Fail("terrain keyed delta is not idempotent");
    }
    if (!EDITOR::ApplyTerrainSurfaceEditDelta(editedTerrainProperties, reverseDelta) ||
        editedTerrainProperties != originalTerrainProperties) {
        return Fail("terrain keyed reverse delta did not restore source");
    }
    const nlohmann::json malformedDelta{
        {"chunks", {nlohmann::json{{"x", "invalid"}, {"z", 1}, {"exists", true}}}},
    };
    if (EDITOR::ApplyTerrainSurfaceEditDelta(editedTerrainProperties, malformedDelta) ||
        editedTerrainProperties != originalTerrainProperties) {
        return Fail("malformed terrain delta mutated component properties");
    }

    // Production terrain work must not silently retune the five shipped
    // looks. These are the render-facing values used by existing scenes.
    const auto presetVisualMatches = [](const TERRAIN::SurfacePreset& preset, std::string_view id,
                                        TERRAIN::ShadingModelId shading, const MATH::Vec3& lightTint,
                                        const MATH::Vec3& darkTint, float sparkle, float ripple,
                                        TERRAIN::SurfaceLayerMaterialKind kind, uint32_t atlasTile, float metersPerTile,
                                        float textureStrength, float normalStrength, float roughness,
                                        float saturation) {
        return preset.id == id && preset.shading == shading && Near(preset.material.lightTint.x, lightTint.x) &&
               Near(preset.material.lightTint.y, lightTint.y) && Near(preset.material.lightTint.z, lightTint.z) &&
               Near(preset.material.darkTint.x, darkTint.x) && Near(preset.material.darkTint.y, darkTint.y) &&
               Near(preset.material.darkTint.z, darkTint.z) && Near(preset.material.sparkleStrength, sparkle) &&
               Near(preset.material.rippleAmount, ripple) && preset.material.toonBandCount == 3u &&
               Near(preset.material.toonBandHardness, 0.14f) && Near(preset.tint.light.x, lightTint.x) &&
               Near(preset.tint.light.y, lightTint.y) && Near(preset.tint.light.z, lightTint.z) &&
               Near(preset.tint.dark.x, darkTint.x) && Near(preset.tint.dark.y, darkTint.y) &&
               Near(preset.tint.dark.z, darkTint.z) && preset.layerMaterial.kind == kind &&
               preset.layerMaterial.atlasTile == atlasTile && Near(preset.layerMaterial.metersPerTile, metersPerTile) &&
               Near(preset.layerMaterial.textureStrength, textureStrength) &&
               Near(preset.layerMaterial.normalStrength, normalStrength) &&
               Near(preset.layerMaterial.roughness, roughness) && Near(preset.layerMaterial.saturation, saturation) &&
               Near(preset.layerMaterial.shadowStrength, 1.0f) && Near(preset.shoreline.edgeFalloff, 4.0f) &&
               Near(preset.shoreline.edgeDepth, 1.5f) && Near(preset.shoreline.wetness, 0.0f);
    };
    if (!presetVisualMatches(TERRAIN::MakeSandPreset(), "sand", TERRAIN::ShadingModelId::GranularAnime,
                             {0.95f, 0.84f, 0.60f}, {0.62f, 0.47f, 0.29f}, 0.35f, 0.45f,
                             TERRAIN::SurfaceLayerMaterialKind::Sand, 0u, 25.0f, 0.52f, 0.72f, 0.92f, 1.14f) ||
        !presetVisualMatches(TERRAIN::MakeSnowPreset(), "snow", TERRAIN::ShadingModelId::GranularAnime,
                             {1.0f, 1.0f, 1.0f}, {0.68f, 0.78f, 0.90f}, 0.35f, 0.45f,
                             TERRAIN::SurfaceLayerMaterialKind::Snow, 0u, 2.8f, 0.30f, 0.42f, 0.96f, 0.18f) ||
        !presetVisualMatches(TERRAIN::MakeDirtPreset(), "dirt", TERRAIN::ShadingModelId::StylizedGround,
                             {0.52f, 0.38f, 0.24f}, {0.28f, 0.20f, 0.12f}, 0.0f, 0.45f,
                             TERRAIN::SurfaceLayerMaterialKind::Dirt, 1u, 1.4f, 0.72f, 0.58f, 0.95f, 0.78f) ||
        !presetVisualMatches(TERRAIN::MakeGrassPreset(), "grass", TERRAIN::ShadingModelId::StylizedGround,
                             {0.68f, 0.90f, 0.50f}, {0.18f, 0.32f, 0.12f}, 0.0f, 0.0f,
                             TERRAIN::SurfaceLayerMaterialKind::Grass, 2u, 1.4f, 0.68f, 0.48f, 0.92f, 0.82f) ||
        !presetVisualMatches(TERRAIN::MakeRockPreset(), "rock", TERRAIN::ShadingModelId::StylizedGround,
                             {0.86f, 0.86f, 0.86f}, {0.30f, 0.30f, 0.30f}, 0.0f, 0.0f,
                             TERRAIN::SurfaceLayerMaterialKind::Rock, 3u, 1.4f, 0.76f, 0.74f, 0.88f, 0.30f)) {
        return Fail("built-in terrain visual preset baseline");
    }

    TERRAIN::TerrainStylizationSettings stylization{};
    if (!Near(stylization.textureScale, 1.0f) || !Near(stylization.textureStrength, 1.0f) ||
        !Near(stylization.normalStrength, 1.0f) || !Near(stylization.saturation, 1.0f) ||
        !Near(stylization.shadowStrength, 1.0f) || !Near(stylization.rockSaturation, 0.30f) ||
        !Near(stylization.facetSizeMeters, 1.6f) || !Near(stylization.facetStrength, 0.0f)) {
        return Fail("terrain stylization defaults");
    }
    stylization.textureScale = -10.0f;
    stylization.textureStrength = 10.0f;
    stylization.normalStrength = (std::numeric_limits<float>::quiet_NaN)();
    stylization.saturation = -1.0f;
    stylization.shadowStrength = 10.0f;
    stylization.rockSaturation = -1.0f;
    stylization.facetSizeMeters = -4.0f;
    stylization.facetStrength = 4.0f;
    stylization = TERRAIN::SanitizeTerrainStylizationSettings(stylization);
    if (!Near(stylization.textureScale, 0.1f) || !Near(stylization.textureStrength, 2.0f) ||
        !Near(stylization.normalStrength, 1.0f) || !Near(stylization.saturation, 0.0f) ||
        !Near(stylization.shadowStrength, 1.0f) || !Near(stylization.rockSaturation, 0.0f) ||
        !Near(stylization.facetSizeMeters, 0.25f) || !Near(stylization.facetStrength, 1.0f)) {
        return Fail("terrain stylization sanitization");
    }

    TERRAIN::TerrainBrush brush{};
    brush.strength = 100.0f;
    brush.ridgeDirectionDegrees = 1000.0f;
    brush.ridgeSharpness = -1.0f;
    brush.terraceStepHeight = 100.0f;
    brush.shaftDepth = 1000.0f;
    brush.shaftWallWidth = -1.0f;
    brush.noiseAmplitude = 100.0f;
    brush.noiseScale = 0.0f;
    brush = TERRAIN::SanitizeTerrainBrush(brush);
    if (!Near(brush.strength, TERRAIN::kTerrainBrushMaximumStrength) || !Near(brush.ridgeDirectionDegrees, 180.0f) ||
        !Near(brush.ridgeSharpness, 0.05f) || !Near(brush.terraceStepHeight, 8.0f) || !Near(brush.shaftDepth, 256.0f) ||
        !Near(brush.shaftWallWidth, 0.01f) || !Near(brush.noiseAmplitude, 16.0f) || !Near(brush.noiseScale, 0.05f)) {
        return Fail("terrain production brush bounds");
    }

    const float noiseA = TERRAIN::EvaluateTerrainAuthoringNoise(12.5f, -8.25f, 1337u, 2.0f, 4u);
    const float noiseB = TERRAIN::EvaluateTerrainAuthoringNoise(12.5f, -8.25f, 1337u, 2.0f, 4u);
    const float noiseOther = TERRAIN::EvaluateTerrainAuthoringNoise(12.5f, -8.25f, 7331u, 2.0f, 4u);
    if (!std::isfinite(noiseA) || !Near(noiseA, noiseB) || Near(noiseA, noiseOther)) {
        return Fail("terrain local noise determinism");
    }

    TERRAIN::TerrainRampSpec ramp{};
    ramp.halfWidth = -1.0f;
    ramp.shoulderWidth = 1000.0f;
    ramp.blendAmount = 5.0f;
    ramp = TERRAIN::SanitizeTerrainRampSpec(ramp);
    if (!Near(ramp.halfWidth, 0.05f) || !Near(ramp.shoulderWidth, 256.0f) || !Near(ramp.blendAmount, 1.0f)) {
        return Fail("terrain ramp authoring bounds");
    }

    TERRAIN::TerrainOpeningSpec opening{};
    opening.normal = {};
    opening.width = -1.0f;
    opening.height = 1000.0f;
    opening.depth = 0.0f;
    opening.feather = 1000.0f;
    opening = TERRAIN::SanitizeTerrainOpeningSpec(opening);
    if (!Near(opening.normal.z, 1.0f) || !Near(opening.width, 0.10f) || !Near(opening.height, 256.0f) ||
        !Near(opening.depth, 0.10f) || !Near(opening.feather, 0.05f)) {
        return Fail("terrain cliff opening authoring bounds");
    }

    const TERRAIN::TerrainGrassSettings grass{};
    if (!grass.enabled) {
        return Fail("terrain grass enabled default");
    }
    if (!Near(grass.density, 0.42f)) {
        return Fail("terrain grass density default");
    }
    if (!Near(grass.bladeHeight, 0.34f)) {
        return Fail("terrain grass blade height default");
    }
    if (!Near(grass.bladeWidth, 0.028f)) {
        return Fail("terrain grass blade width default");
    }
    if (!Near(grass.layerWeightThreshold, 0.35f)) {
        return Fail("terrain grass threshold default");
    }
    if (!Near(grass.maxSlopeDegrees, 42.0f)) {
        return Fail("terrain grass slope default");
    }
    if (!Near(grass.windStrength, 0.055f)) {
        return Fail("terrain grass wind strength default");
    }
    if (!Near(grass.windSpeed, 1.25f)) {
        return Fail("terrain grass wind speed default");
    }
    if (!Near(grass.spatialFrequency, 0.85f)) {
        return Fail("terrain grass spatial frequency default");
    }
    if (grass.maxClumps != TERRAIN::kTerrainGrassMaxClumps / 2u) {
        return Fail("terrain grass clump default");
    }

    // 2x2 セルの斜面 h=x。非一様 scale と平行移動を通しても、
    // query はローカル高さ場と同じ surface を返さなければならない。
    TERRAIN::TerrainChunkRuntimeState state{};
    state.key = {0, 0};
    state.cells = 2u;
    state.cellSize = 1.0f;
    state.chunkWorldSize = 2.0f;
    state.chunkOriginLocal = {0.0f, 0.0f};
    state.heights = {
        0.0f, 1.0f, 2.0f, 0.0f, 1.0f, 2.0f, 0.0f, 1.0f, 2.0f,
    };
    state.coverage.assign(9u, 1.0f);

    const MATH::Mat4 localToWorld = MATH::Mat4::TRS({10.0f, 5.0f, -4.0f}, MATH::Quat::Identity(), {2.0f, 3.0f, 0.5f});
    TERRAIN::TerrainSurfaceSnapshotEntry entry{};
    entry.object = {7u, 3u};
    entry.chunk = std::make_shared<const TERRAIN::TerrainChunkRuntimeState>(state);
    entry.localToWorld = localToWorld;
    entry.worldToLocal = MATH::Inverse(localToWorld);
    entry.normalMatrix = MATH::BuildNormalMatrixFromWorld(localToWorld);

    TERRAIN::TerrainSurfaceQueryService service{};
    service.PublishSnapshot({entry});
    if (service.GetSnapshotRevision() != 1u || service.GetSurfaceCount() != 1u) {
        return Fail("snapshot publish contract");
    }

    float localHeight = 0.0f;
    if (!service.SampleChunkLocalHeight(state, 0.5f, 0.5f, localHeight) || !Near(localHeight, 0.5f)) {
        return Fail("local bilinear height");
    }

    TERRAIN::TerrainChunkRuntimeState edgeState = state;
    edgeState.settings.edgeDepth = 2.0f;
    edgeState.coverage.assign(9u, 0.5f);
    float edgeHeight = 0.0f;
    float edgeCoverage = 0.0f;
    if (!service.SampleChunkLocalSurface(edgeState, 0.5f, 0.5f, edgeHeight, edgeCoverage) ||
        !Near(edgeCoverage, 0.5f) || !Near(edgeHeight, -0.5f)) {
        return Fail("GPU-matched shoreline effective height");
    }

    TERRAIN::TerrainSurfaceQueryService edgeService{};
    TERRAIN::TerrainSurfaceSnapshotEntry edgeEntry = entry;
    edgeState.coverage.assign(9u, TERRAIN::kTerrainSurfaceVisibleCoverageThreshold * 0.5f);
    edgeEntry.chunk = std::make_shared<const TERRAIN::TerrainChunkRuntimeState>(edgeState);
    edgeService.PublishSnapshot({edgeEntry});
    float clippedHeight = 0.0f;
    MATH::Vec3 clippedNormal{};
    if (edgeService.SampleWorldHeight({12.0f, 5.0f, -3.5f}, clippedHeight, clippedNormal)) {
        return Fail("nearly empty shoreline clip threshold");
    }
    edgeState.coverage.assign(9u, TERRAIN::kTerrainSurfaceVisibleCoverageThreshold);
    edgeEntry.chunk = std::make_shared<const TERRAIN::TerrainChunkRuntimeState>(edgeState);
    edgeService.PublishSnapshot({edgeEntry});
    const float visibleCoverage = TERRAIN::kTerrainSurfaceVisibleCoverageThreshold;
    const float visibleSmooth = visibleCoverage * visibleCoverage * (3.0f - 2.0f * visibleCoverage);
    const float expectedEdgeWorldHeight = 5.0f + 3.0f * (1.0f - 2.0f * (1.0f - visibleSmooth));
    if (!edgeService.SampleWorldHeight({12.0f, 5.0f, -3.5f}, clippedHeight, clippedNormal) ||
        !Near(clippedHeight, expectedEdgeWorldHeight)) {
        return Fail("visible shoreline threshold parity");
    }

    float worldHeight = 0.0f;
    MATH::Vec3 worldNormal{};
    if (!service.SampleWorldHeight({12.0f, 8.1f, -3.5f}, worldHeight, worldNormal) || !Near(worldHeight, 8.0f)) {
        return Fail("transformed world height");
    }
    const MATH::Vec3 expectedNormal = MATH::Normalize({-0.5f, 1.0f / 3.0f, 0.0f});
    if (!Near(worldNormal.x, expectedNormal.x) || !Near(worldNormal.y, expectedNormal.y) ||
        !Near(worldNormal.z, expectedNormal.z)) {
        return Fail("inverse-transpose world normal");
    }

    TERRAIN::TerrainChunkRuntimeSnapshotPtr found{};
    if (!service.TryGetChunkState({7u, 3u}, {0, 0}, found) || !found || !Near(found->heights[1], 1.0f)) {
        return Fail("immutable chunk lookup");
    }

    // sparse / 負座標 / 空間順でない publish でも lookup を維持する。
    // terrain page 全走査を置き換えた O(1) chunk index の回帰契約。
    const auto makeIndexedEntry = [](TERRAIN::TerrainChunkKey key, float height) {
        TERRAIN::TerrainChunkRuntimeState chunk{};
        chunk.key = key;
        chunk.cells = 1u;
        chunk.cellSize = 2.0f;
        chunk.chunkWorldSize = 2.0f;
        chunk.chunkOriginLocal = {
            static_cast<float>(key.x) * 2.0f,
            static_cast<float>(key.z) * 2.0f,
        };
        chunk.heights.assign(4u, height);
        chunk.coverage.assign(4u, 1.0f);
        TERRAIN::TerrainSurfaceSnapshotEntry indexed{};
        indexed.object = {11u, 4u};
        indexed.chunk = std::make_shared<const TERRAIN::TerrainChunkRuntimeState>(chunk);
        indexed.localToWorld = MATH::Mat4::Identity();
        indexed.worldToLocal = MATH::Mat4::Identity();
        indexed.normalMatrix = MATH::Mat4::Identity();
        return indexed;
    };
    TERRAIN::TerrainSurfaceQueryService indexedService{};
    indexedService.PublishSnapshot({
        makeIndexedEntry({400, -300}, 9.0f),
        makeIndexedEntry({-2, 0}, 3.0f),
        makeIndexedEntry({0, 0}, 1.0f),
    });
    float indexedHeight = 0.0f;
    MATH::Vec3 indexedNormal{};
    if (!indexedService.SampleWorldHeight({-3.0f, 3.2f, 1.0f}, indexedHeight, indexedNormal) ||
        !Near(indexedHeight, 3.0f) || !indexedService.TryGetChunkState({11u, 4u}, {400, -300}, found) || !found ||
        !Near(found->heights.front(), 9.0f)) {
        return Fail("sparse indexed sand chunk lookup");
    }

    TERRAIN::TerrainChunkRuntimeState simulationState = state;
    simulationState.baseHeights.assign(9u, 0.25f);
    simulationState.elasticHeights.assign(9u, -0.05f);
    simulationState.plasticHeights.assign(9u, -0.10f);
    simulationState.recoveryHold.assign(9u, 2.0f);
    simulationState.elasticScratch.assign(9u, 7.0f);
    simulationState.deformHeights.assign(9u, -0.15f);
    simulationState.deformableMask.assign(9u, 0.5f);
    simulationState.responseGridActive = true;
    simulationState.hasDeformation = true;
    simulationState.stateRevision = 42u;
    const TERRAIN::TerrainChunkRuntimeSnapshotPtr lightweight =
        TERRAIN::MakeTerrainChunkRuntimeSnapshot(simulationState);
    if (!lightweight || !lightweight->baseHeights.empty() || !lightweight->elasticHeights.empty() ||
        !lightweight->plasticHeights.empty() || !lightweight->recoveryHold.empty() ||
        !lightweight->elasticScratch.empty() || lightweight->heights != simulationState.heights ||
        lightweight->deformHeights != simulationState.deformHeights ||
        lightweight->coverage != simulationState.coverage ||
        lightweight->deformableMask != simulationState.deformableMask || !lightweight->responseGridActive ||
        !lightweight->hasDeformation || lightweight->stateRevision != 42u) {
        return Fail("lightweight immutable runtime projection");
    }
    simulationState.heights[0] = 99.0f;
    if (Near(lightweight->heights[0], simulationState.heights[0])) {
        return Fail("runtime projection storage independence");
    }

    TERRAIN::TerrainChunkRuntimeState malformed = state;
    malformed.heights.resize(1u);
    if (service.SampleChunkLocalHeight(malformed, 0.5f, 0.5f, localHeight)) {
        return Fail("malformed height grid rejection");
    }
    malformed = state;
    malformed.coverage.clear();
    float coverage = 1.0f;
    if (service.SampleChunkLocalCoverage(malformed, 0.5f, 0.5f, coverage) || coverage != 0.0f) {
        return Fail("malformed coverage grid rejection");
    }
    if (service.SampleChunkLocalSurface(malformed, 0.5f, 0.5f, edgeHeight, edgeCoverage)) {
        return Fail("malformed effective surface rejection");
    }

    // contact は gate に限定し、solver penetration/load が 0 でも
    // animation profile だけで見える深さへ収束する。
    TERRAIN::DeformParams responseProfile{};
    if (!Near(responseProfile.stampRadius, 0.58f) || !Near(responseProfile.refillPerSecond, 0.008f) ||
        !Near(responseProfile.diffusionPerSecond, 0.04f) || !Near(responseProfile.plasticity, 0.93f) ||
        !Near(responseProfile.plasticRefillPerSecond, 0.0003f) || !Near(responseProfile.recoveryDelaySeconds, 8.0f)) {
        return Fail("persistent animation deformation defaults");
    }
    const TERRAIN::TerrainSurfaceResponseResult settling =
        TERRAIN::EvaluateTerrainSurfaceResponse(responseProfile, {
                                                                     0.5f,
                                                                     0.0f,
                                                                     0.0f,
                                                                     0.0f,
                                                                     1.0f / 60.0f,
                                                                     1.0f,
                                                                 });
    const TERRAIN::TerrainSurfaceResponseResult settled =
        TERRAIN::EvaluateTerrainSurfaceResponse(responseProfile, {
                                                                     0.5f,
                                                                     0.0f,
                                                                     0.0f,
                                                                     1.0f,
                                                                     0.0f,
                                                                     1.0f,
                                                                 });
    if (!(settling.targetDepthLocal > 0.0f) || !(settling.targetDepthLocal < settled.targetDepthLocal) ||
        !Near(settled.targetDepthLocal, 0.18f, 1.0e-3f)) {
        return Fail("art-directed zero-penetration response");
    }
    const TERRAIN::TerrainSurfaceResponseResult holdOnly =
        TERRAIN::EvaluateTerrainSurfaceResponse(responseProfile, {0.5f, 0.2f, 10.0f, 1.0f, 1.0f / 60.0f, 0.0f});
    if (!holdOnly.holdOnly || holdOnly.targetDepthLocal != 0.0f) {
        return Fail("sleeping dynamic hold-only response");
    }

    // sweep 中は新しい stamp の rim が直前 stamp の pit と重なる。
    // 合成順に関係なく pit が残り、移動後も軌跡を置換しない。
    const float previousPit =
        TERRAIN::AccumulateTerrainSurfaceDisplacement(-0.12f, 0.04f, TERRAIN::TerrainSurfaceDisplacementKind::Rim);
    const float rimThenPit = TERRAIN::AccumulateTerrainSurfaceDisplacement(
        0.04f, -0.12f, TERRAIN::TerrainSurfaceDisplacementKind::Depression);
    const float deeperTrail = TERRAIN::AccumulateTerrainSurfaceDisplacement(
        previousPit, -0.16f, TERRAIN::TerrainSurfaceDisplacementKind::Depression);
    const float retainedTrail =
        TERRAIN::AccumulateTerrainSurfaceDisplacement(deeperTrail, 0.08f, TERRAIN::TerrainSurfaceDisplacementKind::Rim);
    if (!Near(previousPit, -0.12f) || !Near(rimThenPit, -0.12f) || !Near(deeperTrail, -0.16f) ||
        !Near(retainedTrail, -0.16f)) {
        return Fail("moving imprint trail accumulation");
    }

    // Shared seam は elastic / plastic を別々の chunk から合成しない。
    // 同じ -0.30 m を表す active/frozen pair を混ぜると旧実装では
    // -0.45 m になったため、pair 選択と最終 clamp を数値契約にする。
    constexpr float minimumSandResponse = -0.30f;
    constexpr float maximumSandResponse = 0.096f;
    const TERRAIN::TerrainSurfaceDisplacementChannels activeChannels =
        TERRAIN::ClampTerrainSurfaceDisplacementChannels({-0.15f, -0.15f}, minimumSandResponse, maximumSandResponse);
    const TERRAIN::TerrainSurfaceDisplacementChannels frozenChannels =
        TERRAIN::ClampTerrainSurfaceDisplacementChannels({0.0f, -0.30f}, minimumSandResponse, maximumSandResponse);
    const TERRAIN::TerrainSurfaceDisplacementChannels selectedChannels =
        TERRAIN::SelectTerrainSurfaceDisplacementChannels(activeChannels, frozenChannels);
    const TERRAIN::TerrainSurfaceDisplacementChannels clampedRunaway =
        TERRAIN::ClampTerrainSurfaceDisplacementChannels({-0.15f, -0.30f}, minimumSandResponse, maximumSandResponse);
    const TERRAIN::TerrainSurfaceDisplacementChannels finiteFallback = TERRAIN::ClampTerrainSurfaceDisplacementChannels(
        {
            (std::numeric_limits<float>::quiet_NaN)(),
            -(std::numeric_limits<float>::infinity)(),
        },
        minimumSandResponse, maximumSandResponse);
    if (!Near(selectedChannels.elastic, 0.0f) || !Near(selectedChannels.plastic, -0.30f) ||
        !Near(selectedChannels.Combined(), -0.30f) || !Near(clampedRunaway.Combined(), minimumSandResponse) ||
        !Near(finiteFallback.Combined(), 0.0f)) {
        return Fail("bounded paired sand seam response");
    }

    // settling は strength と独立した opt-in。Custom/default は false、
    // active 自動 contact は true を保持し、End は sanitize で必ずfalse。
    TERRAIN::TerrainInteractionSample defaultInteraction{};
    if (defaultInteraction.allowSettling || !TERRAIN::IsFiniteTerrainInteractionSample(defaultInteraction)) {
        return Fail("terrain interaction settling default");
    }
    TERRAIN::TerrainInteractionSample activeInteraction{};
    activeInteraction.phase = TERRAIN::TerrainInteractionPhase::Persist;
    activeInteraction.strength = 0.0f;
    activeInteraction.allowSettling = true;
    activeInteraction = TERRAIN::SanitizeTerrainInteractionSample(activeInteraction);
    if (!activeInteraction.allowSettling || activeInteraction.strength != 0.0f ||
        !TERRAIN::IsFiniteTerrainInteractionSample(activeInteraction)) {
        return Fail("hold-only settling permission");
    }
    activeInteraction.phase = TERRAIN::TerrainInteractionPhase::End;
    activeInteraction.allowSettling = true;
    activeInteraction = TERRAIN::SanitizeTerrainInteractionSample(activeInteraction);
    if (activeInteraction.allowSettling) {
        return Fail("ended interaction settling rejection");
    }
    TERRAIN::TerrainInteractionSample smallFootprint{};
    smallFootprint.footprint.halfExtentsWS = {0.002f, 0.003f};
    smallFootprint.footprint.verticalRadiusWS = 0.004f;
    smallFootprint = TERRAIN::SanitizeTerrainInteractionSample(smallFootprint);
    if (!Near(smallFootprint.footprint.halfExtentsWS.x, 0.002f) ||
        !Near(smallFootprint.footprint.halfExtentsWS.y, 0.003f) ||
        !Near(smallFootprint.footprint.verticalRadiusWS, 0.004f)) {
        return Fail("small physical footprint preservation");
    }
    TERRAIN::TerrainInteractionSample nonFiniteInteraction{};
    nonFiniteInteraction.contactAgeSeconds = (std::numeric_limits<float>::quiet_NaN)();
    if (TERRAIN::IsFiniteTerrainInteractionSample(nonFiniteInteraction)) {
        return Fail("non-finite interaction rejection");
    }
    nonFiniteInteraction = TERRAIN::SanitizeTerrainInteractionSample(nonFiniteInteraction);
    if (!TERRAIN::IsFiniteTerrainInteractionSample(nonFiniteInteraction) ||
        nonFiniteInteraction.contactAgeSeconds != 0.0f) {
        return Fail("non-finite interaction sanitization");
    }

    // response subdivision は geometry だけを細分化し、material uv0 の
    // tile 密度と field 用 uv1 を変えてはならない。
    ProceduralMeshSettings coarseSettings{};
    coarseSettings.kind = ProceduralMeshKind::GridPlane;
    coarseSettings.width = 8.0f;
    coarseSettings.depth = 6.0f;
    coarseSettings.segmentsX = 4u;
    coarseSettings.segmentsY = 4u;
    coarseSettings.uvTilesX = 8.0f;
    coarseSettings.uvTilesY = 6.0f;
    MeshPrimitive coarseMesh{};
    PROCEDURAL::BuildProceduralMesh(coarseSettings, coarseMesh);
    ProceduralMeshSettings responseSettings = coarseSettings;
    responseSettings.segmentsX = 8u;
    responseSettings.segmentsY = 8u;
    MeshPrimitive responseMesh{};
    PROCEDURAL::BuildProceduralMesh(responseSettings, responseMesh);
    const Vertex3D& coarseCenter = coarseMesh.staticVertices[2u * 5u + 2u];
    const Vertex3D& responseCenter = responseMesh.staticVertices[4u * 9u + 4u];
    if (!Near(coarseCenter.uv0.x, 4.0f) || !Near(coarseCenter.uv0.y, 3.0f) || !Near(coarseCenter.uv1.x, 0.5f) ||
        !Near(coarseCenter.uv1.y, 0.5f) || !Near(responseCenter.uv0.x, coarseCenter.uv0.x) ||
        !Near(responseCenter.uv0.y, coarseCenter.uv0.y) || !Near(responseCenter.uv1.x, coarseCenter.uv1.x) ||
        !Near(responseCenter.uv1.y, coarseCenter.uv1.y)) {
        return Fail("response grid material UV stability");
    }

    // Terrain layer policy は runtime 解像度の mask へ一度だけ解決する。
    // hard layer は沈まず、authored の混合重みは滑らかに残る。
    TERRAIN::TerrainSurface layerSurface{};
    TERRAIN::TerrainChunk layerChunk = TERRAIN::MakeTerrainChunk(1u, 1u);
    std::vector<float> deformableMask{};
    const auto expectPureMask = [&](const char* presetId, float expected) {
        layerSurface.layerPresetIds = {presetId};
        std::fill(layerChunk.layerWeights.begin(), layerChunk.layerWeights.end(), 1.0f);
        TERRAIN::ProjectTerrainChunkDeformableMask(layerSurface, layerChunk, 2u, deformableMask);
        return deformableMask.size() == 9u && std::all_of(deformableMask.begin(), deformableMask.end(),
                                                          [&](float value) { return Near(value, expected); });
    };
    if (!expectPureMask("grass", 0.0f) || !expectPureMask("dirt", 0.0f) || !expectPureMask("rock", 0.0f) ||
        !expectPureMask("sand", 1.0f) || !expectPureMask("snow", 1.0f)) {
        return Fail("pure terrain layer deformation masks");
    }

    TERRAIN::TerrainChunk mixedLayerChunk = TERRAIN::MakeTerrainChunk(1u, 2u);
    layerSurface.layerPresetIds = {"sand", "grass"};
    const std::size_t mixedVertexCount = 4u;
    for (std::size_t vertex = 0u; vertex < mixedVertexCount; ++vertex) {
        const float sandWeight = vertex == 0u || vertex == 2u ? 1.0f : 0.0f;
        mixedLayerChunk.layerWeights[vertex] = sandWeight;
        mixedLayerChunk.layerWeights[mixedVertexCount + vertex] = 1.0f - sandWeight;
    }
    TERRAIN::ProjectTerrainChunkDeformableMask(layerSurface, mixedLayerChunk, 2u, deformableMask);
    if (deformableMask.size() != 9u || !Near(deformableMask[0], 1.0f) || !Near(deformableMask[2], 0.0f) ||
        !Near(deformableMask[6], 1.0f) || !Near(deformableMask[8], 0.0f) || !Near(deformableMask[4], 0.5f)) {
        return Fail("mixed terrain layer bilinear mask");
    }

    // layerPresetIds が無い場合、層 0 は地形の presetId を使い、未設定の層は変形しない。
    // layer paint revision は投影後の gate 自体を変える。
    layerSurface.layerPresetIds.clear();
    TERRAIN::TerrainChunk unassignedLayerChunk = TERRAIN::MakeTerrainChunk(1u, 5u);
    const std::size_t unassignedVertexCount = 4u;
    std::fill(unassignedLayerChunk.layerWeights.begin(), unassignedLayerChunk.layerWeights.end(), 0.0f);
    for (std::size_t vertex = 0u; vertex < unassignedVertexCount; ++vertex) {
        unassignedLayerChunk.layerWeights[2u * unassignedVertexCount + vertex] = 1.0f;
    }
    TERRAIN::ProjectTerrainChunkDeformableMask(layerSurface, unassignedLayerChunk, 1u, deformableMask);
    if (deformableMask.size() != 4u ||
        !std::all_of(deformableMask.begin(), deformableMask.end(), [](float value) { return Near(value, 0.0f); })) {
        return Fail("unassigned terrain layer is not deformable");
    }
    std::fill(unassignedLayerChunk.layerWeights.begin(), unassignedLayerChunk.layerWeights.end(), 0.0f);
    for (std::size_t vertex = 0u; vertex < unassignedVertexCount; ++vertex) {
        unassignedLayerChunk.layerWeights[4u * unassignedVertexCount + vertex] = 1.0f;
    }
    TERRAIN::ProjectTerrainChunkDeformableMask(layerSurface, unassignedLayerChunk, 1u, deformableMask);
    if (!std::all_of(deformableMask.begin(), deformableMask.end(), [](float value) { return Near(value, 0.0f); })) {
        return Fail("unassigned upper terrain layer is not deformable");
    }
    std::fill(unassignedLayerChunk.layerWeights.begin(), unassignedLayerChunk.layerWeights.end(), 0.0f);
    for (std::size_t vertex = 0u; vertex < unassignedVertexCount; ++vertex) {
        unassignedLayerChunk.layerWeights[vertex] = 1.0f;
    }
    TERRAIN::ProjectTerrainChunkDeformableMask(layerSurface, unassignedLayerChunk, 1u, deformableMask);
    if (!std::all_of(deformableMask.begin(), deformableMask.end(), [](float value) { return Near(value, 1.0f); })) {
        return Fail("base terrain layer uses surface preset");
    }

    // A whole-terrain material fill must touch only layer weights. Height and
    // coverage are authored geometry and must survive the operation intact.
    TERRAIN::TerrainChunk fillChunk = TERRAIN::MakeTerrainChunk(2u, 2u);
    const std::vector<float> fillHeights = {
        0.0f, 0.4f, 0.8f, 0.2f, 0.6f, 1.0f, 0.5f, 0.9f, 1.3f,
    };
    const std::vector<float> fillCoverage = {
        1.0f, 0.8f, 0.6f, 0.9f, 0.7f, 0.5f, 0.4f, 0.3f, 0.2f,
    };
    fillChunk.heights = fillHeights;
    fillChunk.coverage = fillCoverage;
    std::fill(fillChunk.layerWeights.begin(), fillChunk.layerWeights.end(), 0.0f);
    for (std::size_t vertex = 0u; vertex < fillHeights.size(); ++vertex) {
        fillChunk.layerWeights[vertex] = 0.25f;
        fillChunk.layerWeights[fillHeights.size() + vertex] = 0.75f;
    }
    if (!TERRAIN::FillTerrainChunkLayer(fillChunk, 1u) || fillChunk.heights != fillHeights ||
        fillChunk.coverage != fillCoverage ||
        !std::all_of(fillChunk.layerWeights.begin(), fillChunk.layerWeights.begin() + fillHeights.size(),
                     [](float value) { return Near(value, 0.0f); }) ||
        !std::all_of(fillChunk.layerWeights.begin() + fillHeights.size(), fillChunk.layerWeights.end(),
                     [](float value) { return Near(value, 1.0f); })) {
        return Fail("whole terrain fill preserves height and coverage");
    }

    // Paint expansion seeds a newly created chunk directly from the selected
    // layer; it must not leave the constructor's Sand/base weight behind.
    TERRAIN::TerrainChunk grassSeed = TERRAIN::MakeTerrainChunk(1u, 1u);
    if (!TERRAIN::FillTerrainChunkLayer(grassSeed, 3u) || grassSeed.layerCount != 4u ||
        !std::all_of(grassSeed.layerWeights.begin(), grassSeed.layerWeights.begin() + 4u,
                     [](float value) { return Near(value, 0.0f); }) ||
        !std::all_of(grassSeed.layerWeights.begin() + 12u, grassSeed.layerWeights.end(),
                     [](float value) { return Near(value, 1.0f); })) {
        return Fail("paint expansion selected layer seed");
    }

    // A non-material expansion inherits the nearest neighbour edge. Copying
    // only layer weights keeps the new chunk's coverage at the brush-created
    // zero until the stroke itself makes it visible.
    TERRAIN::TerrainChunk neighbour = TERRAIN::MakeTerrainChunk(1u, 2u);
    std::fill(neighbour.layerWeights.begin(), neighbour.layerWeights.end(), 0.0f);
    for (std::size_t vertex = 0u; vertex < 4u; ++vertex) {
        neighbour.layerWeights[4u + vertex] = 1.0f;
    }
    TERRAIN::TerrainChunk inherited = TERRAIN::MakeTerrainChunk(1u, 2u);
    std::fill(inherited.layerWeights.begin(), inherited.layerWeights.end(), 0.0f);
    std::fill(inherited.coverage.begin(), inherited.coverage.end(), 0.0f);
    if (!TERRAIN::CopyTerrainChunkEdgeToAll(neighbour, inherited, TERRAIN::TerrainChunkEdge::PositiveX) ||
        !std::all_of(inherited.layerWeights.begin(), inherited.layerWeights.begin() + 4u,
                     [](float value) { return Near(value, 0.0f); }) ||
        !std::all_of(inherited.layerWeights.begin() + 4u, inherited.layerWeights.end(),
                     [](float value) { return Near(value, 1.0f); }) ||
        !std::all_of(inherited.coverage.begin(), inherited.coverage.end(),
                     [](float value) { return Near(value, 0.0f); })) {
        return Fail("neighbor edge material inheritance");
    }

    // layerPresetIds を持たない solid preset は基底層の presetId を使う。
    // Grass / Rock を選んだ旧 scene を layer 0 の Sand へ戻してはならない。
    TERRAIN::TerrainChunk solidLayerChunk = TERRAIN::MakeTerrainChunk(1u, 1u);
    layerSurface.layerPresetIds.clear();
    layerSurface.presetId = "rock";
    TERRAIN::ProjectTerrainChunkDeformableMask(layerSurface, solidLayerChunk, 1u, deformableMask);
    if (!std::all_of(deformableMask.begin(), deformableMask.end(), [](float value) { return Near(value, 0.0f); })) {
        return Fail("solid rock base layer policy");
    }
    layerSurface.presetId = "grass";
    TERRAIN::ProjectTerrainChunkDeformableMask(layerSurface, solidLayerChunk, 1u, deformableMask);
    if (!std::all_of(deformableMask.begin(), deformableMask.end(), [](float value) { return Near(value, 0.0f); })) {
        return Fail("solid grass base layer policy");
    }
    layerSurface.presetId = "sand";

    layerSurface.layerPresetIds = {"grass"};
    if (TERRAIN::IsTerrainLayerDeformable(layerSurface, 0u)) {
        return Fail("hard layer policy revision before repaint");
    }
    layerSurface.layerPresetIds[0] = "sand";
    if (!TERRAIN::IsTerrainLayerDeformable(layerSurface, 0u)) {
        return Fail("deformable layer policy revision after repaint");
    }
    if (!Near(TERRAIN::EvaluateTerrainChunkFinalHeight(0.25f, -0.40f, 0.0f), 0.25f) ||
        !Near(TERRAIN::EvaluateTerrainChunkFinalHeight(0.25f, -0.40f, 0.5f), 0.05f) ||
        !Near(TERRAIN::EvaluateTerrainChunkFinalHeight(0.25f, -0.40f, 1.0f), -0.15f)) {
        return Fail("final masked height contract");
    }

    TERRAIN::TerrainChunkRuntimeState composedQueryState{};
    composedQueryState.cells = 1u;
    composedQueryState.cellSize = 1.0f;
    composedQueryState.baseHeights.assign(4u, 0.25f);
    composedQueryState.elasticHeights.assign(4u, -0.40f);
    composedQueryState.plasticHeights.assign(4u, 0.0f);
    composedQueryState.deformableMask.assign(4u, 0.0f);
    float composedHeight = 0.0f;
    if (!service.SampleChunkLocalHeight(composedQueryState, 0.5f, 0.5f, composedHeight) ||
        !Near(composedHeight, 0.25f)) {
        return Fail("CPU query hard-layer final height");
    }
    std::fill(composedQueryState.deformableMask.begin(), composedQueryState.deformableMask.end(), 0.5f);
    if (!service.SampleChunkLocalHeight(composedQueryState, 0.5f, 0.5f, composedHeight) ||
        !Near(composedHeight, 0.05f)) {
        return Fail("CPU query mixed-layer final height");
    }

    // Serialized chunk dimensions are validated before any allocation. This
    // remains the shared safety contract for terrain sculpting and grass.
    if (TERRAIN::kTerrainChunkMaxCells != 512u ||
        TERRAIN::TerrainChunkVertexCount((std::numeric_limits<uint32_t>::max)()) != 0u) {
        return Fail("terrain chunk hard limits");
    }
    TERRAIN::TerrainChunk malformedChunk{};
    malformedChunk.cells = (std::numeric_limits<uint32_t>::max)();
    malformedChunk.layerCount = 1u;
    if (TERRAIN::TerrainChunkValid(malformedChunk)) {
        return Fail("terrain chunk overflow rejection");
    }
    const TERRAIN::TerrainChunk clampedChunk = TERRAIN::MakeTerrainChunk((std::numeric_limits<uint32_t>::max)(), 1u);
    if (clampedChunk.cells != TERRAIN::kTerrainChunkMaxCells || !TERRAIN::TerrainChunkValid(clampedChunk)) {
        return Fail("terrain chunk construction clamp");
    }

    // Grass is an authored value contract; placement remains a deterministic
    // snapshot and must not become serialized scene objects.
    TERRAIN::TerrainGrassSettings grassSettings{};
    const TERRAIN::TerrainGrassSettings defaultGrassSettings{};
    if (TERRAIN::kTerrainGrassMaxClumps != 1024u || TERRAIN::kTerrainGrassMaxProjectedInstances != 2048u ||
        defaultGrassSettings.maxClumps != TERRAIN::kTerrainGrassMaxClumps / 2u ||
        TERRAIN::kTerrainGrassMaxClumps > TERRAIN::kTerrainGrassMaxProjectedInstances) {
        return Fail("grass renderer resident budget contract");
    }
    grassSettings.enabled = false;
    grassSettings.density = 4.0f;
    grassSettings.bladeHeight = -1.0f;
    grassSettings.bladeWidth = 4.0f;
    grassSettings.heightVariation = 2.0f;
    grassSettings.layerWeightThreshold = -1.0f;
    grassSettings.maxSlopeDegrees = 180.0f;
    grassSettings.windStrength = 2.0f;
    grassSettings.windSpeed = 40.0f;
    grassSettings.spatialFrequency = -1.0f;
    grassSettings.maxClumps = TERRAIN::kTerrainGrassMaxClumps + 1u;
    grassSettings.seed = 0x1122334455667788ull;
    grassSettings = TERRAIN::SanitizeTerrainGrassSettings(grassSettings);
    if (grassSettings.enabled || !Near(grassSettings.density, 1.0f) || !Near(grassSettings.bladeHeight, 0.005f) ||
        !Near(grassSettings.bladeWidth, 2.0f) || !Near(grassSettings.heightVariation, 1.0f) ||
        !Near(grassSettings.layerWeightThreshold, 0.0f) || !Near(grassSettings.maxSlopeDegrees, 90.0f) ||
        !Near(grassSettings.windStrength, 0.5f) || !Near(grassSettings.windSpeed, 20.0f) ||
        !Near(grassSettings.spatialFrequency, 0.0f) || grassSettings.maxClumps != TERRAIN::kTerrainGrassMaxClumps) {
        return Fail("grass sanitize bounds");
    }
    nlohmann::json grassJson = nlohmann::json::object();
    JSONFIELDS::Write(grassJson, grassSettings, TERRAIN::kTerrainGrassFields);
    TERRAIN::TerrainGrassSettings grassRoundTrip{};
    JSONFIELDS::Read(grassJson, grassRoundTrip, TERRAIN::kTerrainGrassFields);
    nlohmann::json grassRoundTripJson = nlohmann::json::object();
    JSONFIELDS::Write(grassRoundTripJson, grassRoundTrip, TERRAIN::kTerrainGrassFields);
    if (grassRoundTripJson != grassJson || grassJson.at("heightVariation") != grassSettings.heightVariation ||
        grassJson.at("seed") != grassSettings.seed || grassJson.contains("placements") ||
        grassJson.contains("transform")) {
        return Fail("grass authored settings round trip");
    }

    TERRAIN::TerrainSurface grassSurface{};
    grassSurface.presetId = "sand";
    grassSurface.chunkCells = 2u;
    grassSurface.chunkCellSize = 1.0f;
    grassSurface.layerPresetIds = {"grass"};
    TERRAIN::TerrainChunk grassChunk = TERRAIN::MakeTerrainChunk(2u, 1u);
    std::fill(grassChunk.layerWeights.begin(), grassChunk.layerWeights.end(), 1.0f);
    std::fill(grassChunk.coverage.begin(), grassChunk.coverage.end(), 1.0f);
    grassSurface.chunks.emplace(TERRAIN::TerrainChunkKey{0, 0}, grassChunk);
    grassSurface.grass.enabled = true;
    grassSurface.grass.density = 1.0f;
    grassSurface.grass.maxSlopeDegrees = 90.0f;
    grassSurface.grass.maxClumps = 16u;
    const auto grassPlacementsA = TERRAIN::BuildTerrainGrassPlacementSnapshot(
        grassSurface.grass, grassSurface.chunkCells, grassSurface.chunkCellSize, grassSurface.chunks,
        grassSurface.layerPresetIds, grassSurface.presetId);
    const auto grassPlacementsB = TERRAIN::BuildTerrainGrassPlacementSnapshot(
        grassSurface.grass, grassSurface.chunkCells, grassSurface.chunkCellSize, grassSurface.chunks,
        grassSurface.layerPresetIds, grassSurface.presetId);
    if (!grassPlacementsA.enabled || grassPlacementsA.grassLayerIndex != 0u ||
        grassPlacementsA.placements.size() != 4u ||
        grassPlacementsA.placements.size() != grassPlacementsB.placements.size()) {
        return Fail("grass deterministic 2x2 placement count");
    }
    for (std::size_t index = 0u; index < grassPlacementsA.placements.size(); ++index) {
        const auto& lhs = grassPlacementsA.placements[index];
        const auto& rhs = grassPlacementsB.placements[index];
        if (lhs.stableHash != rhs.stableHash || lhs.localPosition.x != rhs.localPosition.x ||
            lhs.localPosition.y != rhs.localPosition.y || lhs.localPosition.z != rhs.localPosition.z ||
            !TERRAIN::IsTerrainGrassPlacementValid(lhs)) {
            return Fail("grass deterministic placement values");
        }
    }

    TERRAIN::TerrainSurface noGrassSurface = grassSurface;
    noGrassSurface.presetId = "sand";
    noGrassSurface.layerPresetIds = {"sand"};
    const auto noGrassSnapshot = TERRAIN::BuildTerrainGrassPlacementSnapshot(
        noGrassSurface.grass, noGrassSurface.chunkCells, noGrassSurface.chunkCellSize, noGrassSurface.chunks,
        noGrassSurface.layerPresetIds, noGrassSurface.presetId);
    if (noGrassSnapshot.enabled || !noGrassSnapshot.placements.empty()) {
        return Fail("grass absent layer gate");
    }

    TERRAIN::TerrainSurface coveredOutSurface = grassSurface;
    std::fill(coveredOutSurface.chunks.begin()->second.coverage.begin(),
              coveredOutSurface.chunks.begin()->second.coverage.end(), 0.0f);
    const auto coverageGateSnapshot = TERRAIN::BuildTerrainGrassPlacementSnapshot(
        coveredOutSurface.grass, coveredOutSurface.chunkCells, coveredOutSurface.chunkCellSize,
        coveredOutSurface.chunks, coveredOutSurface.layerPresetIds, coveredOutSurface.presetId);
    if (!coverageGateSnapshot.enabled || !coverageGateSnapshot.placements.empty()) {
        return Fail("grass coverage gate");
    }

    TERRAIN::TerrainSurface slopeSurface = grassSurface;
    slopeSurface.grass.maxSlopeDegrees = 0.0f;
    const std::vector<float> steepHeights = {
        0.0f, 1.0f, 2.0f, 0.0f, 1.0f, 2.0f, 0.0f, 1.0f, 2.0f,
    };
    slopeSurface.chunks.begin()->second.heights = steepHeights;
    const auto slopeGateSnapshot = TERRAIN::BuildTerrainGrassPlacementSnapshot(
        slopeSurface.grass, slopeSurface.chunkCells, slopeSurface.chunkCellSize, slopeSurface.chunks,
        slopeSurface.layerPresetIds, slopeSurface.presetId);
    if (!slopeGateSnapshot.enabled || !slopeGateSnapshot.placements.empty()) {
        return Fail("grass slope gate");
    }

    TERRAIN::TerrainSurface cappedGrassSurface = grassSurface;
    cappedGrassSurface.grass.maxClumps = 2u;
    const auto cappedGrassSnapshot = TERRAIN::BuildTerrainGrassPlacementSnapshot(
        cappedGrassSurface.grass, cappedGrassSurface.chunkCells, cappedGrassSurface.chunkCellSize,
        cappedGrassSurface.chunks, cappedGrassSurface.layerPresetIds, cappedGrassSurface.presetId);
    if (cappedGrassSnapshot.placements.size() != 2u) {
        return Fail("grass max clumps cap");
    }

    auto makeCoveredGrassChunk = []() {
        TERRAIN::TerrainChunk chunk = TERRAIN::MakeTerrainChunk(4u, 1u);
        std::fill(chunk.layerWeights.begin(), chunk.layerWeights.end(), 1.0f);
        std::fill(chunk.coverage.begin(), chunk.coverage.end(), 1.0f);
        return chunk;
    };
    const TERRAIN::TerrainChunk distributedGrassChunk = makeCoveredGrassChunk();
    TERRAIN::TerrainChunkMap distributedChunksA;
    distributedChunksA.emplace(TERRAIN::TerrainChunkKey{-2, -1}, distributedGrassChunk);
    distributedChunksA.emplace(TERRAIN::TerrainChunkKey{-1, 2}, distributedGrassChunk);
    distributedChunksA.emplace(TERRAIN::TerrainChunkKey{1, -2}, distributedGrassChunk);
    distributedChunksA.emplace(TERRAIN::TerrainChunkKey{2, 1}, distributedGrassChunk);
    TERRAIN::TerrainChunkMap distributedChunksB;
    distributedChunksB.emplace(TERRAIN::TerrainChunkKey{2, 1}, distributedGrassChunk);
    distributedChunksB.emplace(TERRAIN::TerrainChunkKey{1, -2}, distributedGrassChunk);
    distributedChunksB.emplace(TERRAIN::TerrainChunkKey{-1, 2}, distributedGrassChunk);
    distributedChunksB.emplace(TERRAIN::TerrainChunkKey{-2, -1}, distributedGrassChunk);
    TERRAIN::TerrainGrassSettings distributedGrassSettings = grassSurface.grass;
    distributedGrassSettings.seed = 0xA17E5EEDull;
    distributedGrassSettings.maxClumps = 5u;
    const auto distributedGrassSnapshotA = TERRAIN::BuildTerrainGrassPlacementSnapshot(
        distributedGrassSettings, grassSurface.chunkCells, grassSurface.chunkCellSize, distributedChunksA,
        grassSurface.layerPresetIds, grassSurface.presetId);
    const auto distributedGrassSnapshotB = TERRAIN::BuildTerrainGrassPlacementSnapshot(
        distributedGrassSettings, grassSurface.chunkCells, grassSurface.chunkCellSize, distributedChunksB,
        grassSurface.layerPresetIds, grassSurface.presetId);
    TERRAIN::TerrainGrassSettings distributedGrassAllSettings = distributedGrassSettings;
    distributedGrassAllSettings.maxClumps = 64u;
    const auto distributedGrassAllSnapshot = TERRAIN::BuildTerrainGrassPlacementSnapshot(
        distributedGrassAllSettings, grassSurface.chunkCells, grassSurface.chunkCellSize, distributedChunksA,
        grassSurface.layerPresetIds, grassSurface.presetId);
    if (distributedGrassSnapshotA.placements.size() != 5u ||
        distributedGrassSnapshotB.placements.size() != distributedGrassSnapshotA.placements.size() ||
        distributedGrassAllSnapshot.placements.size() != 64u) {
        return Fail("grass distributed budget count");
    }
    bool selectedBeyondFirstChunk = false;
    for (const auto& placement : distributedGrassSnapshotA.placements) {
        if (!(placement.chunk == distributedChunksA.begin()->first)) {
            selectedBeyondFirstChunk = true;
            break;
        }
    }
    if (!selectedBeyondFirstChunk) {
        return Fail("grass distributed budget chunk fairness");
    }
    for (std::size_t index = 0u; index < distributedGrassSnapshotA.placements.size(); ++index) {
        const auto& lhs = distributedGrassSnapshotA.placements[index];
        const auto& rhs = distributedGrassSnapshotB.placements[index];
        const auto& full = distributedGrassAllSnapshot.placements[index];
        if (lhs.stableHash != rhs.stableHash || !(lhs.chunk == rhs.chunk) || lhs.cellX != rhs.cellX ||
            lhs.cellZ != rhs.cellZ || lhs.stableHash != full.stableHash || !(lhs.chunk == full.chunk) ||
            lhs.cellX != full.cellX || lhs.cellZ != full.cellZ ||
            (index > 0u && distributedGrassSnapshotA.placements[index - 1u].stableHash > lhs.stableHash)) {
            return Fail("grass distributed budget deterministic order");
        }
    }

    // Procedural authoring is an editor bake over the same heightfield. Its
    // evaluator must remain deterministic, bounded, and independent from the
    // shipped surface preset values.
    TERRAIN::TerrainProceduralSpec procedural{};
    procedural.mode = TERRAIN::TerrainProceduralMode::NoiseHills;
    procedural.seed = 0x12345678ull;
    procedural.centerX = 0.0f;
    procedural.centerZ = 0.0f;
    procedural.extentX = 4.0f;
    procedural.extentZ = 2.0f;
    procedural.amplitude = 1.5f;
    procedural.scale = 2.0f;
    procedural.octaves = 4u;
    const float proceduralA = TERRAIN::EvaluateTerrainProceduralHeight(procedural, 1.25f, -0.5f);
    const float proceduralB = TERRAIN::EvaluateTerrainProceduralHeight(procedural, 1.25f, -0.5f);
    if (!Near(proceduralA, proceduralB, 1.0e-7f) ||
        TERRAIN::EvaluateTerrainProceduralMask(procedural, 4.1f, 0.0f) != 0.0f ||
        TERRAIN::EvaluateTerrainProceduralMask(procedural, 0.0f, 0.0f) <= 0.0f) {
        return Fail("procedural deterministic evaluator bounds");
    }
    const TERRAIN::TerrainProceduralMode proceduralModes[] = {
        TERRAIN::TerrainProceduralMode::NoiseHills, TERRAIN::TerrainProceduralMode::IslandCoast,
        TERRAIN::TerrainProceduralMode::Ridge,      TERRAIN::TerrainProceduralMode::BasinCrater,
        TERRAIN::TerrainProceduralMode::Dunes,
    };
    for (const TERRAIN::TerrainProceduralMode mode : proceduralModes) {
        procedural.mode = mode;
        const float modeHeight = TERRAIN::EvaluateTerrainProceduralHeight(procedural, 0.0f, 0.0f);
        if (!std::isfinite(modeHeight) ||
            !Near(modeHeight, TERRAIN::EvaluateTerrainProceduralHeight(procedural, 0.0f, 0.0f), 1.0e-7f)) {
            return Fail("procedural profile mode determinism");
        }
    }
    procedural.mode = TERRAIN::TerrainProceduralMode::IslandCoast;
    if (TERRAIN::EvaluateTerrainProceduralMask(procedural, 4.0f, 0.0f) != 0.0f ||
        TERRAIN::EvaluateTerrainProceduralMask(procedural, 0.0f, 0.0f) < 0.99f) {
        return Fail("procedural island coast mask bounds");
    }
    TERRAIN::TerrainProceduralSpec singleMaskA = procedural;
    singleMaskA.mode = TERRAIN::TerrainProceduralMode::Ridge;
    singleMaskA.extentX = 4.0f;
    singleMaskA.extentZ = 4.0f;
    singleMaskA.edgeFalloff = 0.50f;
    TERRAIN::TerrainProceduralSpec singleMaskB = singleMaskA;
    singleMaskB.edgeFalloff = 0.25f;
    const float singleMaskHeightA = TERRAIN::EvaluateTerrainProceduralHeight(singleMaskA, 3.5f, 0.0f);
    const float singleMaskHeightB = TERRAIN::EvaluateTerrainProceduralHeight(singleMaskB, 3.5f, 0.0f);
    if (TERRAIN::EvaluateTerrainProceduralMask(singleMaskA, 3.5f, 0.0f) <= 0.0f ||
        TERRAIN::EvaluateTerrainProceduralMask(singleMaskB, 3.5f, 0.0f) <= 0.0f ||
        !Near(singleMaskHeightA, singleMaskHeightB, 1.0e-6f)) {
        return Fail("procedural edge mask applied exactly once");
    }
    TERRAIN::TerrainSurface proceduralSurface{};
    proceduralSurface.presetId = "sand";
    proceduralSurface.chunkCells = 4u;
    proceduralSurface.chunkCellSize = 0.5f;
    proceduralSurface.chunks.emplace(TERRAIN::TerrainChunkKey{0, 0}, TERRAIN::MakeTerrainChunk(4u, 1u));
    const std::string presetBefore = proceduralSurface.presetId;
    const auto proceduralPreview = TERRAIN::PreviewTerrainProcedural(proceduralSurface, procedural);
    const auto proceduralPreviewRepeat = TERRAIN::PreviewTerrainProcedural(proceduralSurface, procedural);
    if (!proceduralPreview.valid || proceduralPreview.affectedChunks == 0u ||
        proceduralPreview.affectedVertices == 0u ||
        proceduralPreview.affectedChunks != proceduralPreviewRepeat.affectedChunks ||
        proceduralPreview.affectedVertices != proceduralPreviewRepeat.affectedVertices ||
        proceduralSurface.presetId != presetBefore || proceduralSurface.chunks.size() != 1u) {
        return Fail("procedural bounded preview and preset invariance");
    }
    // A narrow island can touch an interior authored vertex while missing all
    // chunk corners and the chunk center. Candidate discovery must use the
    // same exact grid as Preview/Apply, not a corner heuristic.
    TERRAIN::TerrainSurface narrowSurface{};
    narrowSurface.chunkCells = 4u;
    narrowSurface.chunkCellSize = 0.5f;
    TERRAIN::TerrainProceduralSpec narrowProcedural{};
    narrowProcedural.mode = TERRAIN::TerrainProceduralMode::IslandCoast;
    narrowProcedural.centerX = 0.5f;
    narrowProcedural.centerZ = 0.5f;
    narrowProcedural.extentX = 0.5f;
    narrowProcedural.extentZ = 0.5f;
    narrowProcedural.edgeFalloff = 0.25f;
    const auto narrowPreview = TERRAIN::PreviewTerrainProcedural(narrowSurface, narrowProcedural);
    if (!narrowPreview.valid || narrowPreview.affectedChunks != 1u ||
        narrowPreview.newChunks != narrowPreview.affectedChunks || narrowPreview.affectedVertices == 0u) {
        return Fail("procedural interior vertex candidate bounds");
    }
    TERRAIN::TerrainProceduralSpec zeroBlend = narrowProcedural;
    zeroBlend.blendAmount = 0.0f;
    const auto zeroBlendPreview = TERRAIN::PreviewTerrainProcedural(narrowSurface, zeroBlend);
    if (zeroBlendPreview.valid || zeroBlendPreview.budgetExceeded || zeroBlendPreview.affectedChunks != 0u ||
        zeroBlendPreview.affectedVertices != 0u) {
        return Fail("procedural zero blend no-op admission");
    }
    TERRAIN::TerrainSurface tinyCellSurface = narrowSurface;
    tinyCellSurface.chunkCellSize = 0.005f;
    TERRAIN::TerrainSurface minimumCellSurface = narrowSurface;
    minimumCellSurface.chunkCellSize = TERRAIN::kTerrainMinimumChunkCellSize;
    const auto tinyCellPreview = TERRAIN::PreviewTerrainProcedural(tinyCellSurface, narrowProcedural);
    const auto minimumCellPreview = TERRAIN::PreviewTerrainProcedural(minimumCellSurface, narrowProcedural);
    if (tinyCellPreview.affectedChunks != minimumCellPreview.affectedChunks ||
        tinyCellPreview.affectedVertices != minimumCellPreview.affectedVertices ||
        !Near(TERRAIN::SanitizeTerrainChunkCellSize(std::numeric_limits<float>::quiet_NaN()), 0.25f)) {
        return Fail("procedural effective chunk grid parity");
    }
    TERRAIN::TerrainProceduralSpec oversizedProcedural = procedural;
    oversizedProcedural.extentX = 2048.0f;
    oversizedProcedural.extentZ = 2048.0f;
    const auto oversizedPreview = TERRAIN::PreviewTerrainProcedural(proceduralSurface, oversizedProcedural);
    if (oversizedPreview.valid || !oversizedPreview.budgetExceeded ||
        oversizedPreview.candidateVertices <= TERRAIN::kTerrainProceduralMaximumCandidateVertices) {
        return Fail("procedural reversible bake admission budget");
    }
    TERRAIN::TerrainProceduralMode parsedMode{};
    TERRAIN::TerrainProceduralBlendMode parsedBlend{};
    if (!TEXT::TryParseEnum(TERRAIN::kTerrainProceduralModeNames, "ridge", parsedMode) ||
        parsedMode != TERRAIN::TerrainProceduralMode::Ridge ||
        TEXT::TryParseEnum(TERRAIN::kTerrainProceduralModeNames, "rige", parsedMode) ||
        !TEXT::TryParseEnum(TERRAIN::kTerrainProceduralBlendModeNames, "replace", parsedBlend) ||
        parsedBlend != TERRAIN::TerrainProceduralBlendMode::Replace ||
        TEXT::TryParseEnum(TERRAIN::kTerrainProceduralBlendModeNames, "replce", parsedBlend)) {
        return Fail("procedural strict operation enum parsing");
    }
    procedural.extentX = -100.0f;
    procedural.octaves = 99u;
    procedural.mode = static_cast<TERRAIN::TerrainProceduralMode>(255u);
    procedural.blendMode = static_cast<TERRAIN::TerrainProceduralBlendMode>(255u);
    procedural = TERRAIN::SanitizeTerrainProceduralSpec(procedural);
    if (!Near(procedural.extentX, 0.5f) || procedural.octaves != 8u ||
        procedural.mode != TERRAIN::TerrainProceduralMode::NoiseHills ||
        procedural.blendMode != TERRAIN::TerrainProceduralBlendMode::Add) {
        return Fail("procedural profile sanitization");
    }

    service.Clear();
    if (service.GetSnapshotRevision() != 2u || service.GetSurfaceCount() != 0u ||
        service.SampleWorldHeight({12.0f, 8.1f, -3.5f}, worldHeight, worldNormal)) {
        return Fail("snapshot clear contract");
    }

    std::cout << "Terrain surface query contracts passed.\n";
    return 0;
}
