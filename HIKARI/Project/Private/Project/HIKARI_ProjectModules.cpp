#include "Project/HIKARI_ProjectModules.h"

#include <algorithm>
#include <charconv>
#include <map>
#include <string_view>
#include <system_error>
#include <utility>

#include <json.hpp>

#include "Core/IO/HIKARI_PathNormalization.h"
#include "Core/Paths/HIKARI_ExecutablePath.h"
#include "HIKARI/Project/GameProjectSdk.h"

#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Serialization/Json/HIKARI_JsonStrict.h"
#include "Core/Text/HIKARI_OutMessage.h"
#include "Core/Text/HIKARI_UtfPath.h"

namespace HIKARI {
namespace {

std::filesystem::path ReadRelativePath(const std::string& text) {
    return text.empty() ? std::filesystem::path{} : TEXT::PathFromUtf8(text);
}

// 参照からモジュールの清単を決める：明示の路径、項目の Plugins、引擎に同梱のフォルダの順。
std::filesystem::path FindModuleManifest(const std::vector<ProjectModuleReference>& references, const std::string& id,
                                         const std::filesystem::path& projectRoot,
                                         const std::filesystem::path& builtinRoot, ModuleSource& outSource) {
    const auto reference = std::find_if(references.begin(), references.end(),
                                        [&](const ProjectModuleReference& item) { return item.id == id; });
    if (reference != references.end() && !reference->manifest.empty()) {
        outSource = ModuleSource::ManifestPath;
        return reference->manifest.is_absolute() ? reference->manifest
                                                 : (projectRoot / reference->manifest).lexically_normal();
    }
    const std::filesystem::path projectManifest = MakeModuleManifestPath(projectRoot / kProjectPluginsDirectory, id);
    std::error_code ec{};
    if (std::filesystem::is_regular_file(projectManifest, ec)) {
        outSource = ModuleSource::ProjectPlugins;
        return projectManifest;
    }
    outSource = ModuleSource::Engine;
    return MakeModuleManifestPath(builtinRoot, id);
}

class ModuleResolver {
public:
    ModuleResolver(const std::vector<ProjectModuleReference>& references, std::filesystem::path projectRoot,
                   std::filesystem::path builtinRoot)
        : references_(references), projectRoot_(std::move(projectRoot)), builtinRoot_(std::move(builtinRoot)) {}

    bool Resolve(std::vector<ModuleManifest>& outModules, std::string& outMessage) {
        for (const ProjectModuleReference& reference : references_) {
            if (!Visit(reference.id, outMessage)) {
                return false;
            }
        }
        outModules = std::move(ordered_);
        return true;
    }

private:
    enum class Mark { Visiting, Done };

    // 依存を先に積む深さ優先。
    bool Visit(const std::string& id, std::string& outMessage) {
        const auto mark = marks_.find(id);
        if (mark != marks_.end()) {
            if (mark->second == Mark::Visiting) {
                outMessage = "module dependency cycle at " + id;
                return false;
            }
            return true;
        }
        marks_[id] = Mark::Visiting;
        ModuleSource source{};
        const std::filesystem::path manifestPath =
            FindModuleManifest(references_, id, projectRoot_, builtinRoot_, source);
        ModuleManifest manifest{};
        std::string readMessage{};
        if (!LoadModuleManifest(manifestPath, manifest, &readMessage)) {
            outMessage = "module " + id + " could not be read (" + TEXT::PathToUtf8(manifestPath) + "): " + readMessage;
            return false;
        }
        if (manifest.id != id) {
            outMessage = "module manifest " + TEXT::PathToUtf8(manifestPath) + " declares id " + manifest.id +
                         " but the project expects " + id;
            return false;
        }
        for (const std::string& dependency : manifest.dependencies) {
            if (!Visit(dependency, outMessage)) {
                return false;
            }
        }
        marks_[id] = Mark::Done;
        ordered_.push_back(std::move(manifest));
        return true;
    }

