#pragma once

#if defined(_WIN32) && defined(HIKARI_GAME_PROJECT_SDK_EXPORTS)
#define HIKARI_GAME_PROJECT_SDK_API __declspec(dllexport)
#elif defined(_WIN32) && defined(HIKARI_GAME_PROJECT_SDK_IMPORTS)
#define HIKARI_GAME_PROJECT_SDK_API __declspec(dllimport)
#else
#define HIKARI_GAME_PROJECT_SDK_API
#endif

namespace HIKARI::SDK {

// 配布物の名前としての版。配布の区切りで上げる。互換の判定には使わない：公開ヘッダーを変えても上げなくてよい。
// モジュールの DLL が今の引擎と合うかは、DLL の隣の界面の記録（編訳の時に読んだ SDK のファイルの指紋）を引擎が
// 自分の表と比べて決める。
inline constexpr unsigned int kVersionMajor = 4u;
inline constexpr unsigned int kVersionMinor = 21u;
inline constexpr unsigned int kVersionPatch = 0u;

} // namespace HIKARI::SDK
