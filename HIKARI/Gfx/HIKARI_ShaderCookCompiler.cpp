#include "Gfx/HIKARI_ShaderCookCompiler.h"

#include <Windows.h>

#include <algorithm>
#include <exception>
#include <map>
#include <mutex>
#include <new>
#include <string_view>
#include <dxcapi.h>
#include <wrl/client.h>

#include "Core/Text/HIKARI_AsciiCase.h"
#include "Gfx/Internal/HIKARI_DxcRuntime.h"
#include "Gfx/Internal/HIKARI_ShaderCookDxil.h"
#include "Gfx/Internal/HIKARI_ShaderCookFileSystem.h"
#include "Gfx/Internal/HIKARI_ShaderIncludePlan.h"

namespace HIKARI::GFX {
    namespace {

        using Microsoft::WRL::ComPtr;

        bool IsContractId(std::string_view value) noexcept {
            if (value.empty() || value.size() > 128u) {
                return false;
            }
            for (size_t index = 0u; index < value.size(); ++index) {
                const char character = value[index];
                const bool alpha =
                    (character >= 'A' && character <= 'Z') ||
                    (character >= 'a' && character <= 'z');
                const bool digit =
                    character >= '0' && character <= '9';
                if ((!alpha && !digit && character != '_' &&
                     character != '.' && character != '-') ||
                    (index == 0u && digit)) {
                    return false;
                }
            }
            return true;
        }

        bool ValidateRequestIdentity(
            const ShaderCookRequest& request,
            std::string& outError) {
            if (!IsContractId(request.engineInterface.id) ||
                request.engineInterface.version == 0u ||
                request.engineInterface.hash.IsZero() ||
                !IsContractId(request.rootLayout.id) ||
                request.rootLayout.version == 0u ||
                request.rootLayout.hash.IsZero() ||
                (request.requiredCaps.minimumShaderModel != 60u &&
                 request.requiredCaps.minimumShaderModel != 65u) ||
                (request.requiredCaps.minimumRootSignatureVersion != 10u &&
                 request.requiredCaps.minimumRootSignatureVersion != 11u) ||
                (request.requiredCaps.requiredFeatureMask &
                 ~ShaderCookDeviceFeature::All) != 0u) {
                outError = "shader interface/root/capability identity is invalid";
                return false;
            }
            for (const ShaderCookEntryRequest& entry : request.entries) {
                const bool meshPipeline =
                    entry.stage == ShaderStage::Mesh ||
                    entry.stage == ShaderStage::Amplification;
                if (meshPipeline &&
                    (request.requiredCaps.minimumShaderModel < 65u ||
                     (request.requiredCaps.requiredFeatureMask &
                      ShaderCookDeviceFeature::MeshShader) == 0u)) {
                    outError =
                        "mesh shader entry exceeds declared capabilities";
                    return false;
                }
            }
            return true;
        }

        std::wstring WidenUtf8(std::string_view text) {
            if (text.empty()) {
                return {};
            }
            const int count = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                text.data(),
                static_cast<int>(text.size()),
                nullptr,
                0);
            if (count <= 0) {
                return {};
            }
            std::wstring wide(static_cast<size_t>(count), L'\0');
            if (MultiByteToWideChar(
                    CP_UTF8,
                    MB_ERR_INVALID_CHARS,
                    text.data(),
                    static_cast<int>(text.size()),
                    wide.data(),
                    count) != count) {
                return {};
            }
            return wide;
        }



        bool BuildActualDxcArguments(
            const std::string& logicalSource,
            const std::vector<std::string>& canonical,
            const std::vector<INTERNAL::ShaderCookRoot>& roots,
            std::vector<std::wstring>& outOwned,
            std::vector<LPCWSTR>& outArguments,
            std::string& outError) {
            std::map<std::string, std::filesystem::path> physicalRoots{};
            for (const INTERNAL::ShaderCookRoot& root : roots) {
                physicalRoots.emplace(root.logicalPrefix, root.physicalRoot);
            }
            std::vector<std::wstring> owned{};
            owned.reserve(canonical.size() + 1u);
            const size_t sourcePrefixEnd = logicalSource.find('/');
            const std::string_view sourceArgument =
                sourcePrefixEnd == std::string::npos
                    ? std::string_view(logicalSource)
                    : std::string_view(logicalSource).substr(
                        sourcePrefixEnd + 1u);
            owned.push_back(WidenUtf8(sourceArgument));
            if (owned.back().empty()) {
                outError = "logical shader source is not valid UTF-8";
                return false;
            }
            for (size_t index = 0u; index < canonical.size(); ++index) {
                if (canonical[index] == "-I" &&
                    index + 1u < canonical.size()) {
                    owned.push_back(L"-I");
                    const auto found = physicalRoots.find(
                        canonical[++index]);
                    if (found == physicalRoots.end()) {
                        outError = "canonical include root has no owner";
                        return false;
                    }
                    owned.push_back(found->second.wstring());
                }
                else {
                    owned.push_back(WidenUtf8(canonical[index]));
                }
                if (owned.back().empty()) {
                    outError = "DXC argument is not valid UTF-8";
                    return false;
                }
            }
            std::vector<LPCWSTR> arguments{};
            arguments.reserve(owned.size());
            for (const std::wstring& argument : owned) {
                arguments.push_back(argument.c_str());
            }
            outOwned = std::move(owned);
            outArguments = std::move(arguments);
            return true;
        }

