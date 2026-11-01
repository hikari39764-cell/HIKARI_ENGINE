#pragma once
#include "Assets/ContentLibrary/HIKARI_ContentLibrary.h"
#include "RenderContracts/Asset/HIKARI_AssetTypes.h"
#include "RenderContracts/Material/HIKARI_MaterialAssetData.h"
#include "Assets/Vfx/HIKARI_VfxAssetData.h"
#include "Core/IO/HIKARI_FileReplacementTransaction.h"
#include <functional>
#include <json.hpp>
#include <variant>

namespace HIKARI::ASSETS::LIBRARY::INTERNAL {
using Source = std::variant<std::monostate, MaterialAssetData, VFX::VfxAssetData>;
bool LoadSource(const std::filesystem::path& path, AssetType type, Source& out, std::string& message);
bool SaveSource(const std::filesystem::path& path, const Source& source, std::string& message);
void VisitReferences(Source& source, const std::function<void(AssetGuid&)>& visit);
bool SafeKey(std::string_view value);
bool IsWithin(const std::filesystem::path& root, const std::filesystem::path& path);
bool ResolveMember(const std::filesystem::path& root, std::string_view relative, std::filesystem::path& out,
                   std::string& message);
bool ReadManifest(const std::filesystem::path& path, nlohmann::json& out, ContentPackageInfo& info,
                  std::string& message);
bool FileHash(const std::filesystem::path& path, std::string& out, std::string& message);
// 一時領域のみを所有し、終了時に削除する。
class StagingDirectory {
public:
    explicit StagingDirectory(const std::filesystem::path& parent);
    ~StagingDirectory();
    const std::filesystem::path path;
};
} // namespace HIKARI::ASSETS::LIBRARY::INTERNAL
