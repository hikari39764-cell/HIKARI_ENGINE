#include "Editor/ShaderPlayground/HIKARI_ShaderPlaygroundOperations.h"

#include <filesystem>
#include <string>

#include "Core/IO/HIKARI_PathNormalization.h"
#include "Core/Text/HIKARI_UtfPath.h"
#include "Editor/Commands/HIKARI_EditorOperationContext.h"
#include "Editor/ShaderPlayground/HIKARI_ShaderPlaygroundRunner.h"
#include "Project/HIKARI_ProjectContext.h"

namespace HIKARI::EDITOR {
namespace {

ShaderPlayRecord lastPlay;

EditorOperationResult Run(const std::string& specText) {
    const std::filesystem::path root = GetProjectContext().GetRoot();
    std::filesystem::path spec = TEXT::PathFromUtf8(specText);
    if (spec.is_relative())
        spec = root / spec;
    if (root.empty() || !PATHS::IsContainedPath(root, spec))
        return OperationFailure("INVALID_PATH", "spec must be a .json file inside the open project.");
    nlohmann::json summary;
    std::string error;
    if (PLAYGROUND::RunShaderPlayground(spec, GetProjectContext().GetLibraryRoot() / "ShaderPlayground", summary,
                                        error)) {
        const std::string message =
            "Rendered " + summary.value("name", std::string{}) + " to " + summary.value("output", std::string{}) + ".";
        return OperationOk(message, std::move(summary));
    }
    // 着色器の編訳の失敗は、書き出せた要約（診断を含む）も返す。
    EditorOperationResult result =
        OperationFailure(summary.empty() ? "PLAYGROUND_FAILED" : "SHADER_COMPILE_FAILED", error);
    if (!summary.empty())
        result.data = std::move(summary);
    return result;
}

EditorOperationResult Play(const nlohmann::json& parameters) {
    lastPlay.spec = parameters.value("spec", std::string{});
    EditorOperationResult result = Run(lastPlay.spec);
    lastPlay.output = result.data.contains("output") ? TEXT::PathFromUtf8(result.data["output"].get<std::string>())
                                                     : std::filesystem::path{};
    lastPlay.ok = result.ok;
    lastPlay.message = result.message;
    ++lastPlay.revision;
    return result;
}

} // namespace

const ShaderPlayRecord& GetLastShaderPlay() { return lastPlay; }

void RegisterShaderPlaygroundOperations(EditorOperationRegistry& registry) {
    EditorOperationDefinition play{
        "shader.play",
        "Try a shader without a scene: render a sheet of tiles (sphere, white furnace, uv, image or curve) of a "
        "project .hlsl that includes Include/Playground/HIKARI_Playground.hlsli and defines float4 Play(PlaygroundSample "
        "s), or of the render-style shading models when the spec has no source. Rows come from the spec's tiles, "
        "columns from its sweep. Writes sheet.png, issues.png (NaN/Inf magenta, negative cyan, over maxValue "
        "orange), diff.png against the spec's compare shader, diff-last.png against the previous run and result.json "
        "(per-tile mean/min/max and issue counts, GPU time on hardware) to Library/ShaderPlayground/<name>. The "
        "spec format is documented in Editor/ShaderPlayground/HIKARI_ShaderPlaygroundSpec.h.",
        "general",
        {{"spec", EditorOperationParameterType::String, true,
          "Playground spec (.json): a project-relative or absolute path inside the project."}},
        [](const nlohmann::json& parameters) { return Play(parameters); }};
    play.requiresDocumentSession = false;
    play.searchTerms = {"shader", "playground", "hlsl", "material test", "着色器", "试验", "材质球"};
    registry.Register(std::move(play), EditorOperationKind::Session, false);
}

} // namespace HIKARI::EDITOR
