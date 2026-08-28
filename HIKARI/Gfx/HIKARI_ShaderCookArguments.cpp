#include "Gfx/HIKARI_ShaderCookCompiler.h"

#include <algorithm>
#include <exception>
#include <string_view>

namespace HIKARI::GFX {
    namespace {

        std::string_view ExpectedProfile(ShaderStage stage) noexcept {
            switch (stage) {
            case ShaderStage::Vertex: return "vs_6_0";
            case ShaderStage::Pixel: return "ps_6_0";
            case ShaderStage::Compute: return "cs_6_0";
            case ShaderStage::Amplification: return "as_6_5";
            case ShaderStage::Mesh: return "ms_6_5";
            case ShaderStage::Geometry: return "gs_6_0";
            }
            return {};
        }

        bool IsIdentifier(
            std::string_view value,
            bool stablePunctuation) noexcept {
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
                const bool punctuation = stablePunctuation &&
                    (character == '.' || character == '-');
                if ((!alpha && !digit && character != '_' &&
                     !punctuation) ||
                    (index == 0u && digit)) {
                    return false;
                }
            }
            return true;
        }

    } // namespace

    const char* ToString(ShaderCookIncludeRoot root) noexcept {
        switch (root) {
        case ShaderCookIncludeRoot::Project: return "Project";
        case ShaderCookIncludeRoot::EngineSdk: return "EngineSdk";
        case ShaderCookIncludeRoot::Count: break;
        }
        return "Unknown";
    }

    bool BuildCanonicalShaderCookArguments(
        const ShaderCookRequest& request,
        const ShaderCookEntryRequest& entry,
        std::vector<std::string>& outArguments,
        std::string& outError) noexcept {
        try {
            const std::string_view expectedProfile =
                ExpectedProfile(entry.stage);
            if (expectedProfile.empty() ||
                !IsIdentifier(entry.id, true) ||
                !IsIdentifier(entry.entryPoint, false) ||
                entry.profile != expectedProfile ||
                request.defines.size() > kMaximumShaderCookDefines ||
                request.includeRoots.size() >=
                    static_cast<size_t>(ShaderCookIncludeRoot::Count) + 1u) {
                outError = "shader entry or compile-table shape is invalid";
                return false;
            }
            std::vector<ShaderCookIncludeRoot> roots = request.includeRoots;
            std::sort(roots.begin(), roots.end());
            if (std::adjacent_find(roots.begin(), roots.end()) != roots.end() ||
                (!roots.empty() &&
                 roots.back() >= ShaderCookIncludeRoot::Count)) {
                outError = "shader include root is invalid or duplicated";
                return false;
            }
            std::vector<ShaderCookDefine> defines = request.defines;
            std::sort(
                defines.begin(),
                defines.end(),
                [](const ShaderCookDefine& lhs,
                   const ShaderCookDefine& rhs) {
                    return lhs.name < rhs.name;
                });
            for (size_t index = 0u; index < defines.size(); ++index) {
                if (!IsIdentifier(defines[index].name, false) ||
                    defines[index].value.size() > 256u ||
                    defines[index].value.find('\0') != std::string::npos ||
                    (index > 0u &&
                     defines[index - 1u].name == defines[index].name)) {
                    outError = "shader define is invalid or duplicated";
                    return false;
                }
            }

            std::vector<std::string> arguments{
                "-E", entry.entryPoint,
                "-T", entry.profile,
                "-HV", "2021", "-O3", "-WX"
            };
            for (ShaderCookIncludeRoot root : roots) {
                arguments.push_back("-I");
                arguments.push_back(ToString(root));
            }
            for (const ShaderCookDefine& define : defines) {
                arguments.push_back(
                    "-D" + define.name + "=" + define.value);
            }
            if (arguments.size() > kMaximumShaderCookArguments) {
                outError = "canonical shader argument count exceeds its limit";
                return false;
            }
            outArguments = std::move(arguments);
            outError.clear();
            return true;
        }
        catch (const std::exception& error) {
            outError = std::string(
                "canonical shader arguments failed: ") + error.what();
            return false;
        }
    }

} // namespace HIKARI::GFX
