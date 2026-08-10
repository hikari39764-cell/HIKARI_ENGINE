#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace HIKARI::EDITOR::INTERNAL {

    struct VfxAssetAuthoringRoots {
        std::filesystem::path projectRoot{};
        std::filesystem::path assetsRoot{};
        std::filesystem::path sourceMetaRoot{};
    };

    struct VfxAssetCreationFiles {
        VfxAssetAuthoringRoots roots{};
        std::filesystem::path sourcePath{};
        std::filesystem::path metaPath{};
        bool sourceExistedBefore = false;
        bool metaExistedBefore = false;
    };

    bool ResolveVfxAssetAuthoringRoots(
        const std::filesystem::path& projectRoot,
        const std::filesystem::path& assetsRoot,
        const std::filesystem::path& sourceMetaRoot,
        VfxAssetAuthoringRoots& outRoots,
        std::string& outMessage);

    bool ResolveVfxSourceWriteTarget(
        const VfxAssetAuthoringRoots& roots,
        const std::filesystem::path& requestedPath,
        std::filesystem::path& outPath,
        std::string& outMessage);

    bool ResolveVfxAssetCreationFiles(
        const VfxAssetAuthoringRoots& roots,
        const std::filesystem::path& requestedSourcePath,
        const std::filesystem::path& requestedMetaPath,
        VfxAssetCreationFiles& outFiles,
        std::string& outMessage);

    // Removes only regular files that did not exist when creation began.
    // Every deletion is revalidated against its resolved owning root.
    bool RollBackCreatedVfxAsset(
        const VfxAssetCreationFiles& files,
        std::string& outMessage);

    // Removes VFX-private material sources and metadata that were created by
    // the current preset save attempt. Callers must pass only new targets
    // returned by MaterialAssetAuthoringService::Create.
    bool RollBackCreatedVfxPrivateMaterials(
        const VfxAssetAuthoringRoots& roots,
        const std::vector<std::filesystem::path>& sourcePaths,
        const std::vector<std::filesystem::path>& metaPaths,
        std::string& outMessage);

} // namespace HIKARI::EDITOR::INTERNAL
