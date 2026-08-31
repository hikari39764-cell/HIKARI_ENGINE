#pragma once

#if defined(_WIN32) && defined(HIKARI_GAME_PROJECT_SDK_EXPORTS)
#define HIKARI_GAME_PROJECT_SDK_API __declspec(dllexport)
#elif defined(_WIN32) && defined(HIKARI_GAME_PROJECT_SDK_IMPORTS)
#define HIKARI_GAME_PROJECT_SDK_API __declspec(dllimport)
#else
#define HIKARI_GAME_PROJECT_SDK_API
#endif

namespace HIKARI::SDK {

    inline constexpr unsigned int kVersionMajor = 2u;
    inline constexpr unsigned int kVersionMinor = 10u;
    inline constexpr unsigned int kVersionPatch = 0u;

} // namespace HIKARI::SDK
