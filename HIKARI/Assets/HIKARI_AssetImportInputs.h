#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

#include "Assets/HIKARI_AssetArtifactManifest.h"

namespace HIKARI {
class AssetDatabase;
class AssetImportPublication;
struct AssetRecord;
struct AssetMeta;

// 取り込み入力ファイルの SHA-256 を、サイズと NTFS の変更時刻（内容・属性・名前を変えると進み、更新時刻を
// 元に戻しても戻らない）とともに Library へ保存する。起動時の走査で変わっていない入力を読み直さない。
// 資産データベースの所有スレッドだけが使う。
class AssetInputDigestCache {
public:
    void Open(const std::filesystem::path& cachePath);
    void BeginScan();
    // 走査が成功した時は、その走査で参照しなかった項目を捨てて保存する。
    void FinishScan(bool succeeded);
    // key は入力の記録名（AssetImportInputFile::path）。読めない時は空文字列。
    std::string Digest(const std::string& key, const std::filesystem::path& path);

private:
    struct Entry {
        uint64_t size = 0;
        int64_t changeTime = 0;
        std::string digest{};
        bool scanned = false;
    };

    std::filesystem::path path_{};
    std::unordered_map<std::string, Entry> entries_{};
    bool scanning_ = false;
    bool dirty_ = false;
};

AssetImportInputFile CaptureAssetInputFile(const std::filesystem::path& path);
std::string CanonicalImportSettings(std::string_view settings);
bool ValidateAssetImportSettings(const AssetMeta& edited, const AssetMeta& defaults, std::string& error);
bool EquivalentAssetImportMeta(const AssetMeta& left, const AssetMeta& right);
AssetImportInputs CaptureAssetImportInputs(const AssetDatabase& database, const AssetRecord& record,
                                           const std::vector<AssetDependencyDesc>& dependencies,
                                           const AssetImportPublication* publication = nullptr);
bool CheckAssetImportInputs(const std::filesystem::path& projectRoot, const AssetImportInputs& inputs,
                            std::string& reason, const AssetImportPublication* publication = nullptr,
                            AssetInputDigestCache* digests = nullptr);
} // namespace HIKARI
