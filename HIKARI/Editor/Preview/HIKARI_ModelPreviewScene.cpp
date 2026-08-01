#include "Editor/Preview/HIKARI_ModelPreviewScene.h"

#include <algorithm>
#include <utility>

#include "Assets/Formats/HIKARI_HmodelFormat.h"
#include "Assets/HIKARI_AssetRecord.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "Render3D/Core/HIKARI_Material.h"

namespace HIKARI::EDITOR {
namespace {

constexpr uint64_t kModelPreviewObjectId = 0x484D4F444C505256ull;

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

bool ModelPreviewScene::Load(const AssetRecord &record,
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
        "model has no HCMESH artifact; reimport it before previewing";
    return false;
  }

  model_ = std::make_shared<ModelAsset>();
  if (!ReadHmodelFile(modelPath, *model_, outMessage)) {
    ResetState();
    return false;
  }
  assetGuid_ = record.guid;
  model_->id = AssetId{record.guid.value};
  model_->SetName(record.displayName);
  model_->SetSourcePath(modelPath.generic_string());
  model_->SetState(ModelAsset::State::Loaded);
  clusteredGeometryPath_ = clusteredGeometryPath.generic_string();

  BOUNDS::EnsureModelBounds(*model_);
  bounds_ = model_->bounds;
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

    ModelPreviewNode previewNode{};
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
    ResetState();
    return false;
  }

  AdvanceRevision();
  RebuildSceneSource();
  ready_ = gpuSceneRegistry_.GetSceneSource().HasAnyGpuSceneInstances();
  if (!ready_) {
    outMessage = "model preview has no renderable surfaces";
    ResetState();
    return false;
  }
  outMessage = "model preview ready";
  return true;
}

void ModelPreviewScene::Clear() {
  const bool hadState = ready_ || model_ != nullptr || assetGuid_.IsValid();
  ResetState();
  if (hadState) {
    AdvanceRevision();
  }
}

void ModelPreviewScene::SetRenderPolicy(
    const ModelPreviewRenderPolicy &policy) {
  if (renderPolicy_ == policy) {
    return;
  }
  renderPolicy_ = policy;
  if (ready_) {
    AdvanceRevision();
    RebuildSceneSource();
  }
}

const ModelPreviewRenderPolicy &
ModelPreviewScene::GetRenderPolicy() const noexcept {
  return renderPolicy_;
}

bool ModelPreviewScene::SetPreviewMaterialOverride(
    uint32_t materialIndex, const Material *material) {
  if (material == nullptr) {
    return ClearPreviewMaterialOverride(materialIndex);
  }
  if (!ready_ || !HasSurfaceMaterialIndex(materialIndex)) {
    return false;
  }

  const uint64_t materialRevision = material->GetRevision();
  auto found = std::find_if(
      materialOverrides_.begin(), materialOverrides_.end(),
      [materialIndex](
          const RENDER3D::RUNTIME::SceneRenderMaterialOverride &slot) {
        return slot.materialIndex == materialIndex;
      });
  if (found != materialOverrides_.end()) {
    if (found->material == material &&
        found->materialRevision == materialRevision) {
      return true;
    }
    found->material = material;
    found->materialRevision = materialRevision;
  } else {
    materialOverrides_.push_back(
        RENDER3D::RUNTIME::SceneRenderMaterialOverride{
            materialIndex, material, materialRevision});
    std::sort(materialOverrides_.begin(), materialOverrides_.end(),
              [](const auto &left, const auto &right) {
                return left.materialIndex < right.materialIndex;
              });
  }

  AdvanceRevision();
  RebuildSceneSource();
  return true;
}

bool ModelPreviewScene::ClearPreviewMaterialOverride(uint32_t materialIndex) {
  if (!ready_ || !HasSurfaceMaterialIndex(materialIndex)) {
    return false;
  }
  const size_t previousSize = materialOverrides_.size();
  std::erase_if(
      materialOverrides_,
      [materialIndex](
          const RENDER3D::RUNTIME::SceneRenderMaterialOverride &slot) {
        return slot.materialIndex == materialIndex;
      });
  if (materialOverrides_.size() == previousSize) {
    return true;
  }

  AdvanceRevision();
  RebuildSceneSource();
  return true;
}

