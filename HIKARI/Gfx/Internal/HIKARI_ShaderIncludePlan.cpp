#include "Gfx/Internal/HIKARI_ShaderIncludePlan.h"
#include <algorithm>
#include <cctype>
#include <climits>
#include <exception>
#include <unordered_set>
#include "Core/Text/HIKARI_AsciiCase.h"
namespace HIKARI::GFX::INTERNAL {
    namespace {
        using Microsoft::WRL::ComPtr;
        struct IncludeDirective {
            std::filesystem::path path{}; bool quoted = true;
        };
        std::string MakeAliasKey(
            const std::filesystem::path& path) {
            std::error_code ec{};
            const std::filesystem::path normalized = path.is_absolute()
                ? std::filesystem::weakly_canonical(path, ec)
                : path.lexically_normal();
            return TEXT::ToLowerAsciiCopy(
                (ec ? path.lexically_normal() : normalized)
                    .generic_string());
        }
        std::string_view Trim(std::string_view text) noexcept {
            while (!text.empty() && std::isspace(
                static_cast<unsigned char>(text.front()))) {
                text.remove_prefix(1u);
            }
            while (!text.empty() && std::isspace(
                static_cast<unsigned char>(text.back()))) {
                text.remove_suffix(1u);
            }
            return text;
        }
        bool MakeUtf8Path(
            std::string_view text,
            std::filesystem::path& outPath) {
            if (text.empty() || text.size() > INT_MAX) {
                return false;
            }
            const int count = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                text.data(),
                static_cast<int>(text.size()),
                nullptr,
                0);
            if (count <= 0) {
                return false;
            }
            std::wstring wide(static_cast<size_t>(count), L'\0');
            if (MultiByteToWideChar(
                    CP_UTF8,
                    MB_ERR_INVALID_CHARS,
                    text.data(),
                    static_cast<int>(text.size()),
                    wide.data(),
                    count) != count) {
                return false;
            }
            outPath = std::filesystem::path(wide);
            return true;
        }
        bool ExtractIncludeDirectives(
            const std::vector<uint8_t>& bytes,
            std::vector<IncludeDirective>& outDirectives,
            std::string& outError) {
            const std::string_view source(
                reinterpret_cast<const char*>(bytes.data()),
                bytes.size());
            std::vector<IncludeDirective> directives{};
            bool blockComment = false;
            size_t lineBegin = 0u;
            while (lineBegin <= source.size()) {
                const size_t lineEnd = source.find('\n', lineBegin);
                const std::string_view line = source.substr(
                    lineBegin,
                    lineEnd == std::string_view::npos
                        ? source.size() - lineBegin
                        : lineEnd - lineBegin);
                std::string clean{};
                clean.reserve(line.size());
                for (size_t index = 0u; index < line.size(); ++index) {
                    if (blockComment) {
                        if (index + 1u < line.size() &&
                            line[index] == '*' && line[index + 1u] == '/') {
                            blockComment = false;
                            ++index;
                        }
                        continue;
                    }
                    if (index + 1u < line.size() &&
                        line[index] == '/' && line[index + 1u] == '*') {
                        blockComment = true;
                        ++index;
                        continue;
                    }
                    if (index + 1u < line.size() &&
                        line[index] == '/' && line[index + 1u] == '/') {
                        break;
                    }
                    clean.push_back(line[index]);
                }
                if (lineBegin == 0u && clean.size() >= 3u &&
                    static_cast<unsigned char>(clean[0]) == 0xefu &&
                    static_cast<unsigned char>(clean[1]) == 0xbbu &&
                    static_cast<unsigned char>(clean[2]) == 0xbfu) {
                    clean.erase(0u, 3u);
                }
                std::string_view directive = Trim(clean);
                if (!directive.empty() && directive.front() == '#') {
                    directive.remove_prefix(1u);
                    directive = Trim(directive);
                    constexpr std::string_view includeWord = "include";
                    if (directive.rfind(includeWord, 0u) == 0u &&
                        (directive.size() == includeWord.size() ||
                         std::isspace(static_cast<unsigned char>(
                            directive[includeWord.size()])) ||
                         directive[includeWord.size()] == '"' ||
                         directive[includeWord.size()] == '<')) {
                        directive.remove_prefix(includeWord.size());
                        directive = Trim(directive);
                        if (directive.empty() ||
                            (directive.front() != '"' &&
                             directive.front() != '<')) {
                            outError =
                                "macro shader includes are not supported";
                            return false;
                        }
                        const bool quoted = directive.front() == '"';
                        const char closing = quoted ? '"' : '>';
                        const size_t close = directive.find(closing, 1u);
                        if (close == std::string_view::npos ||
                            !Trim(directive.substr(close + 1u)).empty()) {
                            outError = "shader include directive is malformed";
                            return false;
                        }
                        const std::string_view pathText =
                            directive.substr(1u, close - 1u);
                        std::filesystem::path path{};
                        if (pathText.empty() ||
                            pathText.size() > kMaximumShaderCookPathBytes ||
                            !MakeUtf8Path(pathText, path) ||
                            path.is_absolute() || path.has_root_name() ||
                            path.has_root_directory()) {
                            outError =
                                "shader include directive path is invalid";
                            return false;
                        }
                        directives.push_back({ path, quoted });
                    }
                }
                if (lineEnd == std::string_view::npos) {
                    break;
                }
                lineBegin = lineEnd + 1u;
            }
            if (blockComment) {
                outError = "shader source has an unterminated block comment";
                return false;
            }
            outDirectives = std::move(directives);
            return true;
        }
        class IncludePlanner {
        public:
            IncludePlanner(
                const ShaderCookRoot& sourceRoot,
                const std::vector<ShaderCookRoot>& includeRoots,
                std::shared_ptr<ShaderIncludePlan> plan)
                : sourceRoot_(sourceRoot),
                  includeRoots_(includeRoots),
                  plan_(std::move(plan)) {}
            bool Build(
                const ShaderCookFile& source,
                std::string& outError) {
                totalBytes_ = source.dependency.byteSize;
                return Walk(sourceRoot_, source, outError);
            }
        private:
            bool IsRootEnabled(ShaderCookIncludeRoot kind) const noexcept {
                return std::any_of(
                    includeRoots_.begin(),
                    includeRoots_.end(),
                    [kind](const ShaderCookRoot& root) {
                        return root.kind == kind;
                    });
            }
            bool AddAlias(
                const std::filesystem::path& alias,
                const std::shared_ptr<const ShaderCookFile>& file,
                std::string& outError) {
                const std::string key = MakeAliasKey(alias);
                if (key.empty()) {
                    return true;
                }
                const auto found = plan_->aliases.find(key);
                if (found != plan_->aliases.end()) {
                    if (found->second->dependency.logicalPath !=
                        file->dependency.logicalPath) {
                        outError =
                            "shader include alias resolves ambiguously";
                        return false;
                    }
                    return true;
                }
                plan_->aliases.emplace(key, file);
                return true;
            }
            bool AddFileAliases(
                const ShaderCookFile& parent,
                const IncludeDirective& directive,
                const ShaderCookRoot& childRoot,
                const std::shared_ptr<const ShaderCookFile>& file,
                std::string& outError) {
                const std::string& logical = file->dependency.logicalPath;
                const size_t slash = logical.find('/');
                const std::filesystem::path ownerRelative =
                    slash == std::string::npos
                        ? std::filesystem::path(logical)
                        : std::filesystem::path(logical.substr(slash + 1u));
                const std::string& parentLogical =
                    parent.dependency.logicalPath;
                const size_t parentSlash = parentLogical.find('/');
                const std::filesystem::path parentRelative =
                    parentSlash == std::string::npos
                        ? std::filesystem::path(parentLogical)
                        : std::filesystem::path(
                            parentLogical.substr(parentSlash + 1u));
                // DXC's virtual filesystem calls LoadSource with the
                // normalized Clang candidate. A basename-only alias would
                // collapse distinct source-relative includes.
                return AddAlias(file->physicalPath, file, outError) &&
                    AddAlias(
                        parent.physicalPath.parent_path() / directive.path,
                        file,
                        outError) &&
                    AddAlias(
                        childRoot.physicalRoot / directive.path,
                        file,
                        outError) &&
                    AddAlias(logical, file, outError) &&
                    AddAlias(ownerRelative, file, outError) &&
                    AddAlias(
                        parentRelative.parent_path() / directive.path,
                        file,
                        outError);
            }
            bool Resolve(
                const ShaderCookRoot& parentRoot,
                const ShaderCookFile& parent,
                const IncludeDirective& directive,
                ShaderCookRoot& outRoot,
                ShaderCookFile& outFile,
                std::string& outError) {
                const std::string extension = TEXT::ToLowerAsciiCopy(
                    directive.path.extension().string());
                if (extension != ".hlsl" && extension != ".hlsli" &&
                    extension != ".h") {
                    outError =
                        "shader include must use .hlsl, .hlsli, or .h";
                    return false;
                }
                if (directive.quoted &&
                    (parentRoot.kind == sourceRoot_.kind ||
                     IsRootEnabled(parentRoot.kind))) {
                    ShaderCookFile local{};
                    std::string ignored{};
                    if (ReadShaderCookPhysicalFile(
                            parentRoot,
                            parent.physicalPath.parent_path() /
                                directive.path,
                            extension,
                            local,
                            ignored)) {
                        outRoot = parentRoot;
                        outFile = std::move(local);
                        return true;
                    }
                }
                std::vector<std::pair<ShaderCookRoot, ShaderCookFile>>
                    matches{};
                std::unordered_set<std::string> physicalKeys{};
                for (const ShaderCookRoot& root : includeRoots_) {
                    ShaderCookFile file{};
                    std::string ignored{};
                    if (!ReadShaderCookPhysicalFile(
                            root,
                            root.physicalRoot / directive.path,
                            extension,
                            file,
                            ignored)) {
                        continue;
                    }
                    if (physicalKeys.insert(
                            MakeAliasKey(file.physicalPath)).second) {
                        matches.emplace_back(root, std::move(file));
                    }
                }
                if (matches.size() != 1u) {
                    outError = matches.empty()
                        ? "shader include was not found in an allowed root"
                        : "shader include is ambiguous across allowed roots";
                    return false;
                }
                outRoot = std::move(matches.front().first);
                outFile = std::move(matches.front().second);
                return true;
            }
            bool Walk(
                const ShaderCookRoot& owner,
                const ShaderCookFile& file,
                std::string& outError) {
                const std::string key = MakeAliasKey(file.physicalPath);
                if (visited_.contains(key) || visiting_.contains(key)) {
                    return true;
                }
                if (visited_.size() >= kMaximumShaderCookDependencies) {
                    outError = "shader include plan exceeds its file limit";
                    return false;
                }
                visiting_.insert(key);
                std::vector<IncludeDirective> directives{};
                if (!ExtractIncludeDirectives(
                        file.bytes,
                        directives,
                        outError)) {
                    return false;
                }
                for (const IncludeDirective& directive : directives) {
                    ShaderCookRoot childRoot{};
                    ShaderCookFile child{};
                    if (!Resolve(
                            owner,
                            file,
                            directive,
                            childRoot,
                            child,
                            outError)) {
                        return false;
                    }
                    const auto shared =
                        std::make_shared<ShaderCookFile>(std::move(child));
                    if (!AddFileAliases(
                            file,
                            directive,
                            childRoot,
                            shared,
                            outError)) {
                        return false;
                    }
                    if (!ownedFiles_.contains(
                            MakeAliasKey(shared->physicalPath))) {
                        if (shared->dependency.byteSize >
                            kMaximumShaderCookClosureBytes - totalBytes_) {
                            outError =
                                "shader include plan exceeds its byte limit";
                            return false;
                        }
                        totalBytes_ += shared->dependency.byteSize;
                        ownedFiles_.insert(
                            MakeAliasKey(shared->physicalPath));
                    }
                    if (!Walk(childRoot, *shared, outError)) {
                        return false;
                    }
                }
                visiting_.erase(key);
                visited_.insert(key);
                return true;
            }
            ShaderCookRoot sourceRoot_{};
            std::vector<ShaderCookRoot> includeRoots_{};
            std::shared_ptr<ShaderIncludePlan> plan_{};
            std::unordered_set<std::string> visiting_{};
            std::unordered_set<std::string> visited_{};
            std::unordered_set<std::string> ownedFiles_{};
            uint64_t totalBytes_ = 0u;
        };
        bool SameDependency(
            const ShaderCookDependency& lhs,
            const ShaderCookDependency& rhs) noexcept {
            return lhs.logicalPath == rhs.logicalPath && lhs.byteSize == rhs.byteSize &&
                lhs.contentHash == rhs.contentHash;
        }
    } // namespace
    bool BuildShaderIncludePlan(
        const ShaderCookFile& source,
        const ShaderCookRoot& sourceRoot,
        const std::vector<ShaderCookRoot>& includeRoots,
        std::shared_ptr<const ShaderIncludePlan>& outPlan,
        std::string& outError) {
        try {
            auto plan = std::make_shared<ShaderIncludePlan>();
            IncludePlanner planner(sourceRoot, includeRoots, plan);
            if (!planner.Build(source, outError)) {
                return false;
            }
            outPlan = std::move(plan);
            outError.clear();
            return true;
        }
        catch (const std::exception& error) {
            outError = std::string("shader include plan failed: ") +
                error.what();
            return false;
        }
    }
    TrackingDxcIncludeHandler::TrackingDxcIncludeHandler(
        IDxcUtils* utils,
        std::shared_ptr<const ShaderIncludePlan> plan)
        : utils_(utils), plan_(std::move(plan)) {}
    HRESULT STDMETHODCALLTYPE TrackingDxcIncludeHandler::QueryInterface(
        REFIID interfaceId,
        void** outObject) {
        if (outObject == nullptr) {
            return E_POINTER;
        }
        *outObject = nullptr;
        if (interfaceId == __uuidof(IUnknown) ||
            interfaceId == __uuidof(IDxcIncludeHandler)) {
            *outObject = static_cast<IDxcIncludeHandler*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE TrackingDxcIncludeHandler::AddRef() {
        return references_.fetch_add(1u, std::memory_order_relaxed) + 1u;
    }
    ULONG STDMETHODCALLTYPE TrackingDxcIncludeHandler::Release() {
        const ULONG remaining =
            references_.fetch_sub(1u, std::memory_order_acq_rel) - 1u;
        if (remaining == 0u) {
            delete this;
        }
        return remaining;
    }
    HRESULT STDMETHODCALLTYPE TrackingDxcIncludeHandler::LoadSource(
        LPCWSTR filename,
        IDxcBlob** outSource) {
        if (outSource == nullptr) {
            return E_POINTER;
        }
        *outSource = nullptr;
        try {
            if (filename == nullptr || *filename == L'\0' || !plan_) {
                lastError_ = "DXC requested an invalid include path";
                return E_INVALIDARG;
            }
            const std::string requested =
                MakeAliasKey(std::filesystem::path(filename));
            const auto found = plan_->aliases.find(requested);
            std::shared_ptr<const ShaderCookFile> planned{};
            if (found != plan_->aliases.end()) {
                planned = found->second;
            }
            if (!planned) {
                std::string suffix = requested;
                while (suffix.find('/') != std::string::npos) {
                    suffix.erase(0u, suffix.find('/') + 1u);
                    if (suffix.find('/') == std::string::npos) {
                        break;
                    }
                    const auto suffixFound = plan_->aliases.find(suffix);
                    if (suffixFound == plan_->aliases.end()) {
                        continue;
                    }
                    if (planned &&
                        planned->dependency.logicalPath !=
                            suffixFound->second->dependency.logicalPath) {
                        lastError_ =
                            "DXC include suffix resolves ambiguously: " +
                            requested;
                        return HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
                    }
                    planned = suffixFound->second;
                }
            }
            if (!planned ||
                !RecordDependency(planned->dependency, lastError_)) {
                if (lastError_.empty()) {
                    lastError_ =
                        "DXC requested an include outside the approved plan: " +
                        requested;
                }
                return HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
            }
            ComPtr<IDxcBlobEncoding> blob{};
            const HRESULT result = utils_->CreateBlob(
                planned->bytes.data(),
                static_cast<UINT32>(planned->bytes.size()),
                DXC_CP_UTF8,
                blob.GetAddressOf());
            if (FAILED(result)) {
                lastError_ = "DXC could not create an include source blob";
                return result;
            }
            *outSource = blob.Detach();
            return S_OK;
        }
        catch (const std::exception& error) {
            lastError_ = std::string("include load failed: ") + error.what();
            return E_FAIL;
        }
    }
    const std::vector<ShaderCookDependency>&
        TrackingDxcIncludeHandler::Dependencies() const noexcept {
        return dependencies_;
    }
    const std::string& TrackingDxcIncludeHandler::LastError() const noexcept {
        return lastError_;
    }
    bool TrackingDxcIncludeHandler::RecordDependency(
        const ShaderCookDependency& dependency,
        std::string& outError) {
        for (const ShaderCookDependency& existing : dependencies_) {
            if (TEXT::ToLowerAsciiCopy(existing.logicalPath) ==
                    TEXT::ToLowerAsciiCopy(dependency.logicalPath)) {
                if (!SameDependency(existing, dependency)) {
                    outError =
                        "shader include changed or used ambiguous casing";
                    return false;
                }
                return true;
            }
        }
        if (dependencies_.size() >= kMaximumShaderCookDependencies) {
            outError = "shader include count exceeds its limit";
            return false;
        }
        dependencies_.push_back(dependency);
        return true;
    }
} // namespace HIKARI::GFX::INTERNAL