        bool MergeDependency(
            const ShaderCookDependency& dependency,
            std::map<std::string, ShaderCookDependency>& closure,
            uint64_t& totalBytes,
            std::string& outError) {
            const std::string key =
                TEXT::ToLowerAsciiCopy(dependency.logicalPath);
            const auto found = closure.find(key);
            if (found != closure.end()) {
                if (found->second.logicalPath != dependency.logicalPath ||
                    found->second.byteSize != dependency.byteSize ||
                    found->second.contentHash != dependency.contentHash) {
                    outError =
                        "shader dependency changed or used ambiguous casing";
                    return false;
                }
                return true;
            }
            if (dependency.byteSize == 0u ||
                dependency.byteSize > kMaximumShaderCookSourceFileBytes ||
                dependency.byteSize >
                    kMaximumShaderCookClosureBytes - totalBytes ||
                closure.size() >= kMaximumShaderCookDependencies) {
                outError = "shader dependency closure exceeds its budget";
                return false;
            }
            totalBytes += dependency.byteSize;
            closure.emplace(key, dependency);
            return true;
        }

    } // namespace

    bool CompileShaderProgramForCook(
        const ShaderCookRequest& request,
        ShaderCookOutput& outOutput) noexcept {
        ShaderCookOutput output{};
        try {
            if (request.entries.empty() ||
                request.entries.size() > kMaximumShaderCookEntries ||
                !ValidateRequestIdentity(request, output.diagnostics)) {
                if (output.diagnostics.empty()) {
                    output.diagnostics =
                        "shader cook entry count is invalid";
                }
                outOutput = std::move(output);
                return false;
            }
            INTERNAL::ShaderCookRoot sourceRoot{};
            std::vector<INTERNAL::ShaderCookRoot> includeRoots{};
            if (!INTERNAL::BuildShaderCookRoots(
                    request,
                    includeRoots,
                    sourceRoot,
                    output.diagnostics)) {
                outOutput = std::move(output);
                return false;
            }
            INTERNAL::ShaderCookFile source{};
            if (!INTERNAL::ReadShaderCookFile(
                    sourceRoot,
                    request.sourcePath,
                    ".hlsl",
                    source,
                    output.diagnostics)) {
                outOutput = std::move(output);
                return false;
            }
            std::shared_ptr<const INTERNAL::ShaderIncludePlan> includePlan{};
            if (!INTERNAL::BuildShaderIncludePlan(
                    source,
                    sourceRoot,
                    includeRoots,
                    includePlan,
                    output.diagnostics)) {
                outOutput = std::move(output);
                return false;
            }
            INTERNAL::DxcRuntimeSnapshot runtime{};
            if (!INTERNAL::AcquireDxcRuntime(
                    INTERNAL::DxcRuntimePurpose::Cook,
                    runtime,
                    output.diagnostics)) {
                outOutput = std::move(output);
                return false;
            }
            output.toolchain = {
                kShaderCookCompilerContractVersion,
                "DXC",
                runtime.compilerVersion,
                runtime.compilerBinaryHash,
                runtime.validatorBinaryHash
            };
            output.engineInterface = request.engineInterface;
            output.rootLayout = request.rootLayout;
            output.requiredCaps = request.requiredCaps;
            std::unique_lock invocationLock(
                INTERNAL::GetDxcInvocationMutex());

            std::vector<ShaderCookEntryRequest> entries = request.entries;
            std::sort(
                entries.begin(),
                entries.end(),
                [](const ShaderCookEntryRequest& lhs,
                   const ShaderCookEntryRequest& rhs) {
                    return lhs.id < rhs.id;
                });
            std::map<std::string, ShaderCookDependency> closure{};
            uint64_t closureBytes = 0u;
            if (!MergeDependency(
                    source.dependency,
                    closure,
                    closureBytes,
                    output.diagnostics)) {
                outOutput = std::move(output);
                return false;
            }
            output.entries.reserve(entries.size());
            size_t totalDxilBytes = 0u;
            for (size_t index = 0u; index < entries.size(); ++index) {
                if (index > 0u && entries[index - 1u].id == entries[index].id) {
                    output.diagnostics = "shader entry id is duplicated";
                    outOutput = std::move(output);
                    return false;
                }
                ShaderCookEntryOutput entryOutput{};
                entryOutput.id = entries[index].id;
                if (!BuildCanonicalShaderCookArguments(
                        request,
                        entries[index],
                        entryOutput.canonicalArguments,
                        output.diagnostics)) {
                    outOutput = std::move(output);
                    return false;
                }
                std::vector<std::wstring> ownedArguments{};
                std::vector<LPCWSTR> dxcArguments{};
                if (!BuildActualDxcArguments(
                        source.dependency.logicalPath,
                        entryOutput.canonicalArguments,
                        includeRoots,
                        ownedArguments,
                        dxcArguments,
                        output.diagnostics)) {
                    outOutput = std::move(output);
                    return false;
                }
                ComPtr<INTERNAL::TrackingDxcIncludeHandler> includeHandler{};
                includeHandler.Attach(new (std::nothrow)
                    INTERNAL::TrackingDxcIncludeHandler(
                        runtime.utils.Get(),
                        includePlan));
                if (includeHandler == nullptr) {
                    output.diagnostics =
                        "could not allocate the shader include handler";
                    outOutput = std::move(output);
                    return false;
                }
                const DxcBuffer sourceBuffer{
                    source.bytes.data(),
                    source.bytes.size(),
                    DXC_CP_UTF8
                };
                ComPtr<IDxcResult> result{};
                const HRESULT compileCall = runtime.compiler->Compile(
                    &sourceBuffer,
                    dxcArguments.data(),
                    static_cast<UINT32>(dxcArguments.size()),
                    includeHandler.Get(),
                    IID_PPV_ARGS(result.GetAddressOf()));
                ComPtr<IDxcBlobUtf8> errors{};
                if (result != nullptr) {
                    result->GetOutput(
                        DXC_OUT_ERRORS,
                        IID_PPV_ARGS(errors.GetAddressOf()),
                        nullptr);
                }
                HRESULT compileStatus = E_FAIL;
                if (FAILED(compileCall) || result == nullptr ||
                    FAILED(result->GetStatus(&compileStatus)) ||
                    FAILED(compileStatus) ||
                    !includeHandler->LastError().empty()) {
                    output.diagnostics = "DXC compile failed for " +
                        entries[index].id + ": " +
                        INTERNAL::BlobToUtf8(runtime.utils.Get(), errors.Get());
                    if (!includeHandler->LastError().empty()) {
                        output.diagnostics += " include=" +
                            includeHandler->LastError();
                    }
                    outOutput = std::move(output);
                    return false;
                }
                ComPtr<IDxcBlob> shader{};
                if (FAILED(result->GetOutput(
                        DXC_OUT_OBJECT,
                        IID_PPV_ARGS(shader.GetAddressOf()),
                        nullptr)) ||
                    shader == nullptr || shader->GetBufferSize() < 4u ||
                    shader->GetBufferSize() > kMaximumShaderCookDxilBytes ||
                    !INTERNAL::ValidateShaderCookDxil(
                        runtime,
                        shader.Get(),
                        output.diagnostics) ||
                    !INTERNAL::ExtractShaderCookBindings(
                        runtime,
                        result.Get(),
                        entries[index].stage,
                        entryOutput.bindings,
                        output.diagnostics)) {
                    outOutput = std::move(output);
                    return false;
                }
                if (shader->GetBufferSize() >
                    kMaximumShaderCookArtifactBytes - totalDxilBytes) {
                    output.diagnostics =
                        "shader program DXIL exceeds its total budget";
                    outOutput = std::move(output);
                    return false;
                }
                totalDxilBytes += shader->GetBufferSize();
                const auto* begin = static_cast<const uint8_t*>(
                    shader->GetBufferPointer());
                entryOutput.dxil.assign(
                    begin,
                    begin + shader->GetBufferSize());
                for (const ShaderCookDependency& dependency :
                     includeHandler->Dependencies()) {
                    if (!MergeDependency(
                            dependency,
                            closure,
                            closureBytes,
                            output.diagnostics)) {
                        outOutput = std::move(output);
                        return false;
                    }
                }
                output.entries.push_back(std::move(entryOutput));
            }

            output.dependencies.reserve(closure.size());
            for (const auto& item : closure) {
                output.dependencies.push_back(item.second);
            }
            std::sort(
                output.dependencies.begin(),
                output.dependencies.end(),
                [](const ShaderCookDependency& lhs,
                   const ShaderCookDependency& rhs) {
                    return lhs.logicalPath < rhs.logicalPath;
                });
            if (!VerifyShaderCookDependencies(
                    request,
                    output.dependencies,
                    output.diagnostics)) {
                outOutput = std::move(output);
                return false;
            }
            output.success = true;
            output.diagnostics.clear();
            outOutput = std::move(output);
            return true;
        }
        catch (const std::exception& error) {
            output.diagnostics = std::string("shader cook failed: ") +
                error.what();
            outOutput = std::move(output);
            return false;
        }
    }

} // namespace HIKARI::GFX
