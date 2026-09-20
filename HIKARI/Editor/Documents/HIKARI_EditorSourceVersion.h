#pragma once
#include <filesystem>
#include <string>
#include <cstdint>
#include "Assets/HIKARI_AssetGuid.h"

namespace HIKARI { class AssetDatabase; }
namespace HIKARI::EDITOR {
// 各文書が開いたソースの世代を保持する。人と AI の双方の保存に適用する。
class EditorSourceVersion {
public:
    void Accept(const std::filesystem::path& path);
    void Accept(const AssetDatabase& database, const AssetGuid& guid);
    bool CanSave(std::string& message) const;
    uint64_t GetFingerprint() const noexcept { return hash_; }
    bool IsReadable() const noexcept { return readable_; }
    void RestoreFingerprint(uint64_t hash, bool readable) noexcept { hash_ = hash; readable_ = readable; }
private:
    std::filesystem::path path_;
    uint64_t hash_ = 0;
    bool readable_ = true;
};
}
