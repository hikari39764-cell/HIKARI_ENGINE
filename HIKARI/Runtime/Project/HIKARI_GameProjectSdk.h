#pragma once

#if defined(_WIN32) && defined(HIKARI_GAME_PROJECT_SDK_EXPORTS)
#define HIKARI_GAME_PROJECT_SDK_API __declspec(dllexport)
#elif defined(_WIN32) && defined(HIKARI_GAME_PROJECT_SDK_IMPORTS)
#define HIKARI_GAME_PROJECT_SDK_API __declspec(dllimport)
#else
#define HIKARI_GAME_PROJECT_SDK_API
#endif
