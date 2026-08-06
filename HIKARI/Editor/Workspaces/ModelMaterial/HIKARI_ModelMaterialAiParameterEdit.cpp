#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialWorkspaceController.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "Assets/Material/Compiler/HIKARI_MaterialCompiler.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {

bool ModelMaterialWorkspaceController::ApplyAiMaterialParameterEdit(
    DocumentSceneBase &scene, const std::string &parameterId,
    float value, std::string &outMessage, std::string &outCode) {
  std::lock_guard<std::recursive_mutex> guard(
      authoringDocument_.GetMutex());
  outCode = "OK";
  if (!authoringDocument_.HasMaterial()) {
    outMessage = "未打开材质文档";
    outCode = "MATERIAL_DOCUMENT_MISSING";
    return false;
  }
  MaterialAssetData before = authoringDocument_.GetMaterial();
  MaterialAssetData &material = authoringDocument_.EditMaterial();
  const auto matches = [&parameterId](const std::string &id) {
    if (id == parameterId || id == "parameter." + parameterId) {
      return true;
    }
    return id.size() > parameterId.size() &&
        id.compare(
            id.size() - parameterId.size(),
            parameterId.size(),
            parameterId) == 0;
  };
  if (material.kind == MaterialAssetKind::Instance) {
    std::vector<MaterialInstanceParameterOverrideData *> matched{};
    for (MaterialInstanceParameterOverrideData &override :
         material.instance.parameterOverrides) {
      if (matches(override.parameterId)) {
        matched.push_back(&override);
      }
    }
    if (matched.empty()) {
      outMessage = "实例中没有参数覆盖：" + parameterId +
          "（只能修改已存在的 override）";
      outCode = "PARAMETER_NOT_FOUND";
      return false;
    }
    if (matched.size() > 1u) {
      outMessage = "参数名 " + parameterId + " 匹配到多个实例参数：";
      for (const auto *candidate : matched) {
        outMessage += candidate->parameterId + " ";
      }
      outMessage += "；请使用完整参数名";
      outCode = "AMBIGUOUS_PARAMETER";
      return false;
    }
    matched.front()->value.numeric.x = value;
    if (authoringDocument_.CommitMaterial(
            std::move(before), "AI edit instance parameter")) {
      (void)RefreshMaterialPreviews(scene, statusMessage_);
      outMessage = "已更新实例参数 " + parameterId + " = " +
          std::to_string(value);
      return true;
    }
    outMessage = "实例材质提交失败";
    outCode = "COMMIT_FAILED";
    return false;
  }
  MaterialGraphData *graph = material.kind == MaterialAssetKind::Material
      ? &material.graph
      : material.kind == MaterialAssetKind::Function
          ? &material.function.graph
          : nullptr;
  if (graph == nullptr) {
    outMessage = "当前资产没有可编辑的节点图";
    outCode = "NO_GRAPH";
    return false;
  }
  std::vector<MaterialGraphParameterData *> matched{};
  for (MaterialGraphParameterData &parameter :
       graph->parameters) {
    if (matches(parameter.id)) {
      matched.push_back(&parameter);
    }
  }
  if (matched.empty()) {
    outMessage = "找不到参数：" + parameterId;
    outCode = "PARAMETER_NOT_FOUND";
    return false;
  }
  if (matched.size() > 1u) {
    outMessage = "参数名 " + parameterId + " 匹配到多个参数：";
    for (const MaterialGraphParameterData *candidate : matched) {
      outMessage += candidate->id + " ";
    }
    outMessage += "；请使用完整参数名（如 parameter.baseColor）";
    outCode = "AMBIGUOUS_PARAMETER";
    return false;
  }
  MaterialGraphParameterData *found = matched.front();
  if (found->defaultValue.type != MaterialValueType::Float1 &&
      found->defaultValue.type != MaterialValueType::Float2 &&
      found->defaultValue.type != MaterialValueType::Float3 &&
      found->defaultValue.type != MaterialValueType::Float4) {
    outMessage = "参数 " + parameterId + " 不是数值类型";
    outCode = "PARAMETER_TYPE_MISMATCH";
    return false;
  }
  found->defaultValue.numeric.x = value;
  if (authoringDocument_.CommitMaterial(
          std::move(before), "AI edit parameter")) {
    (void)RefreshMaterialPreviews(scene, statusMessage_);
    outMessage = "已更新 " + parameterId + " = " +
        std::to_string(value);
    return true;
  }
  outMessage = "材质提交失败";
  outCode = "COMMIT_FAILED";
  return false;
}

void ModelMaterialWorkspaceController::AppendAiCompileDiagnostics(
    std::vector<std::string> &outDetails) const {
  if (!authoringDocument_.HasMaterial()) {
    return;
  }
  const MaterialCompileResult &compile =
      authoringDocument_.GetCompileResult();
  if (compile.success && !compile.usedFallback) {
    outDetails.push_back("compile: ok");
    return;
  }
  size_t count = 0u;
  for (const MaterialCompileDiagnostic &diagnostic :
       compile.diagnostics) {
    if (count >= 8u) {
      break;
    }
    std::string line =
        diagnostic.code + ": " + diagnostic.message;
    if (!diagnostic.subjectId.empty()) {
      line += " (" + diagnostic.subjectId + ")";
    }
    outDetails.push_back(line);
    ++count;
  }
  if (count == 0u) {
    outDetails.push_back(
        "compile: " +
        std::string(compile.success ? "ok" : "failed"));
  }
}

} // namespace HIKARI::EDITOR
