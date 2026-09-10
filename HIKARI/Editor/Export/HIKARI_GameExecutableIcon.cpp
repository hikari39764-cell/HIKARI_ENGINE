#include "Editor/Export/HIKARI_GameExecutableIcon.h"
#include "Platform/HIKARI_ApplicationIcon.h"
#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace HIKARI::EDITOR {
    namespace {
        struct IconImage {
            BYTE width, height, colors;
            WORD planes, bits;
            std::vector<BYTE> data;
        };
        WORD Read16(const BYTE* p) { return WORD(p[0] | (WORD(p[1]) << 8)); }
        DWORD Read32(const BYTE* p) { return DWORD(Read16(p)) | (DWORD(Read16(p + 2)) << 16); }
        void Append16(std::vector<BYTE>& out, WORD value) {
            out.push_back(BYTE(value)); out.push_back(BYTE(value >> 8));
        }
        void Append32(std::vector<BYTE>& out, DWORD value) {
            Append16(out, WORD(value)); Append16(out, WORD(value >> 16));
        }
        bool ReadIcon(const std::filesystem::path& path, std::vector<IconImage>& images, std::string& message) {
            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            const auto length = stream ? stream.tellg() : std::streampos(-1);
            if (length < 6 || length > 16 * 1024 * 1024) {
                message = "ICO ファイルを開けないか、サイズが無効です（最大 16 MB）。"; return false;
            }
            std::vector<BYTE> bytes(static_cast<size_t>(length));
            stream.seekg(0);
            if (!stream.read(reinterpret_cast<char*>(bytes.data()), length)) {
                message = "ICO ファイルを読み込めません。"; return false;
            }
            const auto count = Read16(bytes.data() + 4);
            if (Read16(bytes.data()) != 0 || Read16(bytes.data() + 2) != 1 ||
                count == 0 || count > 128 || 6u + size_t(count) * 16u > bytes.size()) {
                message = "有効な Windows ICO ファイルを選んでください。"; return false;
            }
            for (size_t i = 0; i < count; ++i) {
                const auto* entry = bytes.data() + 6 + i * 16;
                const size_t size = Read32(entry + 8), offset = Read32(entry + 12);
                if (size < 16 || offset < 6u + size_t(count) * 16u ||
                    offset > bytes.size() || size > bytes.size() - offset) {
                    message = "ICO 内の画像データが破損しています。"; return false;
                }
                IconImage image{entry[0], entry[1], entry[2], Read16(entry + 4), Read16(entry + 6),
                    std::vector<BYTE>(bytes.begin() + offset, bytes.begin() + offset + size)};
                HICON decoded = CreateIconFromResourceEx(image.data.data(), static_cast<DWORD>(size), TRUE,
                    0x00030000, image.width ? image.width : 256, image.height ? image.height : 256, LR_DEFAULTCOLOR);
                if (!decoded) { message = "ICO 内に表示できない画像があります。"; return false; }
                DestroyIcon(decoded);
                images.push_back(std::move(image));
            }
            return true;
        }
    }
    bool ValidateGameExecutableIcon(const std::filesystem::path& icon, std::string& message) {
        std::vector<IconImage> images;
        return ReadIcon(icon, images, message);
    }
    bool EmbedGameExecutableIcon(const std::filesystem::path& executable,
        const std::filesystem::path& icon, std::string& message) {
        if (icon.empty()) return true;
        std::vector<IconImage> images;
        if (!ReadIcon(icon, images, message)) return false;
        HANDLE update = BeginUpdateResourceW(executable.c_str(), FALSE);
        if (!update) { message = "出力 EXE のアイコン更新を開始できません。"; return false; }
        std::vector<BYTE> group;
        Append16(group, 0); Append16(group, 1); Append16(group, static_cast<WORD>(images.size()));
        bool success = true;
        WORD id = 1;
        for (auto& image : images) {
            group.insert(group.end(), {image.width, image.height, image.colors, 0});
            Append16(group, image.planes); Append16(group, image.bits);
            Append32(group, static_cast<DWORD>(image.data.size())); Append16(group, id);
            if (!UpdateResourceW(update, RT_ICON, MAKEINTRESOURCEW(id++), MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
                image.data.data(), static_cast<DWORD>(image.data.size()))) success = false;
        }
        if (success) success = UpdateResourceW(update, RT_GROUP_ICON,
            MAKEINTRESOURCEW(PLATFORM::kApplicationIconResourceId), MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
            group.data(), static_cast<DWORD>(group.size())) != FALSE;
        if (!EndUpdateResourceW(update, success ? FALSE : TRUE)) success = false;
        if (!success) { message = "出力 EXE にアイコンを保存できません。"; return false; }
        HMODULE module = LoadLibraryExW(executable.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
        HRSRC resource = module ? FindResourceW(module, MAKEINTRESOURCEW(PLATFORM::kApplicationIconResourceId), RT_GROUP_ICON) : nullptr;
        const auto size = resource ? SizeofResource(module, resource) : 0u;
        const auto* data = resource ? LockResource(LoadResource(module, resource)) : nullptr;
        success = data && size == group.size() && std::memcmp(data, group.data(), group.size()) == 0;
        if (module) FreeLibrary(module);
        if (!success) message = "出力 EXE のアイコン検証に失敗しました。";
        return success;
    }
}
