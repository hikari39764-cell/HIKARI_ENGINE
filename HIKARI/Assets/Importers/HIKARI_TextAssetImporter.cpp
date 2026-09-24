#include "Assets/Importers/HIKARI_TextAssetImporter.h"
#include "Assets/Text/HIKARI_TextStyleAsset.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Core/Hashing/HIKARI_FnvHash.h"
#include <fstream>
#include <set>
namespace HIKARI {
namespace {
// SFNT のテーブル境界を検査する。グリフ展開とキャッシュは既存の SDF
// 描画系に委譲する。
bool ValidateFont(const std::filesystem::path& path, std::string& error, uint64_t& contentHash) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        error = "Font file cannot be opened.";
        return false;
    }
    const auto length = file.tellg();
    if (length < 12 || length > 256 * 1024 * 1024) {
        error = "Font file size is invalid.";
        return false;
    }
    std::vector<unsigned char> bytes(static_cast<size_t>(length));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(bytes.data()), length);
    if (!file) {
        error = "Font file is incomplete.";
        return false;
    }
    auto u32 = [&](size_t at) -> uint32_t {
        return (uint32_t(bytes[at]) << 24) | (uint32_t(bytes[at + 1]) << 16) | (uint32_t(bytes[at + 2]) << 8) |
               bytes[at + 3];
    };
    size_t offset = 0;
    if (u32(0) == 0x74746366u) {
        if (bytes.size() < 16 || u32(8) == 0 || u32(8) > (bytes.size() - 12) / 4) {
            error = "Invalid TTC font collection.";
            return false;
        }
        offset = u32(12);
    }
    if (offset > bytes.size() - 12) {
        error = "Font header is truncated.";
        return false;
    }
    const auto signature = u32(offset);
    if (signature != 0x00010000u && signature != 0x4f54544fu && signature != 0x74727565u) {
        error = "Unsupported font container.";
        return false;
    }
    const size_t count = (size_t(bytes[offset + 4]) << 8) | bytes[offset + 5];
    if (count == 0 || count > (bytes.size() - offset - 12) / 16) {
        error = "Font table directory is truncated.";
        return false;
    }
    std::set<uint32_t> tables;
    for (size_t i = 0; i < count; ++i) {
        const size_t at = offset + 12 + i * 16;
        const size_t start = u32(at + 8), size = u32(at + 12);
        if (start > bytes.size() || size > bytes.size() - start) {
            error = "Font table exceeds file bounds.";
            return false;
        }
        tables.insert(u32(at));
    }
    for (uint32_t tag : {0x636d6170u, 0x68656164u, 0x68686561u, 0x686d7478u, 0x6d617870u})
        if (!tables.contains(tag)) {
            error = "Font is missing a required rendering table.";
            return false;
        }
    contentHash = HASH::Fnv1aBytes(HASH::kFnv1a64OffsetBasis, bytes.data(), bytes.size());
    return true;
}
} // namespace
ASSETS::SEMANTICS::AssetImporterKind TextAssetImporter::GetImporterKind() const noexcept {
    return style_ ? ASSETS::SEMANTICS::AssetImporterKind::TextStyle : ASSETS::SEMANTICS::AssetImporterKind::Font;
}
AssetMeta TextAssetImporter::CreateDefaultMeta(const std::filesystem::path& path, const AssetGuid& guid) const {
    return ASSETS::SEMANTICS::MakeBaseAssetMeta(path, guid, GetImporterKind());
}
AssetImportResult TextAssetImporter::Import(const AssetRecord& record, const AssetImportContext& context) {
    AssetImportResult result;
    const auto path = record.sourcePath.is_absolute() ? record.sourcePath : context.projectRoot / record.sourcePath;
    uint64_t contentHash = 0;
    if (style_) {
        TextStyleAsset style;
        result.success = LoadTextStyleAsset(path, style, result.message);
        if (result.success) {
            result.dependencies = CollectTextStyleDependencies(style);
            if (style.font.IsValid() && context.assetDatabase) {
                const auto* font = context.assetDatabase->FindByGuid(style.font);
                if (!font || font->type != AssetType::Font) {
                    result.success = false;
                    result.message = "Text style references a missing font asset.";
                }
            }
        }
    } else
        result.success = ValidateFont(path, result.message, contentHash);
    if (result.success)
        result.message = style_ ? "Text style validated" : "Font validated";
    result.diagnosticsJson =
        nlohmann::json{{"valid", result.success}, {"contentHash", contentHash}, {"kind", style_ ? "TextStyle" : "Font"}}
            .dump();
    return result;
}
} // namespace HIKARI
