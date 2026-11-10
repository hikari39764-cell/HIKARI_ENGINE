// SDK の界面の指紋を扱う構築用の道具（HIKARI_SdkInterface.exe）。引擎のライブラリには繋がず、SDK と一緒に配る。
//
//   table  <一覧> <出力.json> <出力.cpp>
//       引擎の構築が呼ぶ。一覧は「SDK の中の相対路径|元のファイル」の行。SDK が配る編訳対象（公開ヘッダー、共用の核、
//       入口アダプター）の指紋の表を、SDK に置く JSON と、host に埋め込む C++ の表として書く。
//   record <構築木> <構成> <SDK の根> <出力.json>...
//       モジュールの構築が呼ぶ。編訳器が実際に読んだファイル（MSBuild が残す *.read.*.tlog）のうち SDK の物を拾い、
//       その指紋を DLL の隣の <library>.interface.json に書く。引擎は DLL を読む前に、この記録と自分の表を比べる。
//   check  <使った側.json> <提供する側.json>
//       使った側の全ファイルが提供する側と同じ指紋なら 0、違えば 1（違うファイルを一行ずつ出す）。
//
// 指紋は、注釈・字下げ・空行・改行の書き方（CRLF / LF）を除いた中身の SHA-256 の先頭 16 バイト。注釈や整形だけの
// 変更では変わらない。文字列の中身はそのまま数える。
#include <Windows.h>

#include <bcrypt.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <json.hpp>

namespace {

using Json = nlohmann::json;
using InterfaceFiles = std::map<std::string, std::string>;

constexpr int kFormatVersion = 1;
constexpr int kExitDifferent = 1;
constexpr int kExitError = 2;
constexpr size_t kFingerprintBytes = 16u;
// SDK の中で、モジュールの編訳に入るファイルがあるフォルダ。
constexpr const wchar_t* kInterfaceDirectories[] = {L"include", L"shared", L"third_party", L"src"};

struct Failure {
    std::string message{};
};

std::string Narrow(std::wstring_view wide) {
    if (wide.empty()) {
        return {};
    }
    const int size =
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string text(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), text.data(), size, nullptr, nullptr);
    return text;
}

std::wstring Widen(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring text(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), text.data(), size);
    return text;
}

std::wstring Lower(std::wstring text) {
    if (!text.empty()) {
        CharLowerBuffW(text.data(), static_cast<DWORD>(text.size()));
    }
    return text;
}

std::string PathText(const std::filesystem::path& path) { return Narrow(path.native()); }

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw Failure{"cannot read " + PathText(path)};
    }
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

// 中身が同じなら書かない（書くと、これに依る構築が毎回やり直しになる）。
void WriteFileIfChanged(const std::filesystem::path& path, std::string_view content) {
    std::error_code ec{};
    if (std::filesystem::is_regular_file(path, ec) && ReadFile(path) == content) {
        return;
    }
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!file) {
        throw Failure{"cannot write " + PathText(path)};
    }
}

