#pragma once

#include <cstdint>
#include <filesystem>

namespace HIKARI {
class AssetRegistry;
struct AssetRecord;
} // namespace HIKARI

namespace HIKARI::ASSETS::SHADER::INTERNAL {

enum class ShaderAssetRegistryAdmissionMode : uint8_t {
    Authoring = 0u,
    Cooked,
};

struct ShaderAssetRegistryAdmissionContext {
    std::filesystem::path projectRoot{};
    std::filesystem::path engineShaderRoot{};
    ShaderAssetRegistryAdmissionMode mode = ShaderAssetRegistryAdmissionMode::Authoring;
};

bool AppendProjectShaderAssetDescriptor(const ShaderAssetRegistryAdmissionContext& context, const AssetRecord& record,
                                        AssetRegistry& registry);

} // namespace HIKARI::ASSETS::SHADER::INTERNAL
