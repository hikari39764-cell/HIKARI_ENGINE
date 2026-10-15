#include "Core/Content/HIKARI_EngineContentContext.h"
#include "Core/IO/HIKARI_PathNormalization.h"

#include <system_error>
#include <utility>

#include "Platform/Paths/HIKARI_ExecutablePath.h"

namespace HIKARI::CONTENT {
namespace {

void SetMessage(std::string* outMessage, std::string message) {
    if (outMessage != nullptr) {
        *outMessage = std::move(message);
    }
}

} // namespace

bool EngineContentContext::Open(const std::filesystem::path& root, std::string* outMessage) {
    Close();
    if (root.empty()) {
        SetMessage(outMessage, "engine content root is empty");
        return false;
    }

    std::error_code ec{};
    std::filesystem::path normalized = std::filesystem::absolute(root, ec).lexically_normal();
    if (ec) {
        normalized = root.lexically_normal();
        ec.clear();
    }
    const std::filesystem::path shaderRoot = normalized / "Shaders";
    if (!std::filesystem::is_directory(shaderRoot, ec) || ec) {
        SetMessage(outMessage, "engine shader directory is missing: " + shaderRoot.generic_string());
        return false;
    }

    root_ = std::move(normalized);
    open_ = true;
    SetMessage(outMessage, {});
    return true;
}

void EngineContentContext::Close() noexcept {
    root_.clear();
    open_ = false;
}

bool EngineContentContext::IsOpen() const noexcept { return open_; }

const std::filesystem::path& EngineContentContext::GetRoot() const noexcept { return root_; }

std::filesystem::path EngineContentContext::Resolve(const std::filesystem::path& relativePath) const {
    if (!open_ || relativePath.is_absolute() || PATHS::HasParentTraversal(relativePath)) {
        return {};
    }
    return (root_ / relativePath).lexically_normal();
}

EngineContentContext& GetEngineContentContext() {
    static EngineContentContext context;
    return context;
}

bool OpenEngineContentForLaunch(std::string* outMessage) {
    const std::filesystem::path executableDirectory = PLATFORM::GetExecutableDirectory();
    EngineContentContext& context = GetEngineContentContext();
    if (executableDirectory.empty()) {
        context.Close();
        SetMessage(outMessage, "could not resolve the executable directory");
        return false;
    }

    return context.Open(executableDirectory / "EngineContent", outMessage);
}

} // namespace HIKARI::CONTENT