    const std::vector<ProjectModuleReference>& references_;
    std::filesystem::path projectRoot_{};
    std::filesystem::path builtinRoot_{};
    std::map<std::string, Mark> marks_{};
    std::vector<ModuleManifest> ordered_{};
};

} // namespace

bool LoadModuleManifest(const std::filesystem::path& manifestPath, ModuleManifest& outManifest,
                        std::string* outMessage) {
    nlohmann::json root{};
    std::string error{};
    if (!SERIALIZATION::JSON::ReadJsonFile(manifestPath, root, &error) || !root.is_object()) {
        SetMessage(outMessage, error.empty() ? "module manifest root must be an object" : error);
        return false;
    }
    if (!JSONSTRICT::RequireFormatVersion(root, kModuleManifestFormatVersion, error)) {
        SetMessage(outMessage, error);
        return false;
    }
    ModuleManifest manifest{};
    std::string runtimeLibrary{};
    std::string shippingRuntimeLibrary{};
    std::string editorLibrary{};
    if (!JSONSTRICT::ReadString(root, "id", {}, manifest.id, error, "root") ||
        !JSONSTRICT::ReadString(root, "name", {}, manifest.name, error, "root") ||
        !JSONSTRICT::ReadString(root, "version", {}, manifest.version, error, "root") ||
        !JSONSTRICT::ReadString(root, "sdkVersion", {}, manifest.sdkVersion, error, "root") ||
        !JSONSTRICT::ReadString(root, "runtimeLibrary", {}, runtimeLibrary, error, "root") ||
        !JSONSTRICT::ReadString(root, "shippingRuntimeLibrary", {}, shippingRuntimeLibrary, error, "root") ||
        !JSONSTRICT::ReadString(root, "editorLibrary", {}, editorLibrary, error, "root")) {
        SetMessage(outMessage, error);
        return false;
    }
    if (manifest.id.empty() || manifest.version.empty() || manifest.sdkVersion.empty() || runtimeLibrary.empty()) {
        SetMessage(outMessage, "module manifest requires id, version, sdkVersion and runtimeLibrary (build the module "
                               "with hikari_add_module, which writes the packaged manifest)");
        return false;
    }
    if (const auto dependencies = root.find("dependencies"); dependencies != root.end()) {
        if (!dependencies->is_array()) {
            SetMessage(outMessage, "root.dependencies must be an array");
            return false;
        }
        for (const nlohmann::json& dependency : *dependencies) {
            if (!dependency.is_string() || dependency.get<std::string>().empty()) {
                SetMessage(outMessage, "root.dependencies entries must be module ids");
                return false;
            }
            manifest.dependencies.push_back(dependency.get<std::string>());
        }
    }
    if (const auto directories = root.find("runtimeDirectories"); directories != root.end()) {
        if (!directories->is_array()) {
            SetMessage(outMessage, "root.runtimeDirectories must be an array");
            return false;
        }
        for (const nlohmann::json& entry : *directories) {
            std::filesystem::path directory =
                entry.is_string() ? ReadRelativePath(entry.get<std::string>()) : std::filesystem::path{};
            if (!PATHS::IsSafeRelativePath(directory)) {
                SetMessage(outMessage, "root.runtimeDirectories entries must be folders inside the module package");
                return false;
            }
            manifest.runtimeDirectories.push_back(std::move(directory));
        }
    }
    if (const auto assetTypes = root.find("assetTypes"); assetTypes != root.end()) {
        if (!assetTypes->is_array()) {
            SetMessage(outMessage, "root.assetTypes must be an array");
            return false;
        }
        for (const nlohmann::json& entry : *assetTypes) {
            ModuleManifestAssetType type{};
            if (!entry.is_object() || !JSONSTRICT::ReadString(entry, "typeId", {}, type.typeId, error, "assetTypes") ||
                !JSONSTRICT::ReadString(entry, "extension", {}, type.extension, error, "assetTypes") ||
                !JSONSTRICT::ReadString(entry, "displayName", {}, type.displayName, error, "assetTypes")) {
                SetMessage(outMessage, error.empty() ? "root.assetTypes entries must be objects" : error);
                return false;
            }
            manifest.assetTypes.push_back(std::move(type));
        }
    }
    manifest.runtimeLibrary = ReadRelativePath(runtimeLibrary);
    manifest.shippingRuntimeLibrary = ReadRelativePath(shippingRuntimeLibrary);
    manifest.editorLibrary = ReadRelativePath(editorLibrary);
    std::error_code ec{};
    manifest.directory = std::filesystem::absolute(manifestPath, ec).parent_path().lexically_normal();
    outManifest = std::move(manifest);
    SetMessage(outMessage, {});
    return true;
}

bool SaveModuleManifest(const std::filesystem::path& manifestPath, const ModuleManifest& manifest,
                        std::string* outMessage) {
    nlohmann::json root{{"formatVersion", kModuleManifestFormatVersion},
                        {"id", manifest.id},
                        {"name", manifest.name},
                        {"version", manifest.version},
                        {"sdkVersion", manifest.sdkVersion},
                        {"runtimeLibrary", TEXT::PathToUtf8(manifest.runtimeLibrary)},
                        {"dependencies", manifest.dependencies}};
    const auto writeOptional = [&root](const char* key, const std::filesystem::path& value) {
        if (!value.empty()) {
            root[key] = TEXT::PathToUtf8(value);
        }
    };
    writeOptional("shippingRuntimeLibrary", manifest.shippingRuntimeLibrary);
    writeOptional("editorLibrary", manifest.editorLibrary);
    if (!manifest.runtimeDirectories.empty()) {
        nlohmann::json directories = nlohmann::json::array();
        for (const std::filesystem::path& directory : manifest.runtimeDirectories) {
            directories.push_back(TEXT::PathToUtf8(directory));
        }
        root["runtimeDirectories"] = std::move(directories);
    }
    if (!manifest.assetTypes.empty()) {
        nlohmann::json assetTypes = nlohmann::json::array();
        for (const ModuleManifestAssetType& type : manifest.assetTypes) {
            assetTypes.push_back(
                {{"typeId", type.typeId}, {"extension", type.extension}, {"displayName", type.displayName}});
        }
        root["assetTypes"] = std::move(assetTypes);
    }
    return SERIALIZATION::JSON::WriteJsonFile(manifestPath, root, outMessage);
}

std::filesystem::path MakeModuleManifestPath(const std::filesystem::path& root, const std::string& id) {
    return root / TEXT::PathFromUtf8(id) / TEXT::PathFromUtf8(id + kModuleManifestExtension);
}

std::filesystem::path GetEngineModulesDirectory() {
    return PLATFORM::GetExecutableDirectory() / kEngineModulesDirectory;
}

bool IsModuleSdkCompatible(const ModuleManifest& manifest, std::string* outMessage) {
    // 先頭の "major.minor" だけを読む。公開ヘッダーが変わると minor が上がり、DLL の仮想関数表が合わなくなる。
    const auto readNumber = [](std::string_view& text, unsigned int& outValue) {
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), outValue);
        if (error != std::errc{}) {
            return false;
        }
        text.remove_prefix(static_cast<size_t>(end - text.data()));
        return true;
    };
    std::string_view rest = manifest.sdkVersion;
    unsigned int major = 0u;
    unsigned int minor = 0u;
    bool parsed = readNumber(rest, major) && rest.starts_with('.');
    if (parsed) {
        rest.remove_prefix(1u);
        parsed = readNumber(rest, minor);
    }
    if (parsed && major == SDK::kVersionMajor && minor == SDK::kVersionMinor) {
        SetMessage(outMessage, {});
        return true;
    }
    SetMessage(outMessage, "module " + manifest.id + " was built for HIKARI SDK " +
                               (manifest.sdkVersion.empty() ? std::string("(unknown)") : manifest.sdkVersion) +
                               ", but this engine provides SDK " + std::to_string(SDK::kVersionMajor) + "." +
                               std::to_string(SDK::kVersionMinor) + "." + std::to_string(SDK::kVersionPatch) +
                               ". Rebuild the module with this engine's SDK, or get a package made for it.");
    return false;
}