bool IsIdentifierChar(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

// out の末尾が生の文字列の接頭辞（R、LR、uR、UR、u8R）か。
bool EndsWithRawPrefix(std::string_view out) noexcept {
    size_t begin = out.size();
    while (begin > 0u && IsIdentifierChar(out[begin - 1u])) {
        --begin;
    }
    const std::string_view word = out.substr(begin);
    return word == "R" || word == "LR" || word == "uR" || word == "UR" || word == "u8R";
}

// C++ のソースから、編訳の結果に関わらない書き方の違いを落とす：
//   注釈は消す。空白と改行の並びは一つにまとめる（改行を含んでいたら改行、無ければ空白）。CR は捨てる。
//   文字列と文字の定数は中身をそのまま残す。
std::string Normalize(std::string_view source) {
    enum class Gap { None, Space, Line };
    std::string out{};
    out.reserve(source.size());
    Gap gap = Gap::None;
    const size_t size = source.size();
    const auto at = [&](size_t index) { return index < size ? source[index] : '\0'; };
    // 字句の前に、溜めた区切りを一つだけ出す。
    const auto separate = [&] {
        if (gap != Gap::None && !out.empty()) {
            out += gap == Gap::Line ? '\n' : ' ';
        }
        gap = Gap::None;
    };
    const auto widen = [&](Gap wanted) { gap = (std::max)(gap, wanted); };
    // 定数の中身を写す（CR だけ捨てる）。
    const auto copy = [&](size_t from, size_t to) {
        for (size_t index = from; index < to; ++index) {
            if (source[index] != '\r') {
                out += source[index];
            }
        }
    };

    size_t i = 0u;
    while (i < size) {
        const char c = source[i];
        if (c == '\r') {
            ++i;
        } else if (c == '\n') {
            widen(Gap::Line);
            ++i;
        } else if (c == ' ' || c == '\t' || c == '\v' || c == '\f') {
            widen(Gap::Space);
            ++i;
        } else if (c == '/' && at(i + 1u) == '/') {
            // 行末まで。行末の \ は次の行へ続く。
            while (i < size && source[i] != '\n') {
                if (source[i] == '\\' && (at(i + 1u) == '\n' || (at(i + 1u) == '\r' && at(i + 2u) == '\n'))) {
                    i += at(i + 1u) == '\n' ? 2u : 3u;
                } else {
                    ++i;
                }
            }
            widen(Gap::Space);
        } else if (c == '/' && at(i + 1u) == '*') {
            const size_t end = source.find("*/", i + 2u);
            i = end == std::string_view::npos ? size : end + 2u;
            widen(Gap::Space);
        } else if (c == '"' && EndsWithRawPrefix(out)) {
            // R"区切り( … )区切り"
            const size_t open = source.find('(', i + 1u);
            size_t end = size;
            if (open != std::string_view::npos && open - i - 1u <= 16u) {
                const std::string close = ")" + std::string(source.substr(i + 1u, open - i - 1u)) + "\"";
                const size_t found = source.find(close, open + 1u);
                end = found == std::string_view::npos ? size : found + close.size();
            }
            separate();
            copy(i, end);
            i = end;
        } else if (c == '"' || c == '\'') {
            // 同じ行の閉じる引用符まで。閉じていなければ（数字の区切りの ' など）引用符だけを字句として出す。
            size_t end = i + 1u;
            while (end < size && source[end] != c && source[end] != '\n') {
                end += source[end] == '\\' ? 2u : 1u;
            }
            const bool closed = end < size && source[end] == c;
            separate();
            copy(i, closed ? end + 1u : i + 1u);
            i = closed ? end + 1u : i + 1u;
        } else {
            separate();
            out += c;
            ++i;
        }
    }
    return out;
}

std::string Fingerprint(std::string_view content) {
    static BCRYPT_ALG_HANDLE algorithm = [] {
        BCRYPT_ALG_HANDLE handle = nullptr;
        if (BCryptOpenAlgorithmProvider(&handle, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) {
            throw Failure{"SHA-256 is unavailable"};
        }
        return handle;
    }();
    const std::string normalized = Normalize(content);
    unsigned char digest[32]{};
    if (BCryptHash(algorithm, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(normalized.data())),
                   static_cast<ULONG>(normalized.size()), digest, sizeof(digest)) < 0) {
        throw Failure{"SHA-256 failed"};
    }
    constexpr char kHex[] = "0123456789abcdef";
    std::string text{};
    for (size_t index = 0u; index < kFingerprintBytes; ++index) {
        text += kHex[digest[index] >> 4u];
        text += kHex[digest[index] & 0x0Fu];
    }
    return text;
}

std::string SerializeInterface(const InterfaceFiles& files) {
    return Json{{"formatVersion", kFormatVersion}, {"files", files}}.dump(2) + "\n";
}

InterfaceFiles LoadInterface(const std::filesystem::path& path) {
    const Json root = Json::parse(ReadFile(path), nullptr, false);
    if (!root.is_object() || root.value("formatVersion", 0) != kFormatVersion || !root.contains("files") ||
        !root["files"].is_object()) {
        throw Failure{PathText(path) + " is not an SDK interface file of format " + std::to_string(kFormatVersion)};
    }
    InterfaceFiles files{};
    for (const auto& [name, hash] : root["files"].items()) {
        if (!hash.is_string()) {
            throw Failure{PathText(path) + ": the fingerprint of " + name + " must be text"};
        }
        files.emplace(name, hash.get<std::string>());
    }
    return files;
}

std::string EscapeCpp(std::string_view text) {
    std::string out{};
    for (const char c : text) {
        if (c == '\\' || c == '"') {
            out += '\\';
        }
        out += c;
    }
    return out;
}

// table <一覧> <出力.json> <出力.cpp>
int MakeTable(const std::vector<std::filesystem::path>& arguments) {
    if (arguments.size() != 3u) {
        throw Failure{"usage: table <list> <out.json> <out.cpp>"};
    }
    InterfaceFiles files{};
    std::string_view rest{};
    const std::string list = ReadFile(arguments[0]);
    rest = list;
    while (!rest.empty()) {
        const size_t lineEnd = rest.find('\n');
        std::string_view line = rest.substr(0u, lineEnd);
        rest = lineEnd == std::string_view::npos ? std::string_view{} : rest.substr(lineEnd + 1u);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1u);
        }
        if (line.empty()) {
            continue;
        }
        const size_t bar = line.find('|');
        if (bar == std::string_view::npos || bar == 0u) {
            throw Failure{"list lines must be <path in the SDK>|<source file>: " + std::string(line)};
        }
        const std::string name(line.substr(0u, bar));
        if (!files.emplace(name, Fingerprint(ReadFile(Widen(line.substr(bar + 1u))))).second) {
            throw Failure{"the list names " + name + " twice"};
        }
    }
    if (files.empty()) {
        throw Failure{"the list is empty"};
    }
    WriteFileIfChanged(arguments[1], SerializeInterface(files));

    std::string cpp = "// 構築が書く（Tools/SdkInterface）。手で書き換えない。\n"
                      "// この host を編訳した時の、SDK が配るファイルの指紋。\n"
                      "#include \"Project/HIKARI_SdkInterface.h\"\n\n"
                      "namespace HIKARI {\n\n"
                      "extern const SdkInterfaceEntry kHostSdkInterface[] = {\n";
    for (const auto& [name, hash] : files) {
        cpp += "    {\"" + EscapeCpp(name) + "\", \"" + hash + "\"},\n";
    }
    cpp += "};\nextern const size_t kHostSdkInterfaceCount = " + std::to_string(files.size()) +
           "u;\n\n} // namespace HIKARI\n";
    WriteFileIfChanged(arguments[2], cpp);
    return 0;
}

