#include "Editor/Authoring/Internal/HIKARI_VfxAssetAuthoringTransaction.h"

#include <algorithm>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

#include "Core/IO/HIKARI_FileReplacementTransaction.h"

namespace HIKARI::EDITOR::INTERNAL {
    namespace {

        bool IsMissingPathStatus(
            const std::filesystem::file_status& status,
            const std::error_code& error) noexcept {

            if (status.type() ==
                    std::filesystem::file_type::not_found ||
                error == std::errc::no_such_file_or_directory ||
                error.default_error_condition() ==
                    std::errc::no_such_file_or_directory) {
                return true;
            }
#if defined(_WIN32)
            // std::filesystem may preserve native ERROR_FILE_NOT_FOUND or
            // ERROR_PATH_NOT_FOUND instead of mapping it to generic errc.
            return error.value() == 2 || error.value() == 3;
#else
            return false;
#endif
        }

        bool ResolveAbsolutePath(
            const std::filesystem::path& path,
            std::filesystem::path& outPath,
            std::string& outMessage) {

            if (path.empty()) {
                outMessage = "VFX asset path is empty";
                return false;
            }
            std::error_code error{};
            const std::filesystem::path absolute = path.is_absolute()
                ? path
                : std::filesystem::absolute(path, error);
            if (error) {
                outMessage = "Failed to resolve VFX asset path: " +
                    error.message();
                return false;
            }
            outPath = std::filesystem::weakly_canonical(absolute, error);
            if (!error) {
                return true;
            }

            std::error_code statusError{};
            const std::filesystem::file_status status =
                std::filesystem::symlink_status(absolute, statusError);
            if (!IsMissingPathStatus(status, statusError)) {
                outMessage = "Failed to canonicalize VFX asset path: " +
                    error.message();
                return false;
            }
            error.clear();
            const std::filesystem::path resolvedParent =
                std::filesystem::weakly_canonical(
                    absolute.parent_path(),
                    error);
            if (error || resolvedParent.empty()) {
                outMessage = "Failed to canonicalize VFX asset parent: " +
                    error.message();
                return false;
            }
            outPath = (resolvedParent / absolute.filename())
                .lexically_normal();
            return true;
        }

        bool IsStrictlyUnderRoot(
            const std::filesystem::path& root,
            const std::filesystem::path& path) {

            const std::filesystem::path relative =
                path.lexically_relative(root);
            if (relative.empty() || relative.is_absolute() ||
                relative == ".") {
                return false;
            }
            return std::none_of(
                relative.begin(),
                relative.end(),
                [](const std::filesystem::path& component) {
                    return component == "..";
                });
        }

        bool InspectCreationFile(
            const std::filesystem::path& path,
            bool& outExisted,
            std::string& outMessage) {

            std::error_code error{};
            const std::filesystem::file_status status =
                std::filesystem::symlink_status(path, error);
            if (IsMissingPathStatus(status, error)) {
                outExisted = false;
                return true;
            }
            if (error) {
                outMessage = "Failed to inspect VFX creation target: " +
                    error.message();
                return false;
            }
            outExisted = std::filesystem::exists(status);
            if (outExisted &&
                status.type() != std::filesystem::file_type::regular) {
                outMessage =
                    "VFX creation target is not a regular file: " +
                    path.generic_string();
                return false;
            }
            return true;
        }

        bool AppendCreatedFileRollback(
            const std::filesystem::path& root,
            const std::filesystem::path& path,
            bool existedBefore,
            std::vector<IO::FileReplacementOperation>& outOperations,
            std::string& outMessage) {

            if (existedBefore) {
                return true;
            }
            std::error_code error{};
            const std::filesystem::file_status status =
                std::filesystem::symlink_status(path, error);
            if (IsMissingPathStatus(status, error)) {
                return true;
            }
            if (error) {
                outMessage = "Failed to inspect VFX rollback target: " +
                    error.message();
                return false;
            }
            if (!std::filesystem::exists(status)) {
                return true;
            }
            if (status.type() != std::filesystem::file_type::regular) {
                outMessage =
                    "Refused to roll back a non-regular VFX file: " +
                    path.generic_string();
                return false;
            }
            std::filesystem::path resolvedPath{};
            if (!ResolveAbsolutePath(path, resolvedPath, outMessage) ||
                !IsStrictlyUnderRoot(root, resolvedPath)) {
                outMessage =
                    "Refused to roll back a VFX file outside its root";
                return false;
            }
            outOperations.push_back({
                .finalPath = path,
                .deleteFinal = true,
            });
            return true;
        }

    } // namespace

    bool ResolveVfxAssetAuthoringRoots(
        const std::filesystem::path& projectRoot,
        const std::filesystem::path& assetsRoot,
        const std::filesystem::path& sourceMetaRoot,
        VfxAssetAuthoringRoots& outRoots,
        std::string& outMessage) {

        VfxAssetAuthoringRoots roots{};
        if (!ResolveAbsolutePath(
                projectRoot,
                roots.projectRoot,
                outMessage) ||
            !ResolveAbsolutePath(
                assetsRoot,
                roots.assetsRoot,
                outMessage) ||
            !ResolveAbsolutePath(
                sourceMetaRoot,
                roots.sourceMetaRoot,
                outMessage)) {
            return false;
        }
        if (!IsStrictlyUnderRoot(roots.projectRoot, roots.assetsRoot)) {
            outMessage = "Configured Assets root is outside the project";
            return false;
        }
        if (!IsStrictlyUnderRoot(
                roots.projectRoot,
                roots.sourceMetaRoot)) {
            outMessage =
                "Configured asset metadata root is outside the project";
            return false;
        }
        outRoots = std::move(roots);
        return true;
    }