void ModelPreviewScene::ClearPreviewMaterialOverrides() {
  if (materialOverrides_.empty()) {
    return;
  }
  materialOverrides_.clear();
  if (ready_) {
    AdvanceRevision();
    RebuildSceneSource();
  }
}

bool ModelPreviewScene::IsReady() const noexcept { return ready_; }

const AssetGuid &ModelPreviewScene::GetAssetGuid() const noexcept {
  return assetGuid_;
}

const ModelAsset *ModelPreviewScene::GetModel() const noexcept {
  return ready_ ? model_.get() : nullptr;
}

std::shared_ptr<const ModelAsset>
ModelPreviewScene::GetSharedModel() const noexcept {
  return ready_ ? model_ : nullptr;
}

const Bounds &ModelPreviewScene::GetBounds() const noexcept { return bounds_; }

const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource *
ModelPreviewScene::GetSceneSource() const noexcept {
  return ready_ ? &gpuSceneRegistry_.GetSceneSource() : nullptr;
}

const std::vector<ModelPreviewNode> &
ModelPreviewScene::GetSourceNodes() const noexcept {
  return sourceNodes_;
}

const ModelPreviewNode *
ModelPreviewScene::FindSourceNode(int32_t nodeIndex) const noexcept {
  const auto found =
      std::find_if(sourceNodes_.begin(), sourceNodes_.end(),
                   [nodeIndex](const ModelPreviewNode &node) {
                     return node.nodeIndex == nodeIndex;
                   });
  return found != sourceNodes_.end() ? &*found : nullptr;
}

const Material *ModelPreviewScene::GetPreviewMaterialOverride(
    uint32_t materialIndex) const noexcept {
  const auto found = std::find_if(
      materialOverrides_.begin(), materialOverrides_.end(),
      [materialIndex](
          const RENDER3D::RUNTIME::SceneRenderMaterialOverride &slot) {
        return slot.materialIndex == materialIndex;
      });
  return found != materialOverrides_.end() ? found->material : nullptr;
}

uint64_t ModelPreviewScene::GetRevision() const noexcept { return revision_; }

bool ModelPreviewScene::HasSurfaceMaterialIndex(
    uint32_t materialIndex) const noexcept {
  return std::any_of(renderModel_.surfaces.begin(), renderModel_.surfaces.end(),
                     [materialIndex](const auto &surface) {
                       return surface.materialIndex == materialIndex;
                     });
}

void ModelPreviewScene::AdvanceRevision() noexcept {
  ++revision_;
  if (revision_ == 0u) {
    revision_ = 1u;
  }
}

void ModelPreviewScene::ResetState() {
  ready_ = false;
  gpuSceneRegistry_.Clear();
  sceneCache_.Clear();
  renderModel_ = {};
  model_.reset();
  bounds_ = {};
  materialOverrides_.clear();
  sourceNodes_.clear();
  clusteredGeometryPath_.clear();
  assetGuid_ = {};
}

void ModelPreviewScene::RebuildSceneSource() {
  sceneCache_.BeginSync(revision_);
  RENDER3D::RUNTIME::SceneRenderObjectDesc object{};
  object.id.value = kModelPreviewObjectId;
  object.model = model_.get();
  object.renderModel = &renderModel_;
  object.worldTransform = {};
  object.localBounds = bounds_;
  object.worldBounds = bounds_;
  object.clusteredGeometryPath = clusteredGeometryPath_;
  object.visible = true;
  object.isStatic = true;
  object.castShadow = renderPolicy_.castShadow;
  object.receiveShadow = renderPolicy_.receiveShadow;
  object.allowStaticCachedForward =
      renderPolicy_.allowStaticCachedForward;
  object.materialOverrides = materialOverrides_;
  sceneCache_.Upsert(object);
  sceneCache_.EndSync();
  sceneCache_.PreRenderSync();

  RENDER3D::GPUDRIVEN::GpuSceneRegistrySyncInput input{};
  input.sceneCache = &sceneCache_;
  gpuSceneRegistry_.SyncForwardFromSceneCache(input);
}

} // namespace HIKARI::EDITOR