// MSBuild の追跡ログ。UTF-16（BOM 付きが普通）で、行ごとに路径が一つ。「^」で始まる行は編訳したソース（| で区切る）。
std::wstring DecodeLog(const std::string& bytes) {
    const bool utf16 = (bytes.size() >= 2u && static_cast<unsigned char>(bytes[0]) == 0xFFu &&
                        static_cast<unsigned char>(bytes[1]) == 0xFEu) ||
                       (bytes.size() >= 2u && bytes.size() % 2u == 0u && bytes[1] == '\0');
    if (!utf16) {
        return Widen(bytes);
    }
    const size_t skip = static_cast<unsigned char>(bytes[0]) == 0xFFu ? 2u : 0u;
    std::wstring text((bytes.size() - skip) / 2u, L'\0');
    std::memcpy(text.data(), bytes.data() + skip, text.size() * sizeof(wchar_t));
    return text;
}

// record <構築木> <構成> <SDK の根> <出力.json>...
int Record(const std::vector<std::filesystem::path>& arguments) {
    if (arguments.size() < 4u) {
        throw Failure{"usage: record <build tree> <configuration> <sdk root> <out.json>..."};
    }
    std::error_code ec{};
    const std::filesystem::path buildTree = arguments[0];
    const std::wstring configuration = Lower(arguments[1].native());
    const std::filesystem::path sdkRoot = std::filesystem::absolute(arguments[2], ec).lexically_normal();

    // SDK が配る編訳対象：小文字の絶対路径 → SDK の中の相対路径（ログの路径は大文字になっている）。
    std::map<std::wstring, std::filesystem::path> sdkFiles{};
    for (const wchar_t* directory : kInterfaceDirectories) {
        for (std::filesystem::recursive_directory_iterator entry(sdkRoot / directory, ec), end; !ec && entry != end;
             entry.increment(ec)) {
            if (entry->is_regular_file(ec)) {
                sdkFiles.emplace(Lower(entry->path().lexically_normal().native()),
                                 entry->path().lexically_relative(sdkRoot));
            }
        }
        ec.clear();
    }
    if (sdkFiles.empty()) {
        throw Failure{"no SDK files under " + PathText(sdkRoot)};
    }

    // <目標>.dir/<構成>/<目標>.tlog/*.read.*.tlog を、構築木の全部の目標について読む（静的ライブラリの分も要る）。
    std::set<std::filesystem::path> used{};
    size_t logs = 0u;
    for (std::filesystem::recursive_directory_iterator
             entry(buildTree, std::filesystem::directory_options::skip_permission_denied, ec),
         end;
         !ec && entry != end; entry.increment(ec)) {
        if (!entry->is_directory(ec) || Lower(entry->path().extension().native()) != L".tlog") {
            continue;
        }
        entry.disable_recursion_pending();
        if (Lower(entry->path().parent_path().filename().native()) != configuration) {
            continue;
        }
        for (const auto& log : std::filesystem::directory_iterator(entry->path(), ec)) {
            const std::wstring name = Lower(log.path().filename().native());
            if (name.find(L".read.") == std::wstring::npos || !name.ends_with(L".tlog")) {
                continue;
            }
            ++logs;
            const std::wstring text = DecodeLog(ReadFile(log.path()));
            size_t begin = 0u;
            while (begin < text.size()) {
                const size_t stop = text.find_first_of(L"|\r\n", begin);
                const size_t finish = stop == std::wstring::npos ? text.size() : stop;
                std::wstring_view item(text.data() + begin, finish - begin);
                if (!item.empty() && item.front() == L'^') {
                    item.remove_prefix(1u);
                }
                if (const auto found = sdkFiles.find(Lower(std::wstring(item))); found != sdkFiles.end()) {
                    used.insert(found->second);
                }
                begin = finish + 1u;
            }
        }
    }
    if (logs == 0u) {
        throw Failure{"no compiler read logs (*.read.*.tlog) for " + Narrow(configuration) + " under " +
                      PathText(buildTree) + ": build the module with the Visual Studio generator first"};
    }

    InterfaceFiles files{};
    bool readsHeaders = false;
    for (const std::filesystem::path& relative : used) {
        const std::string name = Narrow(relative.generic_wstring());
        readsHeaders = readsHeaders || name.starts_with("include/");
        files.emplace(name, Fingerprint(ReadFile(sdkRoot / relative)));
    }
    // SDK のヘッダーを一つも読まないモジュールは無い（入口アダプターが読む）：ログが欠けている。
    if (!readsHeaders) {
        throw Failure{"the compiler read logs under " + PathText(buildTree) + " name no SDK header of " +
                      PathText(sdkRoot) + ": rebuild the module from a clean build tree"};
    }
    const std::string content = SerializeInterface(files);
    for (size_t index = 3u; index < arguments.size(); ++index) {
        WriteFileIfChanged(arguments[index], content);
    }
    return 0;
}

