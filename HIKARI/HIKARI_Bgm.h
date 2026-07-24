#pragma once
#include <string>
#include <vector>
#include <unordered_map>

namespace HIKARI {
    namespace BGM {

        struct Entry {
            std::string name;
            std::string path;
            std::string group{ "default" };
            int handle{ -1 };        // HIKARI::AUDIO の音源ハンドル
        };

        // --- 登録 / 読み込み ---
        void Register(const std::string& name, const std::string& path, const std::string& group = "default");
        bool LoadGroup(const std::string& group);
        bool LoadAll();

        // --- 再生（BGM は単一チャンネル）---
        void Play(const std::string& name, float volume = 0.6f, bool loop = true);
        void Stop();
        void SetVolume(float volume);

        // --- フェード / クロスフェード（毎フレーム Update を呼ぶこと）---
        void FadeIn(float seconds, float targetVolume = 0.6f);  // 現在再生中の曲をフェードイン
        void FadeOut(float seconds);// 現在再生中の曲をフェードアウトして停止
        void CrossFade(const std::string& nextName, float seconds, float nextTargetVolume = 0.6f);
        void Update(float deltaTime);

        bool IsPlaying();
        std::string Now();

    }
} // namespace HIKARI::BGM
