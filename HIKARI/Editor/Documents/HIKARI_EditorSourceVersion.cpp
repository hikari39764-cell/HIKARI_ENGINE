#include "HIKARI_EditorSourceVersion.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Core/IO/HIKARI_NativeFilePath.h"
#include "Core/Hashing/HIKARI_FnvHash.h"
#include <fstream>
#include <array>

namespace HIKARI::EDITOR {
namespace {
bool Fingerprint(const std::filesystem::path& path, uint64_t& hash) {
    hash = 0;
    if (path.empty())
        return true;
    const std::filesystem::path nativePath = IO::NativeFilePath(path);
    std::error_code error;
    const bool exists = std::filesystem::exists(nativePath, error);
    if (error)
        return false;
    if (!exists)
        return true;
    std::ifstream input(nativePath, std::ios::binary);
    if (!input)
        return false;
    hash = HASH::kFnv1a64OffsetBasis;
    std::array<char, 8192> bytes;
    while (input) {
        input.read(bytes.data(), bytes.size());
        hash = HASH::Fnv1aBytes(hash, bytes.data(), static_cast<size_t>(input.gcount()));
    }
    return input.eof();
}
} // namespace
void EditorSourceVersion::Accept(const std::filesystem::path& path) {
    path_ = path;
    readable_ = Fingerprint(path_, hash_);
}
void EditorSourceVersion::Accept(const AssetDatabase& database, const AssetGuid& guid) {
    const auto* record = database.FindByGuid(guid);
    auto path = record ? record->sourcePath : std::filesystem::path{};
    if (!path.empty() && path.is_relative())
        path = database.GetProjectRoot() / path;
    Accept(path);
    if (guid.IsValid() && !record)
        readable_ = false;
}
bool EditorSourceVersion::CanSave(std::string& message) const {
    uint64_t current = 0;
    if (readable_ && Fingerprint(path_, current) && current == hash_)
        return true;
    message = "源文件已被另一文档或外部程序修改，或暂时无法读取。当前草稿已保留；请另存副本，或核对后重新打开。";
    return false;
}
} // namespace HIKARI::EDITOR
