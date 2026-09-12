#include <Windows.h>
#include <cerrno>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <string>

#include "Assets/Importers/HIKARI_IblBaker.h"

namespace {
    bool ParseBudget(const wchar_t* text, uint32_t& value) {
        wchar_t* end = nullptr;
        errno = 0;
        const unsigned long parsed = std::wcstoul(text, &end, 10);
        if (errno != 0 || end == text || *end != L'\0' || parsed == 0 || parsed > 4096) return false;
        value = static_cast<uint32_t>(parsed);
        return true;
    }
}

int wmain(int argc, wchar_t** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (argc < 2 || argc > 4) {
        std::fputs("Usage: HIKARI_IblBakeTool <output.dds> [size=256] [samples=1024]\n", stderr);
        return 2;
    }
    uint32_t size = 256, samples = 1024;
    if ((argc > 2 && !ParseBudget(argv[2], size)) || (argc > 3 && !ParseBudget(argv[3], samples))) {
        std::fputs("Size and sample budget must be positive integers no greater than 4096.\n", stderr);
        return 2;
    }
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(com)) return 1;
    std::string message;
    // Runtime と同じ Baker/署名/トランザクションを使い、Editor の起動には依存しない。
    const bool success = HIKARI::IblBaker::EnsureSharedBrdfLut(
        std::filesystem::absolute(argv[1]), size, samples, false, &message);
    std::fprintf(success ? stdout : stderr, "%s\n", message.c_str());
    CoUninitialize();
    return success ? 0 : 1;
}
