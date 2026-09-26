#include "Editor/Workspaces/Vfx/HIKARI_VfxOperationSections.h"
#include "Editor/Workspaces/Vfx/HIKARI_VfxOperationSupport.h"
#include "Core/Text/HIKARI_UtfPath.h"

namespace HIKARI::EDITOR {
void RegisterVfxLibraryOperations(EditorOperationRegistry& registry, VfxWorkspaceController& controller,
                                  std::function<AssetDatabase*()> databaseProvider) {
    using namespace VFXOPERATIONS;
    registry.Register({"vfx.library.open",
                       "Select and scan a local shared content library",
                       "vfx",
                       {{"path", EditorOperationParameterType::String, true, "Existing content library folder"}},
                       [&controller, databaseProvider](const nlohmann::json& parameters) {
                           EditorOperationResult result;
                           auto* database = RequireDatabase(databaseProvider, result);
                           if (!database)
                               return result;
                           std::string message;
                           if (!controller.SetContentLibraryRoot(
                                   *database, TEXT::PathFromUtf8(parameters.at("path").get<std::string>()), message))
                               return OperationFailure("CONTENT_LIBRARY_FAILED", message);
                           return OperationOk(message);
                       }},
                      HIKARI::EDITOR::EditorOperationKind::Session);
    registry.Register({"vfx.library.list",
                       "List packages from the selected shared content library",
                       "vfx",
                       {},
                       [&controller](const nlohmann::json&) {
                           nlohmann::json packages = nlohmann::json::array();
                           for (const auto& package : controller.GetLibraryPackages())
                               packages.push_back({{"id", package.id},
                                                   {"revision", package.version},
                                                   {"name", package.name},
                                                   {"category", package.category},
                                                   {"manifest", TEXT::PathToUtf8(package.manifestPath)},
                                                   {"assetCount", package.assetCount}});
                           return OperationOk("Shared packages", {{"packages", std::move(packages)}});
                       }},
                      HIKARI::EDITOR::EditorOperationKind::Query, false);
    registry.Register(
        {"vfx.library.import",
         "Copy a package and its dependencies into the project, preserving local edits",
         "vfx",
         {{"manifest", EditorOperationParameterType::String, true, "Package manifest path from vfx.library.list"}},
         [&controller, databaseProvider](const nlohmann::json& parameters) {
             EditorOperationResult result;
             auto* database = RequireDatabase(databaseProvider, result);
             if (!database)
                 return result;
             ASSETS::LIBRARY::ContentImportResult imported;
             std::string message;
             if (!controller.ImportLibraryPackage(
                     *database, TEXT::PathFromUtf8(parameters.at("manifest").get<std::string>()), imported, message))
                 return OperationFailure("CONTENT_IMPORT_FAILED", message);
             nlohmann::json roots = nlohmann::json::array();
             for (const auto& root : imported.roots)
                 roots.push_back(root.value);
             return OperationOk(message, {{"roots", std::move(roots)}, {"reused", imported.reused}});
         }},
        HIKARI::EDITOR::EditorOperationKind::Mutation);
    registry.Register({"vfx.library.publish",
                       "Save the open effect and publish a new immutable source package revision",
                       "vfx",
                       {{"id", EditorOperationParameterType::String, true, "Portable lowercase package ID"},
                        {"revision", EditorOperationParameterType::String, true,
                         "New revision; existing revisions cannot be replaced"},
                        {"category", EditorOperationParameterType::String, false, "Library category"},
                        {"license", EditorOperationParameterType::String, false, "Credits and redistribution terms"}},
                       [&controller, databaseProvider](const nlohmann::json& parameters) {
                           EditorOperationResult result;
                           auto* database = RequireDatabase(databaseProvider, result);
                           if (!database)
                               return result;
                           ASSETS::LIBRARY::ContentPackageInfo info;
                           info.id = parameters.at("id").get<std::string>();
                           info.version = parameters.at("revision").get<std::string>();
                           info.name = controller.GetDocument().Asset().name;
                           info.category = parameters.value("category", "Team");
                           info.license = parameters.value("license", "");
                           std::string message;
                           return controller.PublishToLibrary(*database, info, message)
                                      ? OperationOk(message)
                                      : OperationFailure("CONTENT_PUBLISH_FAILED", message);
                       }},
                      HIKARI::EDITOR::EditorOperationKind::Mutation);
}
} // namespace HIKARI::EDITOR
