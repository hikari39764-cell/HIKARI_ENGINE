#pragma once

#include <cstddef>
#include <filesystem>
#include <map>
#include <string>

// SDK の界面の指紋。引擎の構築が、SDK が配る編訳対象（公開ヘッダー、共用の核、入口アダプター）ごとに、注釈と空白を
// 除いた中身のハッシュを取る（Tools/SdkInterface）。モジュールの構築は、DLL を編訳する時に実際に読んだ SDK の
// ファイルとその指紋を DLL の隣に記録する。その記録が host の表と合う間は、引擎が変わっても DLL をそのまま読める：
// 手で上げる版番号は無い。
namespace HIKARI {

struct SdkInterfaceEntry {
    // SDK の根からの相対路径（区切りは /）。
    const char* path = nullptr;
    const char* hash = nullptr;
};

// 相対路径 → 指紋。
using SdkInterfaceFiles = std::map<std::string, std::string>;

// SDK の根に置く表（Sdk/HIKARI_SdkInterface.json）と、DLL の隣の記録（<library>.interface.json）は同じ形の JSON。
inline constexpr char kSdkInterfaceFilename[] = "HIKARI_SdkInterface.json";
inline constexpr char kLibraryInterfaceExtension[] = ".interface.json";

// この host を編訳した時の SDK の表（構築が埋め込む）。
const SdkInterfaceFiles& GetHostSdkInterface();

bool LoadSdkInterfaceFile(const std::filesystem::path& path, SdkInterfaceFiles& outFiles,
                          std::string* outMessage = nullptr);

// <フォルダ>/<library>.dll → <フォルダ>/<library>.interface.json。
std::filesystem::path MakeLibraryInterfacePath(const std::filesystem::path& library);

// library が編訳の時に読んだ SDK のファイルが、provided と全部同じ指紋か。違う・記録が無い・読めない時は false と、
// 何が違うか（「it was built against …」の形で、文の途中に入れられる）。
bool IsLibraryInterfaceCurrent(const std::filesystem::path& library, const SdkInterfaceFiles& provided,
                               std::string* outMessage = nullptr);

} // namespace HIKARI
