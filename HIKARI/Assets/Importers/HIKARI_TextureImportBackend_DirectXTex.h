#pragma once

#include "HIKARI_TextureImportBackend.h"

namespace HIKARI {

    struct MetallicRoughnessPackRequest {
        std::filesystem::path metalnessSource{};
        std::filesystem::path roughnessSource{};
        std::filesystem::path glossinessSource{};
    };

    bool InspectTextureAlphaWithDirectXTex(
        const std::filesystem::path& sourcePath,
        TextureImportSettings& inOutSettings,
        std::string& outMessage);

    // Converts separate linear scalar maps into the engine's canonical
    // glTF-style material texture: G=roughness, B=metallic.
    bool WritePackedMetallicRoughnessDdsWithDirectXTex(
        const MetallicRoughnessPackRequest& request,
        const std::filesystem::path& outputPath,
        std::string& outMessage);

    class DirectXTexTextureImportBackend final : public ITextureImportBackend {
    public:
        bool IsAvailable() const override;

        bool Inspect(
            const std::filesystem::path& sourcePath,
            TextureImportSettings& inOutSettings,
            std::string& outMessage) override;

        bool ConvertToDds(
            const std::filesystem::path& sourcePath,
            const std::filesystem::path& outputPath,
            const TextureImportSettings& settings,
            std::string& outMessage,
            AssetTaskContext* task = nullptr) override;

        bool ConvertToHtexAndDds(
            const std::filesystem::path& sourcePath,
            const std::filesystem::path& htexOutputPath,
            const std::filesystem::path& debugDdsOutputPath,
            const TextureImportSettings& settings,
            std::string& outMessage,
            AssetTaskContext* task = nullptr) override;
    };

} // namespace HIKARI
