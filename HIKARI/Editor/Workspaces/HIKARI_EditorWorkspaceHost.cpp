#include "HIKARI_EditorWorkspaceHost.h"

#include <utility>

namespace HIKARI::EDITOR {

EditorWorkspaceHost::EditorWorkspaceHost() {
    EditorViewInstance& game = View(BuiltInViewSlot::CinematicsGame);
    game.renderViewId = RENDER3D::kPrimaryRenderViewId;
    game.role = EditorViewRole::Game;
    game.purpose = RENDER3D::RenderViewPurpose::Game;
    game.cameraBinding.kind = EditorViewCameraSourceKind::SceneDirector;
    game.execution = EditorViewExecutionMode::PrimaryFullQuality;

    EditorViewInstance& director = View(BuiltInViewSlot::CinematicsDirector);
    director.renderViewId = kEditorDirectorRenderViewId;
    director.role = EditorViewRole::Director;
    director.purpose = RENDER3D::RenderViewPurpose::EditorScene;
    director.cameraBinding.kind = EditorViewCameraSourceKind::OwnedEditorCamera;
    director.execution = EditorViewExecutionMode::SecondaryEditorScene;
    director.visualization.shadingMode = EditorViewShadingMode::Neutral;
    director.visualization.displayExposure = 1.25f;

    auto& gameplayObserver = View(BuiltInViewSlot::GameplayCameraObserver);
    gameplayObserver = director;
    gameplayObserver.renderViewId = kGameplayCameraObserverRenderViewId;
    auto& gameplayPreview = View(BuiltInViewSlot::GameplayCameraPreview);
    gameplayPreview = director;
    gameplayPreview.renderViewId = kGameplayCameraPreviewRenderViewId;
    gameplayPreview.visualization.shadingMode = EditorViewShadingMode::Lit;

    EditorViewInstance& collision = View(BuiltInViewSlot::ModelCollisionPreview);
    collision.renderViewId = kModelCollisionPreviewRenderViewId;
    collision.role = EditorViewRole::Preview;
    collision.purpose = RENDER3D::RenderViewPurpose::EditorScene;
    collision.cameraBinding.kind = EditorViewCameraSourceKind::OwnedEditorCamera;
    collision.execution = EditorViewExecutionMode::SecondaryEditorScene;
    collision.visualization.shadingMode = EditorViewShadingMode::Neutral;
    collision.visualization.displayExposure = 1.0f;

    EditorViewInstance& material = View(BuiltInViewSlot::ModelMaterialPreview);
    material.renderViewId = kModelMaterialPreviewRenderViewId;
    material.role = EditorViewRole::Preview;
    material.purpose = RENDER3D::RenderViewPurpose::ToolPreview;
    material.cameraBinding.kind = EditorViewCameraSourceKind::OwnedEditorCamera;
    material.execution = EditorViewExecutionMode::RuntimeParityToolPreview;
    material.visualization.shadingMode = EditorViewShadingMode::Lit;
    material.visualization.displayExposure = 1.0f;

    EditorViewInstance& compare = View(BuiltInViewSlot::ModelMaterialCompare);
    compare.renderViewId = kModelMaterialCompareRenderViewId;
    compare.role = EditorViewRole::Preview;
    compare.purpose = RENDER3D::RenderViewPurpose::ToolPreview;
    compare.cameraBinding.kind = EditorViewCameraSourceKind::OwnedEditorCamera;
    compare.execution = EditorViewExecutionMode::RuntimeParityToolPreview;
    compare.visualization.shadingMode = EditorViewShadingMode::Lit;
    compare.visualization.displayExposure = 1.0f;

    EditorViewInstance& vfx = View(BuiltInViewSlot::VfxPreview);
    vfx.renderViewId = kVfxPreviewRenderViewId;
    vfx.role = EditorViewRole::Preview;
    vfx.purpose = RENDER3D::RenderViewPurpose::ToolPreview;
    vfx.cameraBinding.kind = EditorViewCameraSourceKind::OwnedEditorCamera;
    vfx.execution = EditorViewExecutionMode::RuntimeParityToolPreview;
    vfx.visualization.shadingMode = EditorViewShadingMode::Lit;
    vfx.visualization.displayExposure = 1.0f;

    EditorViewInstance& terrain = View(BuiltInViewSlot::TerrainSurfaceGame);
    terrain.renderViewId = RENDER3D::kPrimaryRenderViewId;
    terrain.role = EditorViewRole::Game;
    terrain.purpose = RENDER3D::RenderViewPurpose::EditorScene;
    terrain.cameraBinding.kind = EditorViewCameraSourceKind::OwnedEditorCamera;
    terrain.execution = EditorViewExecutionMode::PrimaryFullQuality;
}

bool EditorWorkspaceHost::RequestOpen(EditorWorkspaceOpenRequest request) {
    if (request.workspaceId == EditorWorkspaceId::Count) {
        return false;
    }
    if (request.targetCameraObjectId && request.targetCameraObjectId->value == 0) {
        request.targetCameraObjectId.reset();
    }
    pendingOpenRequest_ = std::move(request);
    return true;
}

const EditorWorkspaceOpenRequest* EditorWorkspaceHost::PeekPending() const noexcept {
    return pendingOpenRequest_ ? &*pendingOpenRequest_ : nullptr;
}

void EditorWorkspaceHost::CancelPending() noexcept { pendingOpenRequest_.reset(); }

std::optional<EditorWorkspaceActivation> EditorWorkspaceHost::ApplyPending() {
    if (!pendingOpenRequest_) {
        return std::nullopt;
    }

    EditorWorkspaceOpenRequest request = std::move(*pendingOpenRequest_);
    pendingOpenRequest_.reset();

    EditorWorkspaceActivation activation{};
    activation.previous = activeWorkspace_;
    activation.current = request.workspaceId;
    activation.targetCameraObjectId = request.targetCameraObjectId;
    activation.sequenceAssetGuid = request.sequenceAssetGuid;
    activation.cameraProfileAssetGuid = request.cameraProfileAssetGuid;
    activation.textStyleAssetGuid = request.textStyleAssetGuid;
    activation.uiAnimationAssetGuid = request.uiAnimationAssetGuid;
    activation.modelAssetGuid = request.modelAssetGuid;
    activation.materialAssetGuid = request.materialAssetGuid;
    activation.animationStateMachineAssetGuid = request.animationStateMachineAssetGuid;
    activation.vfxAssetGuid = request.vfxAssetGuid;
    activation.dialogueAssetGuid = request.dialogueAssetGuid;
    activeWorkspace_ = request.workspaceId;

    return activation;
}

EditorWorkspaceId EditorWorkspaceHost::GetActive() const noexcept { return activeWorkspace_; }

bool EditorWorkspaceHost::IsActive(EditorWorkspaceId workspaceId) const noexcept {
    return activeWorkspace_ == workspaceId;
}

void EditorWorkspaceHost::RequestResetActiveLayout() noexcept {
    const size_t index = static_cast<size_t>(activeWorkspace_);
    if (index < resetLayoutRequests_.size()) {
        resetLayoutRequests_[index] = true;
    }
}

bool EditorWorkspaceHost::ConsumeReset(EditorWorkspaceId workspaceId) noexcept {
    const size_t index = static_cast<size_t>(workspaceId);
    if (index >= resetLayoutRequests_.size()) {
        return false;
    }
    const bool result = resetLayoutRequests_[index];
    resetLayoutRequests_[index] = false;
    return result;
}

EditorViewInstance& EditorWorkspaceHost::GetGameplayCameraObserverView() noexcept {
    return View(BuiltInViewSlot::GameplayCameraObserver);
}
EditorViewInstance& EditorWorkspaceHost::GetGameplayCameraPreviewView() noexcept {
    return View(BuiltInViewSlot::GameplayCameraPreview);
}

EditorViewInstance& EditorWorkspaceHost::GetCinematicsGameView() noexcept {
    return View(BuiltInViewSlot::CinematicsGame);
}

const EditorViewInstance& EditorWorkspaceHost::GetCinematicsGameView() const noexcept {
    return View(BuiltInViewSlot::CinematicsGame);
}

EditorViewInstance& EditorWorkspaceHost::GetCinematicsDirectorView() noexcept {
    return View(BuiltInViewSlot::CinematicsDirector);
}

const EditorViewInstance& EditorWorkspaceHost::GetCinematicsDirectorView() const noexcept {
    return View(BuiltInViewSlot::CinematicsDirector);
}

EditorViewInstance& EditorWorkspaceHost::GetModelCollisionPreviewView() noexcept {
    return View(BuiltInViewSlot::ModelCollisionPreview);
}

const EditorViewInstance& EditorWorkspaceHost::GetModelCollisionPreviewView() const noexcept {
    return View(BuiltInViewSlot::ModelCollisionPreview);
}

EditorViewInstance& EditorWorkspaceHost::GetModelMaterialPreviewView() noexcept {
    return View(BuiltInViewSlot::ModelMaterialPreview);
}

const EditorViewInstance& EditorWorkspaceHost::GetModelMaterialPreviewView() const noexcept {
    return View(BuiltInViewSlot::ModelMaterialPreview);
}

EditorViewInstance& EditorWorkspaceHost::GetModelMaterialCompareView() noexcept {
    return View(BuiltInViewSlot::ModelMaterialCompare);
}

const EditorViewInstance& EditorWorkspaceHost::GetModelMaterialCompareView() const noexcept {
    return View(BuiltInViewSlot::ModelMaterialCompare);
}

EditorViewInstance& EditorWorkspaceHost::GetVfxPreviewView() noexcept { return View(BuiltInViewSlot::VfxPreview); }

const EditorViewInstance& EditorWorkspaceHost::GetVfxPreviewView() const noexcept {
    return View(BuiltInViewSlot::VfxPreview);
}

EditorViewInstance& EditorWorkspaceHost::GetTerrainSurfaceGameView() noexcept {
    return View(BuiltInViewSlot::TerrainSurfaceGame);
}

const EditorViewInstance& EditorWorkspaceHost::GetTerrainSurfaceGameView() const noexcept {
    return View(BuiltInViewSlot::TerrainSurfaceGame);
}

EditorViewInstance* EditorWorkspaceHost::FindView(RENDER3D::RenderViewId renderViewId) noexcept {
    for (EditorViewInstance& view : views_) {
        if (view.renderViewId == renderViewId) {
            return &view;
        }
    }
    return nullptr;
}

const EditorViewInstance* EditorWorkspaceHost::FindView(RENDER3D::RenderViewId renderViewId) const noexcept {
    for (const EditorViewInstance& view : views_) {
        if (view.renderViewId == renderViewId) {
            return &view;
        }
    }
    return nullptr;
}

EditorViewInstance& EditorWorkspaceHost::View(BuiltInViewSlot slot) noexcept {
    return views_[static_cast<size_t>(slot)];
}

const EditorViewInstance& EditorWorkspaceHost::View(BuiltInViewSlot slot) const noexcept {
    return views_[static_cast<size_t>(slot)];
}

} // namespace HIKARI::EDITOR
