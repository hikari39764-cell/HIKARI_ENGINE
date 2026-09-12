#include "HIKARI_VfxAssetImporter.h"

#include <json.hpp>

#include "Assets/Formats/HIKARI_HvfxcFormat.h"
#include "Assets/Semantics/HIKARI_AssetArtifactSemantics.h"
#include "Assets/Vfx/Compiler/HIKARI_VfxCompiler.h"
#include "Assets/Vfx/Serialization/HIKARI_VfxSourceJson.h"
#include "Core/HIKARI_Logger.h"
#include "Project/Paths/HIKARI_ProjectPath.h"

namespace HIKARI {

    ASSETS::SEMANTICS::AssetImporterKind
        VfxAssetImporter::GetImporterKind() const noexcept {
        return ASSETS::SEMANTICS::AssetImporterKind::VfxEffect;
    }

    AssetMeta VfxAssetImporter::CreateDefaultMeta(
        const std::filesystem::path& sourcePath,
        const AssetGuid& guid) const {

        AssetMeta meta = ASSETS::SEMANTICS::MakeBaseAssetMeta(
            sourcePath,
            guid,
            GetImporterKind());
        meta.importSettingsJson = nlohmann::json{
            { "sourceFormat", sourcePath.extension().string() },
            { "outputFormat", "HVFXC" },
            { "cookVfx", true },
        }.dump(2);
        return meta;
    }

    AssetImportResult VfxAssetImporter::Import(
        const AssetRecord& record,
        const AssetImportContext& context) {

        AssetImportResult result{};
        if (!ASSETS::SEMANTICS::IsAssetSourceForImporter(
                record.sourcePath,
                GetImporterKind())) {
            result.message =
                "[VfxAssetImporter][ERROR] unsupported source: " +
                record.sourcePath.generic_string();
            return result;
        }

        const std::filesystem::path sourcePath =
            PROJECT_PATHS::ResolveProjectPath(
                context.projectRoot,
                record.sourcePath);
        VFX::VfxAssetData source{};
        std::string error{};
        if (!VFX::SERIALIZATION::LoadVfxSourceJson(
                sourcePath,
                source,
                error)) {
            result.message =
                "[VfxAssetImporter][ERROR] source read failed: " +
                error;
            HIKARI_LOG_ERROR(result.message);
            return result;
        }

        const VFX::VfxCompileResult compileResult =
            VFX::CompileVfxAsset(source);
        nlohmann::json diagnostics = nlohmann::json::array();
        for (const VFX::VfxDiagnostic& diagnostic :
             compileResult.diagnostics) {
            diagnostics.push_back({
                { "severity", static_cast<uint32_t>(
                    diagnostic.severity) },
                { "code", diagnostic.code },
                { "message", diagnostic.message },
                { "subjectId", diagnostic.subjectId },
            });
        }
        result.diagnosticsJson = nlohmann::json{
            { "success", compileResult.success },
            { "sourceHash", compileResult.program.sourceHash },
            { "diagnostics", std::move(diagnostics) },
        }.dump(2);
        if (!compileResult.success) {
            result.message =
                "[VfxAssetImporter][ERROR] .hvfx validation failed; "
                "no runtime program was published: " +
                record.sourcePath.generic_string();
            HIKARI_LOG_ERROR(result.message);
            return result;
        }

        const std::filesystem::path outputPath =
            (context.importedDirectory / "effect.hvfxc")
                .lexically_normal();
        if (!VFX::WriteHvfxcFile(
                context.StageFile(outputPath),
                compileResult.program,
                error)) {
            result.message =
                "[VfxAssetImporter][ERROR] cooked program write failed: " +
                error;
            HIKARI_LOG_ERROR(result.message);
            return result;
        }

        for (const AssetGuid& dependency :
             compileResult.program.dependencyGuids) {
            result.dependencies.push_back({
                dependency,
                {},
                "Vfx.RenderResource"
            });
        }
        result.artifacts.push_back(
            ASSETS::SEMANTICS::MakeAssetArtifact(
                ASSETS::SEMANTICS::AssetArtifactKind::VfxProgram,
                PROJECT_PATHS::MakeProjectRelativeString(
                    context.projectRoot,
                    outputPath)));
        result.success = true;
        result.message =
            "[VfxAssetImporter] cooked native GPU VFX program: " +
            PROJECT_PATHS::MakeProjectRelativeString(
                context.projectRoot,
                outputPath);
        HIKARI_LOG_INFO(result.message);
        return result;
    }

} // namespace HIKARI
