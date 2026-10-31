#include <algorithm>
#include <cmath>
#include <thread>
#include "HIKARI/Project/ActionViews.h"
#include "HIKARI/Project/WorldView.h"
#include "Runtime/Project/Internal/HIKARI_GameProjectAuthoringInternal.h"
#include "Scene/HIKARI_World.h"
#include "Scene/HIKARI_PresentationModelService.h"
#include "Scene/Components/Rendering/Model/HIKARI_ModelComponent.h"
#include "Scene/Vfx/HIKARI_CombatAfterimageService.h"
#include "Scene/Vfx/HIKARI_AfterimageSnapshot.h"
#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Material/HIKARI_GpuMaterialTypes.h"
#include "Internal/HIKARI_ContractAssert.h"

namespace {
void AssertMatrixNear(const HIKARI::MATH::Mat4& actual, const HIKARI::MATH::Mat4& expected) {
    for (int column = 0; column < 4; ++column)
        for (int row = 0; row < 4; ++row)
            assert(std::abs(actual.m[column][row] - expected.m[column][row]) < 0.0001f);
}

void CheckFrozenWorldTransform(HIKARI::RENDER3D::RUNTIME::SceneRenderObjectDesc source) {
    using namespace HIKARI;
    World hierarchy;
    auto* parent = hierarchy.CreateObject("SnapshotParent");
    auto* nextParent = hierarchy.CreateObject("ReplacementParent");
    auto* child = hierarchy.CreateObject("SnapshotChild");
    assert(parent->SetLocalPosition({3, -2, 7}));
    assert(parent->SetLocalRotation(MATH::Quat::FromEulerXYZ(0.2f, 0.7f, -0.4f)));
    assert(parent->SetLocalScale({2, 3, 0.5f}));
    assert(child->SetParent(parent));
    assert(child->SetLocalPosition({1, 4, -2}));
    assert(child->SetLocalRotation(MATH::Quat::FromEulerXYZ(-0.3f, 0.1f, 0.6f)));
    assert(child->SetLocalScale({0.75f, 1.25f, 2}));
    source.worldTransform = child->GetTransform();
    const auto historicalWorld = source.worldTransform.GetWorldMatrix();
    VFX::AFTERIMAGE::SnapshotCache cache;
    VFX::AFTERIMAGE::Part inherited, explicitPart;
    assert(cache.Capture(source, inherited));

    // An explicit transform is already world-space, even with a non-null parent and unrelated TRS.
    source.worldTransform.useExplicitMatrix = true;
    source.worldTransform.explicitMatrix =
        MATH::Mat4::TRS({-5, 8, 2}, MATH::Quat::FromEulerXYZ(0.4f, -0.8f, 0.2f), {4, 0.5f, 2});
    source.worldTransform.explicitMatrix.m[1][0] += 0.35f;
    const auto explicitWorld = source.worldTransform.GetWorldMatrix();
    assert(cache.Capture(source, explicitPart));
    const auto checkFrozen = [&] {
        assert(inherited.desc.worldTransform.GetParent() == nullptr);
        assert(explicitPart.desc.worldTransform.GetParent() == nullptr);
        assert(inherited.desc.worldTransform.useExplicitMatrix && explicitPart.desc.worldTransform.useExplicitMatrix);
        AssertMatrixNear(inherited.desc.worldTransform.GetWorldMatrix(), historicalWorld);
        AssertMatrixNear(explicitPart.desc.worldTransform.GetWorldMatrix(), explicitWorld);
    };
    checkFrozen();
    assert(parent->SetLocalPosition({30, 5, -10}));
    checkFrozen();
    assert(parent->SetLocalRotation(MATH::Quat::FromEulerXYZ(-0.6f, 1.2f, 0.9f)));
    checkFrozen();
    assert(parent->SetLocalScale({0.25f, 5, 3}));
    checkFrozen();
    assert(nextParent->SetLocalPosition({-100, 20, 40}));
    assert(child->SetParent(nextParent));
    checkFrozen();
    assert(child->SetLocalPosition({-3, 9, 12}));
    assert(child->SetLocalRotation(MATH::Quat::FromEulerXYZ(1, 2, 3)));
    assert(child->SetLocalScale({6, 2, 0.1f}));
    checkFrozen();
    hierarchy.DestroyObject(parent->GetRuntimeHandle());
    checkFrozen();
}
} // namespace

