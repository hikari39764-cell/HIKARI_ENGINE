#include "Editor/Tools/Console/HIKARI_ConsoleVariables.h"

#include <charconv>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <json.hpp>

#include "Core/Serialization/Json/HIKARI_JsonFieldPath.h"
#include "Core/Text/HIKARI_ParseNumber.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/Commands/HIKARI_EditorOperationRegistry.h"
#include "Gfx/HIKARI_GfxDebugConfig.h"
#include "Gfx/HIKARI_GfxDebugConfigFields.h"
#include "Render3D/Settings/HIKARI_RenderQualityFields.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"

namespace HIKARI::EDITOR {
namespace {

// 変数 1 つ。名前は組の接頭辞 + 項目表のパス。
struct Variable {
    std::string name{};
    std::string path{};
    JSONFIELDS::FieldInfo info{};
};

// 設定構造体 1 つ分の変数。型ごとの読み書きは MakeGroup が関数にして持つ。
struct VariableGroup {
    std::string prefix{};
    std::vector<Variable> variables{};
    // path の現在値・既定値（保存と同じ JSON の形）。
    std::function<nlohmann::json(std::string_view path)> current{};
    std::function<nlohmann::json(std::string_view path)> defaultValue{};
    // path に value を書いて今のセッションへ反映する。失敗したら理由を error に入れて false。
    std::function<bool(std::string_view path, const nlohmann::json& value, std::string& error)> assign{};
};

// 入れ子の構造体は子項目を "a.b" のパスで平らに並べる。
void AppendVariables(VariableGroup& group, const std::vector<JSONFIELDS::FieldInfo>& infos, const std::string& parent) {
    for (const JSONFIELDS::FieldInfo& info : infos) {
        const std::string path = parent.empty() ? info.id : parent + "." + info.id;
        if (info.kind == JSONFIELDS::FieldKind::Nested) {
            AppendVariables(group, info.children, path);
        } else {
            group.variables.push_back({group.prefix + path, path, info});
        }
    }
}

// 設定構造体 T を変数の組にする。read は今の設定、apply は設定の反映（画質パネルと同じ関数）。既定値は T{}。
template <class T, class Fields>
VariableGroup MakeGroup(std::string prefix, const Fields& fields, std::function<T()> read,
                        std::function<void(const T&)> apply) {
    VariableGroup group;
    group.prefix = std::move(prefix);
    AppendVariables(group, JSONFIELDS::Describe<T>(fields), {});
    group.current = [fields, read](std::string_view path) {
        return JSONFIELDS::GetField(read(), fields, path).value_or(nlohmann::json());
    };
    group.defaultValue = [fields](std::string_view path) {
        return JSONFIELDS::GetField(T{}, fields, path).value_or(nlohmann::json());
    };
    group.assign = [fields, read, apply](std::string_view path, const nlohmann::json& value, std::string& error) {
        T next = read();
        if (!JSONFIELDS::SetField(next, fields, path, value, error)) {
            return false;
        }
        apply(next);
        return true;
    };
    return group;
}

const std::vector<VariableGroup>& Groups() {
    static const std::vector<VariableGroup> groups{
        MakeGroup<RENDER3D::RenderQualitySettings>("r.", RENDER3D::kRenderQualitySettingsFields,
                                                   RENDER3D::GetRenderQualitySettings,
                                                   RENDER3D::SetRenderQualitySettings),
        MakeGroup<GFX::GfxDebugConfig>("gfx.", GFX::kGfxDebugConfigFields, GFX::GetGfxDebugConfig,
                                       GFX::SetGfxDebugConfig)};
    return groups;
}

struct FoundVariable {
    const VariableGroup* group = nullptr;
    const Variable* variable = nullptr;
};

// 名前は登録表が name の enum（全変数の名前）で確かめてから操作を呼ぶので、必ず見つかる。
FoundVariable FindVariable(std::string_view name) {
    for (const VariableGroup& group : Groups()) {
        for (const Variable& variable : group.variables) {
            if (variable.name == name) {
                return {&group, &variable};
            }
        }
    }
    return {};
}

// float の項目は double に広げて JSON に入るので、表示では float の最短の桁へ戻す（0.9200000166893005 → 0.92）。
nlohmann::json ForDisplay(const nlohmann::json& value) {
    if (!value.is_number_float()) {
        return value;
    }
    const double number = value.get<double>();
    const float narrowed = static_cast<float>(number);
    if (static_cast<double>(narrowed) != number) {
        return value;
    }
    char digits[32];
    const auto written = std::to_chars(digits, digits + sizeof(digits), narrowed);
    double shortest = number;
    return TEXT::ParseNumber(std::string_view(digits, written.ptr), shortest) ? nlohmann::json(shortest) : value;
}

// 値を一行に書く文字列（文字列は引用符なし）。
std::string ValueText(const nlohmann::json& value) {
    const nlohmann::json shown = ForDisplay(value);
    return shown.is_string() ? shown.get<std::string>() : shown.dump();
}

// 範囲の端の数。最短の桁のまま小数で書く（既定の書式だと 100000 が 1e+05、0.0001 が 1e-04 になる）。
std::string NumberText(double number) {
    char digits[32];
    return std::string(digits, std::to_chars(digits, digits + sizeof(digits), number, std::chars_format::fixed).ptr);
}

std::string EnumChoices(const JSONFIELDS::FieldInfo& info) {
    std::string choices;
    for (const std::string& name : info.enumNames) {
        choices += choices.empty() ? "" : "|";
        choices += name;
    }
    return choices;
}

// cvars の一行：名前 = 値  選択肢か範囲、単位、起動時のみ。
std::string DescribeLine(const VariableGroup& group, const Variable& variable) {
    const JSONFIELDS::FieldInfo& info = variable.info;
    std::string line = variable.name + " = " + ValueText(group.current(variable.path));
    if (info.kind == JSONFIELDS::FieldKind::Enum) {
        line += "  " + EnumChoices(info);
    } else if (info.hasRange) {
        line += "  " + NumberText(info.minimum) + ".." + NumberText(info.maximum);
    }
    if (!info.unit.empty()) {
        line += " " + info.unit;
    }
    if (info.readOnly) {
        line += "  (startup only)";
    }
    return line;
}

EditorOperationResult ReadVariable(std::string_view name) {
    const FoundVariable found = FindVariable(name);
    const nlohmann::json value = found.group->current(found.variable->path);
    return OperationOk(std::string(name) + " = " + ValueText(value),
                       {{"name", std::string(name)},
                        {"value", ForDisplay(value)},
                        {"default", ForDisplay(found.group->defaultValue(found.variable->path))}});
}

// requested が null なら既定値へ戻す。範囲へ丸めた・正規化で変わった時は、頼んだ値との違いを見せる。
EditorOperationResult AssignVariable(std::string_view name, const nlohmann::json* requested) {
    const FoundVariable found = FindVariable(name);
    if (found.variable->info.readOnly) {
        return OperationFailure("STARTUP_ONLY", std::string(name) +
                                                    " is read only when the graphics device is created; it cannot "
                                                    "change in a running session.");
    }
    const nlohmann::json previous = found.group->current(found.variable->path);
    const nlohmann::json value = requested != nullptr ? *requested : found.group->defaultValue(found.variable->path);
    std::string error;
    if (!found.group->assign(found.variable->path, value, error)) {
        EditorOperationResult failure = OperationFailure("INVALID_VALUE", found.group->prefix + error);
        if (found.variable->info.kind == JSONFIELDS::FieldKind::Enum) {
            failure.hints.push_back("choices: " + EnumChoices(found.variable->info));
        }
        return failure;
    }
    const nlohmann::json applied = found.group->current(found.variable->path);
    EditorOperationResult result =
        OperationOk(std::string(name) + " = " + ValueText(applied),
                    {{"name", std::string(name)}, {"value", ForDisplay(applied)}, {"previous", ForDisplay(previous)}});
    if (!EquivalentOperationValue(value, applied)) {
        result.data["adjusted"] = true;
        result.details.push_back("requested " + ValueText(value) + ", applied " + ValueText(applied));
    }
    return result;
}

// 一覧は人がコンソールで読む行にする（結果の data はコンソールが短く切るので、行は details に並べる）。
EditorOperationResult ListVariables(const std::string& prefix) {
    std::vector<std::string> lines;
    for (const VariableGroup& group : Groups()) {
        for (const Variable& variable : group.variables) {
            if (variable.name.starts_with(prefix)) {
                lines.push_back(DescribeLine(group, variable));
            }
        }
    }
    EditorOperationResult result = OperationOk(
        std::to_string(lines.size()) + (prefix.empty() ? " variables." : " variables starting with " + prefix),
        {{"count", lines.size()}});
    result.details = std::move(lines);
    return result;
}

} // namespace

void RegisterConsoleVariableOperations(EditorOperationRegistry& registry) {
    nlohmann::json names = nlohmann::json::array();
    for (const VariableGroup& group : Groups()) {
        for (const Variable& variable : group.variables) {
            names.push_back(variable.name);
        }
    }
    // 登録表の検証とコンソールの補完が、この一覧を同じように使う。
    nlohmann::json nameSchema = nlohmann::json::object();
    nameSchema["enum"] = std::move(names);
    const EditorOperationParameter name{"name", EditorOperationParameterType::String, true,
                                        "Variable: r.* render quality, gfx.* graphics diagnostics", nameSchema};

    // 種別で権限も決まる（Query は読むだけ、Session は今の設定を変える）。
    const auto define = [&registry](EditorOperationKind kind, EditorOperationDefinition definition) {
        definition.permission = kind == EditorOperationKind::Session ? EditorOperationPermission::ModifyEditorState
                                                                     : EditorOperationPermission::ReadEditorState;
        definition.searchTerms = {"cvar", "console variable", "setting", "quality", "控制台变量", "画质"};
        registry.Register(std::move(definition), kind, false);
    };
    // r.* は画質面板と同じ今の画質を変える。ファイルには書かないが、面板の保存と Play ボタンが今の画質を保存する。
    define(EditorOperationKind::Session,
           {"set",
            "Set a console variable (r.* render quality, gfx.* graphics diagnostics) in the live session. set writes "
            "no file, but r.* is the render quality the Quality panel edits: it goes into "
            "ProjectSettings/render_quality.json when the panel saves or the Play button starts a preview. DLSS frame "
            "generation loads only at startup. cvars lists names, values and ranges.",
            "general",
            {name,
             {"value", EditorOperationParameterType::Json, true,
              "New value: a number, true/false or an enum name (cvars shows the choices)"}},
            [](const nlohmann::json& p) { return AssignVariable(p.at("name").get<std::string>(), &p.at("value")); }});
    define(EditorOperationKind::Query,
           {"get",
            "Read one console variable: its current and default value.",
            "general",
            {name},
            [](const nlohmann::json& p) { return ReadVariable(p.at("name").get<std::string>()); }});
    define(EditorOperationKind::Session,
           {"reset",
            "Restore one console variable to its built-in default in the live session (kept or saved like set).",
            "general",
            {name},
            [](const nlohmann::json& p) { return AssignVariable(p.at("name").get<std::string>(), nullptr); }});
    define(EditorOperationKind::Query,
           {"cvars",
            "List console variables with their values, choices or ranges, and units. prefix narrows the list, for "
            "example r.directionalShadow.",
            "general",
            {{"prefix", EditorOperationParameterType::String, false, "Only names that start with this"}},
            [](const nlohmann::json& p) { return ListVariables(p.value("prefix", std::string{})); }});
}

} // namespace HIKARI::EDITOR
