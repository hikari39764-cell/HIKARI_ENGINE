#include "HIKARI_MaterialGraphAuthoringOperations.h"
#include "HIKARI_ModelMaterialWorkspaceController.h"
#include "Editor/Commands/HIKARI_EditorOperationValue.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Scene/Document/HIKARI_DocumentSceneBase.h"
namespace HIKARI::EDITOR {
namespace {
        void AppendAiHints(
            const std::string& code,
            EDITOR::EditorOperationResult& result) {
            if (code == "AMBIGUOUS_PARAMETER" ||
                code == "PARAMETER_NOT_FOUND") {
                result.hints = {
                    "请使用完整参数名（如 parameter.baseColor），"
                    "或用 material.graph_query 查看当前参数",
                };
            } else if (code == "PARAMETER_TYPE_MISMATCH") {
                result.hints = {
                    "该参数不是数值类型，无法用 value 修改",
                };
            } else if (code == "UNKNOWN_NODE_TYPE") {
                result.hints = {
                    "请用 help material.graph_edit 查看可用的 "
                    "nodeType 写法（Constant/Parameter/Add 或 "
                    "node.additive 等）",
                };
            } else if (code == "PIN_NOT_FOUND" ||
                       code == "CONNECT_REJECTED") {
                result.hints = {
                    "先用 material.graph_query 查看节点 id 和插槽，"
                    "再连线；插槽可省略或用 out/in 取第一个",
                };
            } else if (code == "FUNCTION_CALL_UNSUPPORTED") {
                result.hints = {
                    "FunctionCall 需要资产 GUID，请在编辑器的 "
                    "Material Functions 菜单添加；也可以用 "
                    "material.graph_edit remove_node 删除坏节点",
                };
            } else if (code == "NODE_NOT_FOUND" ||
                       code == "EDGE_NOT_FOUND") {
                result.hints = {
                    "先用 material.graph_query 查看现有节点 id/"
                    "displayName",
                };
            } else if (code == "SURFACE_OUTPUT_EXISTS" ||
                       code == "SURFACE_OUTPUT_PROTECTED") {
                result.hints = {
                    "Surface Output 是单例根节点，不能重复添加或删除",
                };
            } else if (code.find("COMPILE") != std::string::npos) {
                result.hints = {
                    "请查看 compile 诊断修正节点，"
                    "或用 action.undo_last 回退",
                };
            }
        }
    EDITOR::EditorOperationResult
    ExecuteAiMaterialGraphQuery(ModelMaterialWorkspaceController& controller, DocumentSceneBase* scene,
        const nlohmann::json& parameters) {
        EDITOR::EditorOperationResult result{};
        if (scene == nullptr) {
            result.code = "NO_SCENE";
            result.message = "场景上下文不可用";
            return result;
        }
        std::string message;
        std::string code;
        std::vector<std::string> details;
        if (controller.QueryAiMaterialGraph(
                *scene,
                parameters.value("name", std::string{}),
                message, details, code)) {
            result.ok = true;
            result.code = "OK";
            result.message = message;
            result.details = std::move(details);
        } else {
            result.code = code;
            result.message = message;
            result.details = std::move(details);
        }
        return result;
    }
    EDITOR::EditorOperationResult
    ExecuteAiMaterialGraphLayout(ModelMaterialWorkspaceController& controller, DocumentSceneBase* scene) {
        EDITOR::EditorOperationResult result{};
        if (scene == nullptr) {
            result.code = "NO_SCENE";
            result.message = "场景上下文不可用";
            return result;
        }
        std::string message;
        std::string code;
        if (controller.LayoutMaterialGraph(
                *scene, message, code)) {
            result.ok = true;
            result.code = "OK";
            result.message = message;

        } else {
            result.code = code;
            result.message = message;
        }
        return result;
    }
    EDITOR::EditorOperationResult
    ExecuteAiMaterialEditParameter(ModelMaterialWorkspaceController& controller, DocumentSceneBase* scene,
        const nlohmann::json& parameters) {
        EDITOR::EditorOperationResult result{};
        if (scene == nullptr) {
            result.code = "NO_SCENE";
            result.message = "场景上下文不可用";
            return result;
        }
        const std::string parameter =
            parameters.value("parameter", std::string{});
        if (parameter.empty()) {
            result.code = "MISSING_FIELD";
            result.message = "缺少 parameter 字段";
            result.hints = {
                "请带上完整参数名，例如 "
                "{\"action\":\"material.edit_parameter\","
                "\"parameter\":\"parameter.baseColor\","
                "\"value\":0.8}",
            };
            return result;
        }
        float value = 0.0f;
        const auto valueIt = parameters.find("value");
        if (valueIt != parameters.end()) {
            if (valueIt->is_number()) {
                value = valueIt->get<float>();
            } else if (valueIt->is_string()) {
                const std::string valueText =
                    valueIt->get<std::string>();
                try {
                    value = std::stof(valueText);
                } catch (...) {
                    result.code = "INVALID_VALUE";
                    result.message =
                        "value 不是有效数字：" + valueText;
                    return result;
                }
            }
        }
        std::string message;
        std::string code;
        if (controller
                .ApplyAiMaterialParameterEdit(
                    *scene, parameter, value, message,
                    code)) {
            result.ok = true;
            result.code = "OK";
            result.message = message;
            controller
                .AppendAiCompileDiagnostics(
                    result.details);
        } else {
            result.code = code;
            result.message = message;
            AppendAiHints(code, result);
        }
        return result;
    }
    EDITOR::EditorOperationResult
    ExecuteAiMaterialGraphEdit(ModelMaterialWorkspaceController& controller, DocumentSceneBase* scene,
        const nlohmann::json& parameters) {
        EDITOR::EditorOperationResult result{};
        if (scene == nullptr) {
            result.code = "NO_SCENE";
            result.message = "场景上下文不可用";
            return result;
        }
        EDITOR::AiMaterialGraphEditRequest request{};
        request.operation =
            parameters.value("operation", std::string{});
        if (request.operation.empty()) {
            request.operation =
                parameters.value("op", std::string{});
        }
        request.nodeType =
            parameters.value("nodeType", std::string{});
        request.name =
            parameters.value("name", std::string{});
        if (request.name.empty()) {
            request.name =
                parameters.value("node", std::string{});
        }
        request.sourceNode =
            parameters.value("sourceNode", std::string{});
        request.sourcePin =
            parameters.value("sourcePin", std::string{});
        request.targetNode =
            parameters.value("targetNode", std::string{});
        request.targetPin =
            parameters.value("targetPin", std::string{});
        request.edgeId =
            parameters.value("edgeId", std::string{});
        request.valueType =
            parameters.value("valueType", std::string{});
        request.property =
            parameters.value("property", std::string{});
        request.resource =
            parameters.value("resource", std::string{});
        request.stringValue =
            parameters.value("value", std::string{});
        request.manualPosition =
            parameters.value("layout", std::string{}) ==
            "manual";
        request.positionX =
            ReadEditorOperationFloat(parameters, "positionX", 0.0f);
        request.positionY =
            ReadEditorOperationFloat(parameters, "positionY", 0.0f);
        const auto hasNumberField =
            [&parameters](const char* key) {
                const auto found = parameters.find(key);
                return found != parameters.end() &&
                    (found->is_number() ||
                     (found->is_string() &&
                      !found->get<std::string>().empty()));
            };
        request.valueSet =
            hasNumberField("valueX") ||
            hasNumberField("valueY") ||
            hasNumberField("valueZ") ||
            hasNumberField("valueW");
        request.valueX =
            ReadEditorOperationFloat(parameters, "valueX", 0.0f);
        request.valueY =
            ReadEditorOperationFloat(parameters, "valueY", 0.0f);
        request.valueZ =
            ReadEditorOperationFloat(parameters, "valueZ", 0.0f);
        request.valueW =
            ReadEditorOperationFloat(parameters, "valueW", 0.0f);
        std::string message;
        std::string code;
        std::vector<std::string> graphDetails;
        if (controller
                .ApplyAiMaterialGraphEdit(
                    *scene, request, message,
                    graphDetails, code)) {
            result.ok = true;
            result.code = "OK";
            result.message = message;
            result.details = std::move(graphDetails);
            controller
                .AppendAiCompileDiagnostics(
                    result.details);
        } else {
            result.code = code;
            result.message = message;
            result.details = std::move(graphDetails);
            AppendAiHints(code, result);
        }
        return result;
    }
}
void RegisterMaterialGraphAuthoringOperations(EditorOperationRegistry& registry,
    ModelMaterialWorkspaceController& controller, std::function<DocumentSceneBase*()> sceneProvider) {

        registry.Register({
            "material.graph_query",
            "List material graph nodes with ids, pins and connections.",
            "model_material",
            {
                EDITOR::EditorOperationParameter{"name", EditorOperationParameterType::String,
                  false, "按节点 id/displayName 过滤"},
            },
            [&controller, sceneProvider](const nlohmann::json& parameters) {
                return ExecuteAiMaterialGraphQuery(controller, sceneProvider(), parameters);
            },
        }, HIKARI::EDITOR::EditorOperationKind::Query);
        registry.Register({
            "material.insert_module", "Insert an editable reusable material graph module.", "model_material",
            {{"module", EditorOperationParameterType::String, true, "dissolve / uv_flow / radial_mask / noise_breakup"},
             {"x", EditorOperationParameterType::Float, false, "Canvas X"},
             {"y", EditorOperationParameterType::Float, false, "Canvas Y"}},
            [&controller, sceneProvider](const nlohmann::json& parameters) {
                EditorOperationResult result; auto* scene = sceneProvider();
                if (!scene) { result.code = "SCENE_MISSING"; return result; }
                result.ok = controller.InsertMaterialModule(*scene, parameters.value("module", std::string{}),
                    {parameters.value("x",40.0f),parameters.value("y",40.0f)},result.message);
                result.code = result.ok ? "OK" : "MODULE_FAILED"; return result;
            },
        }, HIKARI::EDITOR::EditorOperationKind::Mutation);
        registry.Register({
            "material.extract_function", "Save the connected selection as a reusable function and replace it with one call.", "model_material",
            {{"name", EditorOperationParameterType::String, false, "Function asset name"}},
            [&controller, sceneProvider](const nlohmann::json& parameters) {
                EditorOperationResult result; auto* scene = sceneProvider();
                if (!scene) { result.code = "SCENE_MISSING"; return result; }
                result.ok = controller.ExtractSelectedMaterialFunction(*scene, parameters.value("name", std::string{}),result.message);
                result.code = result.ok ? "OK" : "EXTRACTION_FAILED"; return result;
            },
        }, HIKARI::EDITOR::EditorOperationKind::Mutation);
        registry.Register({
            "material.graph_layout",
            "Auto-layout material graph nodes.",
            "model_material",
            {},
            [&controller, sceneProvider](const nlohmann::json&) {
                return ExecuteAiMaterialGraphLayout(controller, sceneProvider());
            },
        }, HIKARI::EDITOR::EditorOperationKind::Mutation);
        registry.Register({
            "material.edit_parameter",
            "Edit a scalar material graph parameter by full parameter name.",
            "model_material",
            {
                EDITOR::EditorOperationParameter{"parameter", EditorOperationParameterType::String,
                  true, "完整参数名，例如 parameter.baseColor"},
                EDITOR::EditorOperationParameter{"value", EditorOperationParameterType::Float,
                  true, "要写入的数值"},
            },
            [&controller, sceneProvider](const nlohmann::json& parameters) {
                return ExecuteAiMaterialEditParameter(controller, sceneProvider(), parameters);
            },
        }, HIKARI::EDITOR::EditorOperationKind::Mutation);
        registry.Register({
            "material.graph_edit",
            "Add/remove/connect material graph nodes and edit node values.",
            "model_material",
            {
                EDITOR::EditorOperationParameter{"operation", EditorOperationParameterType::String,
                  true, "add_node/remove_node/connect/disconnect/set_node"},
                EDITOR::EditorOperationParameter{"nodeType", EditorOperationParameterType::String,
                  false, "add_node 的稳定节点类型名"},
                EDITOR::EditorOperationParameter{"name", EditorOperationParameterType::String,
                  false, "add_node 的显示名或 remove_node 的节点 id/name"},
                EDITOR::EditorOperationParameter{"node", EditorOperationParameterType::String,
                  false, "name 的别名"},
                EDITOR::EditorOperationParameter{"sourceNode", EditorOperationParameterType::String,
                  false, "连线起点节点"},
                EDITOR::EditorOperationParameter{"sourcePin", EditorOperationParameterType::String,
                  false, "连线起点插槽，缺省取第一个"},
                EDITOR::EditorOperationParameter{"targetNode", EditorOperationParameterType::String,
                  false, "连线终点节点"},
                EDITOR::EditorOperationParameter{"targetPin", EditorOperationParameterType::String,
                  false, "连线终点插槽，缺省取第一个"},
                EDITOR::EditorOperationParameter{"edgeId", EditorOperationParameterType::String,
                  false, "disconnect 的边 id"},
                EDITOR::EditorOperationParameter{"valueType", EditorOperationParameterType::String,
                  false, "set_node 的值类型"},
                EDITOR::EditorOperationParameter{"property", EditorOperationParameterType::String,
                  false, "set_node 的属性名"},
                EDITOR::EditorOperationParameter{"resource", EditorOperationParameterType::String,
                  false, "set_node 的资源 id（TextureSample）"},
                EDITOR::EditorOperationParameter{"value", EditorOperationParameterType::String,
                  false, "set_node 的字符串属性值"},
                EDITOR::EditorOperationParameter{"layout", EditorOperationParameterType::String,
                  false, "manual 保留坐标，否则自动布局"},
                EDITOR::EditorOperationParameter{"positionX", EditorOperationParameterType::Float,
                  false, "节点 X"},
                EDITOR::EditorOperationParameter{"positionY", EditorOperationParameterType::Float,
                  false, "节点 Y"},
                EDITOR::EditorOperationParameter{"valueX", EditorOperationParameterType::Float,
                  false, "值 X"},
                EDITOR::EditorOperationParameter{"valueY", EditorOperationParameterType::Float,
                  false, "值 Y"},
                EDITOR::EditorOperationParameter{"valueZ", EditorOperationParameterType::Float,
                  false, "值 Z"},
                EDITOR::EditorOperationParameter{"valueW", EditorOperationParameterType::Float,
                  false, "值 W"},
            },
            [&controller, sceneProvider](const nlohmann::json& parameters) {
                return ExecuteAiMaterialGraphEdit(controller, sceneProvider(), parameters);
            },
        }, HIKARI::EDITOR::EditorOperationKind::Mutation);
}
}