std::vector<ProjectModuleOption> ListProjectModuleOptions(const ProjectDescriptor& descriptor,
                                                          const std::filesystem::path& projectRoot,
                                                          const std::filesystem::path& builtinRoot) {
    std::vector<ProjectModuleOption> options{};
    const auto find = [&options](const std::string& id) {
        return std::find_if(options.begin(), options.end(),
                            [&](const ProjectModuleOption& option) { return option.manifest.id == id; });
    };
    const auto add = [&options](const std::filesystem::path& manifestPath, const std::string& id, ModuleSource source,
                                bool enabled) {
        ProjectModuleOption option{};
        option.source = source;
        option.enabled = enabled;
        if (!LoadModuleManifest(manifestPath, option.manifest, &option.problem)) {
            option.manifest.directory = manifestPath.parent_path();
        } else if (option.manifest.id != id) {
            option.problem = "the manifest declares id " + option.manifest.id;
        } else {
            IsModuleSdkCompatible(option.manifest, &option.problem);
        }
        option.manifest.id = id;
        options.push_back(std::move(option));
    };

    for (const ProjectModuleReference& reference : descriptor.modules) {
        if (find(reference.id) == options.end()) {
            ModuleSource source{};
            const std::filesystem::path manifestPath =
                FindModuleManifest(descriptor.modules, reference.id, projectRoot, builtinRoot, source);
            add(manifestPath, reference.id, source, true);
        }
    }
    const std::pair<std::filesystem::path, ModuleSource> roots[] = {
        {projectRoot / kProjectPluginsDirectory, ModuleSource::ProjectPlugins},
        {builtinRoot, ModuleSource::Engine},
    };
    for (const auto& [root, source] : roots) {
        std::error_code ec{};
        for (std::filesystem::directory_iterator entry(root, ec), end; !ec && entry != end; entry.increment(ec)) {
            const std::string id = TEXT::PathToUtf8(entry->path().filename());
            const std::filesystem::path manifestPath = MakeModuleManifestPath(root, id);
            if (find(id) == options.end() && std::filesystem::is_regular_file(manifestPath, ec)) {
                add(manifestPath, id, source, false);
            }
        }
    }

    // 有効なモジュールから依存を辿り、読み込まれるもの全部について「誰が要るか」を記す。
    std::vector<std::string> pending{};
    for (const ProjectModuleOption& option : options) {
        if (option.enabled) {
            pending.push_back(option.manifest.id);
        }
    }
    std::vector<std::string> visited{};
    while (!pending.empty()) {
        const std::string id = std::move(pending.back());
        pending.pop_back();
        const auto owner = find(id);
        if (owner == options.end() || std::find(visited.begin(), visited.end(), id) != visited.end()) {
            continue;
        }
        visited.push_back(id);
        for (const std::string& dependency : owner->manifest.dependencies) {
            if (const auto required = find(dependency); required != options.end()) {
                required->requiredBy.push_back(id);
                pending.push_back(dependency);
            } else if (owner->problem.empty()) {
                owner->problem = "requires module " + dependency +
                                 ", which is in neither the project's Plugins folder nor the engine";
            }
        }
    }

    std::sort(options.begin(), options.end(), [](const ProjectModuleOption& left, const ProjectModuleOption& right) {
        return left.manifest.id < right.manifest.id;
    });
    return options;
}