// check <使った側.json> <提供する側.json>
int Check(const std::vector<std::filesystem::path>& arguments) {
    if (arguments.size() != 2u) {
        throw Failure{"usage: check <used.json> <provided.json>"};
    }
    const InterfaceFiles used = LoadInterface(arguments[0]);
    const InterfaceFiles provided = LoadInterface(arguments[1]);
    int result = 0;
    for (const auto& [name, hash] : used) {
        const auto found = provided.find(name);
        if (found == provided.end() || found->second != hash) {
            std::printf("%s %s\n", found == provided.end() ? "removed" : "changed", name.c_str());
            result = kExitDifferent;
        }
    }
    return result;
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    try {
        const std::wstring command = argc > 1 ? argv[1] : L"";
        const std::vector<std::filesystem::path> arguments(argv + (std::min)(argc, 2), argv + argc);
        if (command == L"table") {
            return MakeTable(arguments);
        }
        if (command == L"record") {
            return Record(arguments);
        }
        if (command == L"check") {
            return Check(arguments);
        }
        throw Failure{"usage: HIKARI_SdkInterface table|record|check ..."};
    } catch (const Failure& failure) {
        std::fprintf(stderr, "HIKARI_SdkInterface: %s\n", failure.message.c_str());
    } catch (const std::exception& exception) {
        std::fprintf(stderr, "HIKARI_SdkInterface: %s\n", exception.what());
    }
    return kExitError;
}
