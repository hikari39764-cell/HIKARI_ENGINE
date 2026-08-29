#include "Scene/Water/HIKARI_WaterSurfaceSystem.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "Core/Math/HIKARI_NormalMatrix.h"
#include "Render3D/Procedural/HIKARI_ProceduralMeshTypes.h"
#include "Scene/Components/HIKARI_ProceduralMeshComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Water/HIKARI_WaterSurfaceComponent.h"
#include "Scene/Water/HIKARI_WaterSurfaceQueryService.h"

namespace HIKARI {

    void WaterSurfaceSystem::OnWorldAttached(World& world) {
        queryService_ =
            world.Services().Find<WATER::WaterSurfaceQueryService>();
        if (queryService_ != nullptr) {
            queryService_->Clear();
            Refresh(world);
        }
    }

    void WaterSurfaceSystem::OnWorldDetached(World&) {
        if (queryService_ != nullptr) {
            queryService_->Clear();
        }
        queryService_ = nullptr;
    }

    void WaterSurfaceSystem::PreFixedUpdate(
        World& world,
        const FrameContext&) {
        Refresh(world);
    }

    void WaterSurfaceSystem::PreRender(
        World& world,
        const FrameContext&) {
        // Editor 停止中は fixed step が走らないため、描画前にも同じ
        // snapshot publisher を通して authoring 変更を反映する。
        Refresh(world);
    }

    void WaterSurfaceSystem::Refresh(World& world) {
        if (queryService_ == nullptr) {
            return;
        }

        std::vector<WATER::WaterSurfaceSnapshotEntry> surfaces{};
        surfaces.reserve(world.GetObjects().size());
        const auto publishSurface = [&](GameObject& object,
            const WATER::WaterSurfaceSettings& authored,
            ProceduralMeshComponent& procedural) {

            const ProceduralMeshSettings mesh =
                SanitizeProceduralMeshSettings(
                    procedural.GetSettings());
            if (mesh.kind != ProceduralMeshKind::Plane &&
                mesh.kind != ProceduralMeshKind::GridPlane) {
                return;
            }

            const float spacingX = mesh.width /
                static_cast<float>((std::max)(mesh.segmentsX, 1u));
            const float spacingZ = mesh.depth /
                static_cast<float>((std::max)(mesh.segmentsY, 1u));
            WATER::WaterSurfaceSnapshotEntry snapshot{};
            snapshot.object = object.GetRuntimeHandle();
            snapshot.world = object.GetTransform().GetWorldMatrix();
            snapshot.inverseWorld = MATH::Inverse(snapshot.world);
            snapshot.normalMatrix =
                MATH::BuildNormalMatrixFromWorld(snapshot.world);
            snapshot.settings =
                WATER::ResolveWaterSurfaceRuntimeSettings(
                    authored,
                    (std::max)(spacingX, spacingZ),
                    snapshot.world);
            snapshot.halfExtents = {
                mesh.width * 0.5f,
                mesh.depth * 0.5f
            };
            snapshot.maximumHorizontalDisplacement =
                WATER::CalculateWaterMaximumHorizontalDisplacement(
                    snapshot.settings);
            surfaces.push_back(std::move(snapshot));
        };

        world.ForEachObjectWith<
            WaterSurfaceComponent,
            ProceduralMeshComponent>(
            [&](GameObject& object,
                WaterSurfaceComponent& water,
                ProceduralMeshComponent& procedural) {
                publishSurface(object, water.GetSettings(), procedural);
            });
        queryService_->PublishSnapshot(std::move(surfaces));
    }

} // namespace HIKARI