bool SetProjectModuleEnabled(const std::filesystem::path& projectRoot, const std::filesystem::path& builtinRoot,
                             const std::string& id, bool enabled, std::string* outMessage) {
    const std::filesystem::path descriptorPath = projectRoot / kProjectDescriptorFilename;
    ProjectDescriptor descriptor{};
    if (!LoadProjectDescriptor(descriptorPath, descriptor, outMessage)) {
        return false;
    }
    const std::vector<ProjectModuleOption> options = ListProjectModuleOptions(descriptor, projectRoot, builtinRoot);
    const auto option = std::find_if(options.begin(), options.end(),
                                     [&](const ProjectModuleOption& item) { return item.manifest.id == id; });
    if (option == options.end()) {
        SetMessage(outMessage, "unknown module " + id + ": put its package folder into the project's " +
                                   kProjectPluginsDirectory + " folder");
        return false;
    }
    if (option->enabled == enabled) {
        SetMessage(outMessage, {});
        return true;
    }
    if (enabled) {
        if (!option->problem.empty()) {
            SetMessage(outMessage, option->problem);
            return false;
        }
        descriptor.modules.push_back({id, {}});
    } else {
        if (!option->requiredBy.empty()) {
            SetMessage(outMessage, "module " + id + " is required by " + option->requiredBy.front());
            return false;
        }
        std::erase_if(descriptor.modules, [&](const ProjectModuleReference& reference) { return reference.id == id; });
    }
    return SaveProjectDescriptor(descriptorPath, descriptor, outMessage);
}

bool ResolveProjectModules(const std::vector<ProjectModuleReference>& references,
                           const std::filesystem::path& projectRoot, const std::filesystem::path& builtinRoot,
                           std::vector<ModuleManifest>& outModules, std::string* outMessage) {
    std::string message{};
    std::vector<ModuleManifest> resolved{};
    if (!ModuleResolver(references, projectRoot, builtinRoot).Resolve(resolved, message)) {
        SetMessage(outMessage, message);
        return false;
    }
    outModules = std::move(resolved);
    SetMessage(outMessage, {});
    return true;
}

} // namespace HIKARI
