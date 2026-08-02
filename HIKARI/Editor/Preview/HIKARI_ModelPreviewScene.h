#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetGuid.h"
#include "Assets/Models/HIKARI_ModelAsset.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneRegistry.h"
#include "Render3D/Runtime/HIKARI_RenderModelAsset.h"
#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"

namespace HIKARI {
struct AssetRecord;
class Material;
}

namespace HIKARI::EDITOR {

struct ModelPreviewNode {
  int32_t nodeIndex = -1;
  int32_t meshIndex = -1;
  std::string name{};
  Bounds bounds{};
  MATH::Mat4 globalTransform = MATH::Mat4::Identity();
};

struct ModelPreviewRenderPolicy {
  bool castShadow = false;
  bool receiveShadow = false;
  bool allowStaticCachedForward = false;

  friend bool operator==(const ModelPreviewRenderPolicy &,
                         const ModelPreviewRenderPolicy &) = default;
};

// HMODEL/HCMESH から Tool Preview 用の Runtime scene source を構築する。
// 描画実行と UI 状態は呼び出し側が所有する。
class ModelPreviewScene {
public:
  bool Load(const AssetRecord &record, const std::filesystem::path &projectRoot,
            std::string &outMessage);
  bool LoadMaterialSphere(const std::filesystem::path &projectRoot,
                          std::string &outMessage);
  void Clear();

  void SetRenderPolicy(const ModelPreviewRenderPolicy &policy);
  const ModelPreviewRenderPolicy &GetRenderPolicy() const noexcept;

  // Material は借用参照。呼び出し側は override の解除または再 Load まで
  // Material の寿命を維持する。
  bool SetPreviewMaterialOverride(uint32_t materialIndex,
                                  const Material *material);
  bool ClearPreviewMaterialOverride(uint32_t materialIndex);
  void ClearPreviewMaterialOverrides();

  bool SetEditorSelectionOutline(uint32_t materialIndex);
  void ClearEditorSelectionOutline();

  bool IsReady() const noexcept;
  const AssetGuid &GetAssetGuid() const noexcept;
  const ModelAsset *GetModel() const noexcept;
  std::shared_ptr<const ModelAsset> GetSharedModel() const noexcept;
  const Bounds &GetBounds() const noexcept;
  const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource *
  GetSceneSource() const noexcept;
  const std::vector<ModelPreviewNode> &GetSourceNodes() const noexcept;
  const ModelPreviewNode *FindSourceNode(int32_t nodeIndex) const noexcept;
  const Material *GetPreviewMaterialOverride(
      uint32_t materialIndex) const noexcept;
  uint64_t GetRevision() const noexcept;

private:
  bool HasSurfaceMaterialIndex(uint32_t materialIndex) const noexcept;
  void AdvanceRevision() noexcept;
  void ResetState();
  void RebuildSceneSource();

  AssetGuid assetGuid_{};
  std::shared_ptr<ModelAsset> model_{};
  Bounds bounds_{};
  RENDER3D::RUNTIME::RenderModelAsset renderModel_{};
  RENDER3D::RUNTIME::SceneRenderCache sceneCache_{};
  RENDER3D::GPUDRIVEN::GpuSceneRegistry gpuSceneRegistry_{};
  std::vector<RENDER3D::RUNTIME::SceneRenderMaterialOverride>
      materialOverrides_{};
  uint32_t editorSelectionMaterialIndex_ =
      RENDER3D::RUNTIME::kInvalidRenderSurfaceIndex;
  std::vector<ModelPreviewNode> sourceNodes_{};
  std::string clusteredGeometryPath_{};
  ModelPreviewRenderPolicy renderPolicy_{};
  uint64_t revision_ = 0u;
  bool ready_ = false;
};

} // namespace HIKARI::EDITOR
