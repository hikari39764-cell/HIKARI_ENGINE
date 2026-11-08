#include "Project/HIKARI_ProjectModules.h"

#include <algorithm>
#include <map>
#include <system_error>
#include <utility>

#include <json.hpp>

#include "Core/Serialization/Json/HIKARI_JsonFile.h"
#include "Core/Serialization/Json/HIKARI_JsonStrict.h"
#include "Core/Text/HIKARI_OutMessage.h"
#include "Core/Text/HIKARI_UtfPath.h"

namespace HIKARI {
namespace {

std::filesystem::path ReadRelativePath(const std::string& text) {
    return text.empty() ? std::filesystem::path{} : TEXT::PathFromUtf8(text);
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

    std::filesystem::path FindManifestPath(const std::string& id) const {
        const auto reference = std::find_if(references_.begin(), references_.end(),
                                            [&](const ProjectModuleReference& item) { return item.id == id; });
        if (reference == references_.end() || reference->manifest.empty()) {
            return MakeBuiltinModuleManifestPath(builtinRoot_, id);
        }
        return reference->manifest.is_absolute() ? reference->manifest
                                                 : (projectRoot_ / reference->manifest).lexically_normal();
    }

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
        const std::filesystem::path manifestPath = FindManifestPath(id);
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
    std::string contentDirectory{};
    if (!JSONSTRICT::ReadString(root, "id", {}, manifest.id, error, "root") ||
        !JSONSTRICT::ReadString(root, "name", {}, manifest.name, error, "root") ||
        !JSONSTRICT::ReadString(root, "version", {}, manifest.version, error, "root") ||
        !JSONSTRICT::ReadString(root, "runtimeLibrary", {}, runtimeLibrary, error, "root") ||
        !JSONSTRICT::ReadString(root, "shippingRuntimeLibrary", {}, shippingRuntimeLibrary, error, "root") ||
        !JSONSTRICT::ReadString(root, "editorLibrary", {}, editorLibrary, error, "root") ||
        !JSONSTRICT::ReadString(root, "contentDirectory", {}, contentDirectory, error, "root")) {
        SetMessage(outMessage, error);
        return false;
    }
    if (manifest.id.empty() || manifest.version.empty() || runtimeLibrary.empty()) {
        SetMessage(outMessage, "module manifest requires id, version and runtimeLibrary");
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
    manifest.runtimeLibrary = ReadRelativePath(runtimeLibrary);
    manifest.shippingRuntimeLibrary = ReadRelativePath(shippingRuntimeLibrary);
    manifest.editorLibrary = ReadRelativePath(editorLibrary);
    manifest.contentDirectory = ReadRelativePath(contentDirectory);
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
                        {"runtimeLibrary", TEXT::PathToUtf8(manifest.runtimeLibrary)},
                        {"dependencies", manifest.dependencies}};
    const auto writeOptional = [&root](const char* key, const std::filesystem::path& value) {
        if (!value.empty()) {
            root[key] = TEXT::PathToUtf8(value);
        }
    };
    writeOptional("shippingRuntimeLibrary", manifest.shippingRuntimeLibrary);
    writeOptional("editorLibrary", manifest.editorLibrary);
    writeOptional("contentDirectory", manifest.contentDirectory);
    return SERIALIZATION::JSON::WriteJsonFile(manifestPath, root, outMessage);
}

std::filesystem::path MakeBuiltinModuleManifestPath(const std::filesystem::path& builtinRoot, const std::string& id) {
    return builtinRoot / TEXT::PathFromUtf8(id) / TEXT::PathFromUtf8(id + kModuleManifestExtension);
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
