#include "Assets/Shader/HIKARI_ShaderCookedArtifactBuilder.h"

#include <algorithm>
#include <span>
#include <unordered_map>
#include <utility>

namespace HIKARI::ASSETS::SHADER {
namespace {

GFX::ShaderStage ToGfxStage(ShaderStage stage) noexcept {
    switch (stage) {
    case ShaderStage::Vertex: return GFX::ShaderStage::Vertex;
    case ShaderStage::Pixel: return GFX::ShaderStage::Pixel;
    case ShaderStage::Compute: return GFX::ShaderStage::Compute;
    case ShaderStage::Amplification: return GFX::ShaderStage::Amplification;
    case ShaderStage::Mesh: return GFX::ShaderStage::Mesh;
    case ShaderStage::Geometry: return GFX::ShaderStage::Geometry;
    case ShaderStage::Count: break;
    }
    return GFX::ShaderStage::Pixel;
}

GFX::ShaderCookIncludeRoot ToGfxRoot(ShaderIncludeRoot root) noexcept {
    switch (root) {
    case ShaderIncludeRoot::Project: return GFX::ShaderCookIncludeRoot::Project;
    case ShaderIncludeRoot::EngineSdk: return GFX::ShaderCookIncludeRoot::EngineSdk;
    case ShaderIncludeRoot::Count: break;
    }
    return GFX::ShaderCookIncludeRoot::Count;
}

ShaderResourceBindingKind ToArtifactBindingKind(GFX::ShaderCookBindingKind kind) noexcept {
    switch (kind) {
    case GFX::ShaderCookBindingKind::ConstantBuffer: return ShaderResourceBindingKind::ConstantBuffer;
    case GFX::ShaderCookBindingKind::ShaderResource: return ShaderResourceBindingKind::ShaderResource;
    case GFX::ShaderCookBindingKind::UnorderedAccess: return ShaderResourceBindingKind::UnorderedAccess;
    case GFX::ShaderCookBindingKind::Sampler: return ShaderResourceBindingKind::Sampler;
    case GFX::ShaderCookBindingKind::Count: break;
    }
    return ShaderResourceBindingKind::Count;
}

std::string_view LogicalRootPrefix(ShaderIncludeRoot root) noexcept {
    switch (root) {
    case ShaderIncludeRoot::Project: return "Project/";
    case ShaderIncludeRoot::EngineSdk: return "EngineSdk/";
    case ShaderIncludeRoot::Count: break;
    }
    return {};
}

} // namespace

bool BuildShaderCookRequest(const ShaderSourceManifest& manifest, ShaderIncludeRoot primarySourceRoot,
                            const std::filesystem::path& projectShaderRoot,
                            const std::filesystem::path& engineSdkShaderRoot, GFX::ShaderCookRequest& outRequest,
                            std::string& outError) {
    if (primarySourceRoot >= ShaderIncludeRoot::Count) {
        outError = "shader primary source root is invalid";
        return false;
    }
    GFX::ShaderCookRequest request{};
    request.projectShaderRoot = projectShaderRoot;
    request.engineSdkIncludeRoot = engineSdkShaderRoot;
    request.primarySourceRoot = ToGfxRoot(primarySourceRoot);
    request.sourcePath = manifest.sourcePath;
    request.entries.reserve(manifest.entries.size());
    for (const ShaderEntryPoint& entry : manifest.entries) {
        request.entries.push_back({entry.id, ToGfxStage(entry.stage), entry.entry, entry.profile});
    }
    request.defines.reserve(manifest.defines.size());
    for (const ShaderDefine& define : manifest.defines) {
        request.defines.push_back({define.name, define.value});
    }
    request.includeRoots.reserve(manifest.includeRoots.size());
    bool needsProject = primarySourceRoot == ShaderIncludeRoot::Project;
    bool needsEngineSdk = primarySourceRoot == ShaderIncludeRoot::EngineSdk;
    for (ShaderIncludeRoot root : manifest.includeRoots) {
        request.includeRoots.push_back(ToGfxRoot(root));
        needsProject = needsProject || root == ShaderIncludeRoot::Project;
        needsEngineSdk = needsEngineSdk || root == ShaderIncludeRoot::EngineSdk;
    }
    request.engineInterface = {manifest.engineInterfaceId, manifest.engineInterfaceVersion,
                               manifest.engineInterfaceHash};
    request.rootLayout = {manifest.rootLayoutId, manifest.rootLayoutVersion, manifest.rootLayoutHash};
    request.requiredCaps = {manifest.requiredCaps.minimumShaderModel, manifest.requiredCaps.minimumRootSignatureVersion,
                            manifest.requiredCaps.requiredFeatureMask};
    if ((needsProject && !request.projectShaderRoot.is_absolute()) ||
        (needsEngineSdk && !request.engineSdkIncludeRoot.is_absolute())) {
        outError = "shader include root is not initialized";
        return false;
    }
    outRequest = std::move(request);
    outError.clear();
    return true;
}

bool BuildShaderCookedArtifact(const AssetGuid& sourceGuid, const ShaderSourceManifest& manifest,
                               const HASH::Sha256Digest& manifestHash, ShaderIncludeRoot primarySourceRoot,
                               const GFX::ShaderCookOutput& compiled, ShaderCookedArtifact& outArtifact,
                               std::string& outError) {
    const std::string_view rootPrefix = LogicalRootPrefix(primarySourceRoot);
    if (!sourceGuid.IsValid() || rootPrefix.empty() || !compiled.success ||
        compiled.toolchain.contractVersion != kShaderCompilerContractVersion ||
        compiled.entries.size() != manifest.entries.size() ||
        compiled.engineInterface.id != manifest.engineInterfaceId ||
        compiled.engineInterface.version != manifest.engineInterfaceVersion ||
        compiled.engineInterface.hash != manifest.engineInterfaceHash ||
        compiled.rootLayout.id != manifest.rootLayoutId || compiled.rootLayout.version != manifest.rootLayoutVersion ||
        compiled.rootLayout.hash != manifest.rootLayoutHash ||
        compiled.requiredCaps.minimumShaderModel != manifest.requiredCaps.minimumShaderModel ||
        compiled.requiredCaps.minimumRootSignatureVersion != manifest.requiredCaps.minimumRootSignatureVersion ||
        compiled.requiredCaps.requiredFeatureMask != manifest.requiredCaps.requiredFeatureMask) {
        outError = "shader compiler output identity is invalid";
        return false;
    }

    ShaderCookedArtifact artifact{};
    artifact.sourceGuid = sourceGuid;
    artifact.programId = manifest.programId;
    artifact.manifestHash = manifestHash;
    artifact.compiler = {compiled.toolchain.compilerId, compiled.toolchain.compilerVersion,
                         compiled.toolchain.contractVersion, compiled.toolchain.compilerBinaryHash,
                         compiled.toolchain.validatorBinaryHash};
    artifact.engineInterfaceId = compiled.engineInterface.id;
    artifact.engineInterfaceVersion = compiled.engineInterface.version;
    artifact.engineInterfaceHash = compiled.engineInterface.hash;
    artifact.rootLayoutId = compiled.rootLayout.id;
    artifact.rootLayoutVersion = compiled.rootLayout.version;
    artifact.rootLayoutHash = compiled.rootLayout.hash;
    artifact.requiredCaps = {compiled.requiredCaps.minimumShaderModel,
                             compiled.requiredCaps.minimumRootSignatureVersion,
                             compiled.requiredCaps.requiredFeatureMask};
    artifact.dependencies.reserve(compiled.dependencies.size());
    for (const GFX::ShaderCookDependency& dependency : compiled.dependencies) {
        artifact.dependencies.push_back({dependency.logicalPath, dependency.byteSize, dependency.contentHash});
    }
    const std::string mainLogicalPath =
        std::string(rootPrefix) + manifest.sourcePath.lexically_normal().generic_string();
    if (std::none_of(artifact.dependencies.begin(), artifact.dependencies.end(),
                     [&mainLogicalPath](const ShaderSourceDependency& dependency) {
                         return dependency.logicalPath == mainLogicalPath;
                     })) {
        outError = "manifest source path does not match its canonical file";
        return false;
    }
    if (!ComputeShaderSourceClosureHash(artifact.manifestHash, artifact.dependencies, artifact.sourceClosureHash)) {
        outError = "could not compute shader source closure hash";
        return false;
    }

    std::unordered_map<std::string, const ShaderEntryPoint*> entriesById{};
    for (const ShaderEntryPoint& entry : manifest.entries) {
        entriesById.emplace(entry.id, &entry);
    }
    artifact.entries.reserve(compiled.entries.size());
    for (const GFX::ShaderCookEntryOutput& compiledEntry : compiled.entries) {
        const auto found = entriesById.find(compiledEntry.id);
        if (found == entriesById.end()) {
            outError = "compiled shader entry is not in the manifest";
            return false;
        }
        ShaderCookedEntry entry{};
        entry.entry = *found->second;
        entry.compilerArguments = compiledEntry.canonicalArguments;
        entry.dxil = compiledEntry.dxil;
        entry.bindings.reserve(compiledEntry.bindings.size());
        for (const GFX::ShaderCookBinding& binding : compiledEntry.bindings) {
            entry.bindings.push_back({ToArtifactBindingKind(binding.kind), binding.registerIndex, binding.registerSpace,
                                      binding.count, binding.visibilityMask});
        }
        if (!ComputeShaderArgumentsHash(entry.compilerArguments, entry.compilerArgumentsHash) ||
            !ComputeShaderReflectionHash(entry.bindings, entry.reflectionHash) ||
            !HASH::ComputeSha256(std::as_bytes(std::span<const uint8_t>(entry.dxil)), entry.dxilHash)) {
            outError = "could not hash compiled shader entry";
            return false;
        }
        artifact.entries.push_back(std::move(entry));
    }
    if (!ValidateShaderCookedArtifact(artifact, outError)) {
        return false;
    }
    outArtifact = std::move(artifact);
    outError.clear();
    return true;
}

} // namespace HIKARI::ASSETS::SHADER