void CheckAfterimageContracts() {
    using namespace HIKARI;
    using namespace HIKARI::RENDER3D::RUNTIME;
    World world;
    VFX::CombatAfterimageService afterimages;
    PresentationModelService clothes;
    assert(world.Services().Register(afterimages));
    assert(world.Services().Register(clothes));
    auto* object = world.CreateObject("AfterimageOwner");
    const auto owner = object->GetRuntimeHandle();
    auto* component = object->AddComponent<ModelComponent>();
    ModelAsset model;
    model.SetState(ModelAsset::State::Loaded);
    model.SetName("AfterimageFixture");
    model.materials.resize(1);
    model.materials[0].alphaMode = AlphaMode::Mask;
    model.nodes.resize(2);
    model.nodes[0].meshIndex = 0;
    model.nodes[0].skinIndex = 0;
    model.nodes[1].meshIndex = 0;
    model.nodes[1].skinIndex = 0;
    model.meshes.resize(1);
    auto& primitive = model.meshes[0].primitives.emplace_back();
    primitive.layout = VertexLayoutKind::SkinnedPNTTJW;
    primitive.skinnedVertices.resize(3);
    primitive.skinnedVertices[0].position = {-1, 0, 0};
    primitive.skinnedVertices[1].position = {1, 0, 0};
    primitive.skinnedVertices[2].position = {0, 2, 0};
    for (auto& vertex : primitive.skinnedVertices)
        vertex.weights[0] = 1;
    primitive.indices = {0, 1, 2};
    model.skins.resize(1);
    model.skins[0].joints.resize(1);
    model.skins[0].joints[0].nodeIndex = 0;
    model.skins[0].joints[0].inverseBindMatrix = MATH::Mat4::Identity();
    component->SetModelAsset(&model);
    RenderModelAsset renderModel;
    assert(BuildRenderModelAsset(model, renderModel));
    auto pose = std::make_shared<ANIMATION::AnimationPoseSnapshot>();
    pose->valid = true;
    pose->revision = 7;
    pose->localPose.nodes.resize(2);
    pose->localPose.nodes[0].position.x = 2;
    SceneRenderCache scene;
    scene.BeginSync(1);
    SceneRenderObjectDesc body;
    body.id = {object->GetRenderStableId()};
    body.model = &model;
    body.renderModel = &renderModel;
    body.animationPose = pose;
    body.animationPoseRevision = pose->revision;
    body.hasRuntimeAnimation = true;
    body.nodeVisibilityMask = {1, 0};
    CheckFrozenWorldTransform(body);
    scene.Upsert(body);
    PresentationModelSubmission clothing;
    clothing.ownerObjectHandle = owner.ToValue();
    clothing.instanceId = 1;
    clothing.assetGuid = "00000000000000000000000000000001";
    assert(clothes.Submit(clothing, 1));
    clothes.PrepareForRender(1, {});
    assert(clothes.GetFrameRecords().size() == 1);
    auto garment = body;
    garment.id = {clothes.GetFrameRecords()[0].renderObjectId};
    garment.worldTransform.position.x = 4;
    garment.nodeVisibilityMask = {0, 1};
    scene.Upsert(garment);
    scene.EndSync();
    scene.PreRenderSync();

    VFX::CombatAfterimageRequest request;
    request.ownerObject = owner;
    request.sourceId = 51;
    request.frameIndex = 1;
    request.silhouetteCount = 3;
    request.lifetimeUnscaledSeconds = 0.12f;
    assert(afterimages.Capture(world, request));
    assert(afterimages.GetSampleCount(owner, 51) == 0); // deferred, not the preceding pose
    pose->localPose.nodes[0].position.x = 3;
    afterimages.Tick(1, 0.016f);
    afterimages.Publish(world, scene, 1);
    assert(afterimages.GetSampleCount(owner, 51) == 1); // never synthesize extra layers
    size_t snapshots = 0;
    for (const auto& item : scene.GetObjects()) {
        if (item.desc.id == body.id || item.desc.id == garment.id)
            continue;
        ++snapshots;
        assert(!item.desc.castShadow && !item.desc.receiveShadow);
        assert(item.desc.animationPose->localPose.nodes[0].position.x == 3);
        assert(item.desc.model != &model && item.desc.model->meshes[0].primitives[0].indices.size() == 3);
        assert(item.desc.nodeVisibilityMask ==
               (item.desc.worldTransform.GetWorldMatrix().TransformPoint({0, 0, 0, 1}).x == 4
                    ? garment.nodeVisibilityMask
                    : body.nodeVisibilityMask));
        assert(item.desc.materialOverrides.size() == 1);
        const auto* material = item.desc.materialOverrides[0].material;
        assert(material->GetMaterialAlphaMode() == RuntimeMaterialAlphaMode::Blend);
        assert((material->GetMaterialProgramInstanceFlags() &
                RENDER3D::MATERIAL::GPU_MATERIAL_PROGRAM_FLAGS::AlphaMask) != 0);
    }
    assert(snapshots == 2); // body plus only the current garment
    pose->localPose.nodes[0].position.x = 20;
    model.meshes[0].primitives[0].indices.clear(); // captured geometry survives reload
    assert(afterimages.SetPaused(owner, 51, true));
    request.frameIndex = 2;
    assert(!afterimages.Capture(world, request));
    afterimages.Tick(2, 0.25f);
    afterimages.Publish(world, scene, 2);
    assert(afterimages.GetSampleCount(owner, 51) == 1);
    for (const auto& item : scene.GetObjects())
        if (item.desc.id != body.id && item.desc.id != garment.id)
            assert(item.desc.animationPose->localPose.nodes[0].position.x == 3);

    const auto view = GameProjectSdkAccess::MakeWorldView(world).GetPresentationVfx();
    assert(!view.SetAfterimagePaused(owner.ToValue(), 999, true));
    assert(!view.ClearAfterimages(owner.ToValue(), 0));
    bool wrongThread = true;
    std::thread other([&] { wrongThread = view.ClearAfterimages(owner.ToValue(), 51); });
    other.join();
    assert(!wrongThread);
    assert(view.SetAfterimagePaused(owner.ToValue(), 51, false));
    afterimages.Tick(3, 0.08f);
    afterimages.Publish(world, scene, 3);
    assert(afterimages.GetSampleCount(owner, 51) == 1);
    assert(view.ClearAfterimages(owner.ToValue(), 51));
    assert(view.ClearAfterimages(owner.ToValue(), 51));
    afterimages.Publish(world, scene, 3);
    assert(scene.GetObjects().size() == 2);
    request.frameIndex = 4;
    request.silhouetteCount = 1;
    assert(afterimages.Capture(world, request));
    assert(view.ClearAfterimages(owner.ToValue(), 51));
    afterimages.Publish(world, scene, 4);
    assert(afterimages.GetSampleCount(owner, 51) == 0);
    assert(scene.GetObjects().size() == 2);

    // Scale all parts about the owner's origin, including garment offsets.
    primitive.indices = {0, 1, 2};
    body.worldTransform.useExplicitMatrix = true;
    body.worldTransform.explicitMatrix =
        MATH::Mat4::TRS({3, 2, -1}, MATH::Quat::FromEulerXYZ(0.2f, -0.7f, 0.4f), {2, 3, 0.5f});
    garment.worldTransform.useExplicitMatrix = true;
    garment.worldTransform.explicitMatrix =
        body.worldTransform.explicitMatrix * MATH::Mat4::TRS({4, 0, 0}, MATH::Quat::Identity(), {1, 1, 1});
    scene.Upsert(body);
    scene.Upsert(garment);
    const auto scaleAdjustment = MATH::Mat4::TRS({3, 2, -1}, MATH::Quat::Identity(), {2, 2, 2}) *
                                 MATH::Mat4::TRS({-3, -2, 1}, MATH::Quat::Identity(), {1, 1, 1});
    request.uniformScale = 2;
    request.frameIndex = 5;
    assert(afterimages.Capture(world, request));
    afterimages.Publish(world, scene, 5);
    size_t scaledGarments = 0;
    for (const auto& item : scene.GetObjects()) {
        if (item.desc.id == body.id || item.desc.id == garment.id)
            continue;
        const auto matrix = item.desc.worldTransform.GetWorldMatrix();
        assert(item.desc.worldTransform.GetParent() == nullptr);
        if (item.desc.nodeVisibilityMask == garment.nodeVisibilityMask) {
            AssertMatrixNear(matrix, scaleAdjustment * garment.worldTransform.GetWorldMatrix());
            ++scaledGarments;
        } else {
            AssertMatrixNear(matrix, scaleAdjustment * body.worldTransform.GetWorldMatrix());
        }
    }
    assert(scaledGarments == 1);
    assert(view.ClearAfterimages(owner.ToValue(), 51));
    afterimages.Publish(world, scene, 5);

    // Retired history and staging must release leases beyond both cache budgets.
    request.uniformScale = 1;
    for (uint64_t frame = 6; frame < 86; ++frame) {
        ++model.sourceRevision;
        request.frameIndex = frame;
        assert(afterimages.Capture(world, request));
        afterimages.Publish(world, scene, frame);
        assert(afterimages.GetSampleCount(owner, 51) == 1);
        assert(view.ClearAfterimages(owner.ToValue(), 51));
        afterimages.Publish(world, scene, frame);
        assert(scene.GetObjects().size() == 2);
    }
    // A full history contains distinct real captures and stops at five.
    request.silhouetteCount = 5;
    for (uint64_t frame = 86; frame < 93; ++frame) {
        request.frameIndex = frame;
        pose->localPose.nodes[0].position.x = static_cast<float>(frame);
        ++pose->revision;
        assert(afterimages.Capture(world, request));
        afterimages.Publish(world, scene, frame);
        assert(afterimages.GetSampleCount(owner, 51) == (std::min)(size_t(frame - 85), size_t(5)));
    }
    assert(scene.GetObjects().size() == 12);
    for (const auto& item : scene.GetObjects())
        if (item.desc.id != body.id && item.desc.id != garment.id)
            assert(item.desc.animationPose->localPose.nodes[0].position.x >= 88);
    afterimages.Tick(93, 0.13f);
    afterimages.Publish(world, scene, 93);
    assert(afterimages.GetSampleCount(owner, 51) == 0);
    assert(scene.GetObjects().size() == 2);

    request.frameIndex = 94;
    assert(afterimages.Capture(world, request));
    auto independent = request;
    independent.sourceId = 52;
    assert(afterimages.Capture(world, independent));
    afterimages.Publish(world, scene, 94);
    assert(view.ClearAfterimages(owner.ToValue(), 51));
    afterimages.Publish(world, scene, 94);
    assert(afterimages.GetSampleCount(owner, 52) == 1);
    World otherWorld;
    VFX::CombatAfterimageService otherAfterimages;
    assert(otherWorld.Services().Register(otherAfterimages));
    auto* otherOwner = otherWorld.CreateObject("IndependentWorld");
    const auto otherView = GameProjectSdkAccess::MakeWorldView(otherWorld).GetPresentationVfx();
    assert(otherView.ClearAfterimages(otherOwner->GetRuntimeHandle().ToValue(), 52));
    assert(afterimages.GetSampleCount(owner, 52) == 1);
    world.DestroyObject(owner);
    afterimages.Publish(world, scene, 95);
    assert(afterimages.GetActiveCount() == 0);
    assert(scene.GetObjects().size() == 2);
    assert(!view.ClearAfterimages(owner.ToValue(), 52));
}
