#include "Editor/Workspaces/ModelCollision/HIKARI_ModelCollisionPreviewScene.h"

#include <algorithm>

#include "Assets/Formats/HIKARI_HmodelFormat.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"

namespace HIKARI::EDITOR {
namespace {
std::filesystem::path ResolvePath(const std::filesystem::path &projectRoot,
                                  const std::filesystem::path &path) {

  return path.is_absolute() ? path.lexically_normal()
                            : (projectRoot / path).lexically_normal();
}

std::filesystem::path
ResolveArtifactPath(const AssetRecord &record,
                    const std::filesystem::path &projectRoot,
                    ASSETS::SEMANTICS::AssetArtifactKind kind) {

  const std::string path =
      ASSETS::SEMANTICS::FindAssetArtifactPath(record, kind, true);
  return path.empty() ? std::filesystem::path{}
                      : ResolvePath(projectRoot, path);
}
} // namespace

bool ModelCollisionPreviewScene::Load(const AssetRecord &record,
                                      const std::filesystem::path &projectRoot,
                                      std::string &outMessage) {

  Clear();
  if (record.type != AssetType::Model || !record.guid.IsValid()) {
    outMessage = "selected asset is not a valid model";
    return false;
  }
  const std::filesystem::path modelPath = ResolveArtifactPath(
      record, projectRoot, ASSETS::SEMANTICS::AssetArtifactKind::MainModel);
  if (modelPath.empty()) {
    outMessage = "model has no HMODEL artifact; import it first";
    return false;
  }
  const std::filesystem::path clusteredGeometryPath = ResolveArtifactPath(
      record, projectRoot,
      ASSETS::SEMANTICS::AssetArtifactKind::ClusteredGeometry);
  if (clusteredGeometryPath.empty()) {
    outMessage =
        "model has no HCMESH artifact; reimport it before editing collision";
    return false;
  }
  model_ = std::make_shared<ModelAsset>();
  if (!ReadHmodelFile(modelPath, *model_, outMessage)) {
    Clear();
    return false;
  }
  model_->id = AssetId{record.guid.value};
  model_->SetName(record.displayName);
  model_->SetSourcePath(modelPath.generic_string());
  model_->SetState(ModelAsset::State::Loaded);
  clusteredGeometryPath_ = clusteredGeometryPath.generic_string();
  BOUNDS::EnsureModelBounds(*model_);
  const std::vector<MATH::Mat4> nodeGlobals =
      BOUNDS::BuildModelNodeGlobals(*model_);
  sourceNodes_.reserve(model_->nodes.size());
  for (size_t nodeIndex = 0; nodeIndex < model_->nodes.size(); ++nodeIndex) {

    const ModelNode &node = model_->nodes[nodeIndex];
    const Bounds nodeBounds =
        BOUNDS::ComputeModelNodeBounds(*model_, nodeIndex, nodeGlobals);
    if (!BOUNDS::IsUsable(nodeBounds)) {
      continue;
    }
    ModelCollisionPreviewNode previewNode{};
    previewNode.nodeIndex = static_cast<int32_t>(nodeIndex);
    previewNode.meshIndex = node.meshIndex;
    previewNode.name =
        node.name.empty() ? "Node " + std::to_string(nodeIndex) : node.name;
    previewNode.bounds = nodeBounds;
    previewNode.globalTransform = nodeGlobals[nodeIndex];
    sourceNodes_.push_back(std::move(previewNode));
  }
  if (!RENDER3D::RUNTIME::BuildRenderModelAsset(*model_, renderModel_,
                                                &outMessage)) {
    Clear();
    return false;
  }

  RebuildSceneSource();
  ready_ = gpuSceneRegistry_.GetSceneSource().HasAnyGpuSceneInstances();
  if (!ready_) {
    outMessage = "model preview has no renderable surfaces";
    Clear();
    return false;
  }
  ++revision_;
  if (revision_ == 0u) {
    revision_ = 1u;
  }
  outMessage = "model preview ready";
  return true;
}

void ModelCollisionPreviewScene::Clear() {
  ready_ = false;
  gpuSceneRegistry_.Clear();
  sceneCache_.Clear();
  renderModel_ = {};
  model_.reset();
  sourceNodes_.clear();
  clusteredGeometryPath_.clear();
}

bool ModelCollisionPreviewScene::IsReady() const noexcept { return ready_; }

const ModelAsset *ModelCollisionPreviewScene::GetModel() const noexcept {
  return ready_ ? model_.get() : nullptr;
}

std::shared_ptr<const ModelAsset>
ModelCollisionPreviewScene::GetSharedModel() const noexcept {

  return ready_ ? model_ : nullptr;
}

const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource *
ModelCollisionPreviewScene::GetSceneSource() const noexcept {

  return ready_ ? &gpuSceneRegistry_.GetSceneSource() : nullptr;
}

const std::vector<ModelCollisionPreviewNode> &
ModelCollisionPreviewScene::GetSourceNodes() const noexcept {

  return sourceNodes_;
}

const ModelCollisionPreviewNode *
ModelCollisionPreviewScene::FindSourceNode(int32_t nodeIndex) const noexcept {

  const auto found =
      std::find_if(sourceNodes_.begin(), sourceNodes_.end(),
                   [nodeIndex](const ModelCollisionPreviewNode &node) {
                     return node.nodeIndex == nodeIndex;
                   });
  return found != sourceNodes_.end() ? &*found : nullptr;
}

uint64_t ModelCollisionPreviewScene::GetRevision() const noexcept {
  return revision_;
}

void ModelCollisionPreviewScene::RebuildSceneSource() {
  sceneCache_.BeginSync(revision_ + 1u);
  RENDER3D::RUNTIME::SceneRenderObjectDesc object{};
  object.id.value = 0x48434F4C4C505256ull;
  object.model = model_.get();
  object.renderModel = &renderModel_;
  object.worldTransform = {};
  object.localBounds = model_->bounds;
  object.worldBounds = model_->bounds;
  object.clusteredGeometryPath = clusteredGeometryPath_;
  object.visible = true;
  object.isStatic = true;
  object.castShadow = false;
  object.receiveShadow = false;
  object.allowStaticCachedForward = false;
  sceneCache_.Upsert(object);
  sceneCache_.EndSync();
  sceneCache_.PreRenderSync();

  RENDER3D::GPUDRIVEN::GpuSceneRegistrySyncInput input{};
  input.sceneCache = &sceneCache_;
  gpuSceneRegistry_.SyncForwardFromSceneCache(input);
}

} // namespace HIKARI::EDITOR