    bool ResolveVfxSourceWriteTarget(
        const VfxAssetAuthoringRoots& roots,
        const std::filesystem::path& requestedPath,
        std::filesystem::path& outPath,
        std::string& outMessage) {

        if (!ResolveAbsolutePath(
                requestedPath,
                outPath,
                outMessage)) {
            return false;
        }
        if (!IsStrictlyUnderRoot(roots.assetsRoot, outPath) ||
            outPath.extension() != ".hvfx") {
            outMessage =
                "VFX source must be a .hvfx file under project Assets";
            return false;
        }
        return true;
    }

    bool ResolveVfxAssetCreationFiles(
        const VfxAssetAuthoringRoots& roots,
        const std::filesystem::path& requestedSourcePath,
        const std::filesystem::path& requestedMetaPath,
        VfxAssetCreationFiles& outFiles,
        std::string& outMessage) {

        VfxAssetCreationFiles files{};
        files.roots = roots;
        if (!ResolveAbsolutePath(
                requestedSourcePath,
                files.sourcePath,
                outMessage)) {
            return false;
        }
        if (!IsStrictlyUnderRoot(
                files.roots.assetsRoot,
                files.sourcePath) ||
            files.sourcePath.extension() != ".hvfx") {
            outMessage =
                "VFX source must be a .hvfx file under project Assets";
            return false;
        }
        if (!ResolveAbsolutePath(
                requestedMetaPath,
                files.metaPath,
                outMessage) ||
            !IsStrictlyUnderRoot(
                files.roots.sourceMetaRoot,
                files.metaPath)) {
            outMessage =
                "VFX metadata target is outside the project metadata root";
            return false;
        }
        if (!InspectCreationFile(
                files.sourcePath,
                files.sourceExistedBefore,
                outMessage) ||
            !InspectCreationFile(
                files.metaPath,
                files.metaExistedBefore,
                outMessage)) {
            return false;
        }
        outFiles = std::move(files);
        return true;
    }

    bool RollBackCreatedVfxAsset(
        const VfxAssetCreationFiles& files,
        std::string& outMessage) {

        if (!IsStrictlyUnderRoot(
                files.roots.projectRoot,
                files.roots.assetsRoot) ||
            !IsStrictlyUnderRoot(
                files.roots.projectRoot,
                files.roots.sourceMetaRoot)) {
            outMessage =
                "Refused to roll back VFX files from invalid roots";
            return false;
        }
        std::vector<IO::FileReplacementOperation> operations{};
        operations.reserve(2u);
        if (!AppendCreatedFileRollback(
                files.roots.assetsRoot,
                files.sourcePath,
                files.sourceExistedBefore,
                operations,
                outMessage) ||
            !AppendCreatedFileRollback(
                files.roots.sourceMetaRoot,
                files.metaPath,
                files.metaExistedBefore,
                operations,
                outMessage)) {
            return false;
        }
        return IO::CommitFileReplacementTransaction(
            std::span<const IO::FileReplacementOperation>(operations),
            outMessage);
    }

    bool RollBackCreatedVfxPrivateMaterials(
        const VfxAssetAuthoringRoots& roots,
        const std::vector<std::filesystem::path>& sourcePaths,
        const std::vector<std::filesystem::path>& metaPaths,
        std::string& outMessage) {

        if (sourcePaths.size() != metaPaths.size() ||
            !IsStrictlyUnderRoot(roots.projectRoot, roots.assetsRoot) ||
            !IsStrictlyUnderRoot(
                roots.projectRoot, roots.sourceMetaRoot)) {
            outMessage =
                "Refused to roll back invalid VFX private material targets";
            return false;
        }
        std::vector<IO::FileReplacementOperation> operations{};
        operations.reserve(sourcePaths.size() * 2u);
        for (size_t index = 0u; index < sourcePaths.size(); ++index) {
            std::filesystem::path sourcePath{};
            std::filesystem::path metaPath{};
            if (!ResolveAbsolutePath(
                    sourcePaths[index], sourcePath, outMessage) ||
                !ResolveAbsolutePath(
                    metaPaths[index], metaPath, outMessage) ||
                !IsStrictlyUnderRoot(roots.assetsRoot, sourcePath) ||
                !IsStrictlyUnderRoot(roots.sourceMetaRoot, metaPath) ||
                !sourcePath.generic_string().ends_with(".material.json")) {
                outMessage =
                    "Refused to roll back a private material outside its VFX roots";
                return false;
            }
            if (!AppendCreatedFileRollback(
                    roots.assetsRoot,
                    sourcePath,
                    false,
                    operations,
                    outMessage) ||
                !AppendCreatedFileRollback(
                    roots.sourceMetaRoot,
                    metaPath,
                    false,
                    operations,
                    outMessage)) {
                return false;
            }
        }
        return IO::CommitFileReplacementTransaction(
            std::span<const IO::FileReplacementOperation>(operations),
            outMessage);
    }

} // namespace HIKARI::EDITOR::INTERNAL
