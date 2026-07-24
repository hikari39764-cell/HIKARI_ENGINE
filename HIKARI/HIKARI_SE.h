#pragma once
#include <string>
#include <vector>
#include <unordered_map>

namespace HIKARI {
    namespace SE {

        struct Entry {
            std::string name;
            std::string path;
            std::string group{ "default" };
            int handle{ -1 };
        };

        // --- 登録 / 読み込み ---
        void Register(const std::string& name, const std::string& path, const std::string& group = "default");
        bool LoadGroup(const std::string& group);
        bool LoadAll();

        // --- 再生（多重再生対応）---
        int Play(const std::string& name, float volume = 1.0f);             // ワンショット再生
        int PlayOnce(const std::string& name, float volume = 1.0f);         // 重複再生を禁止：再生中なら既存の音を返す
        void Stop(int voiceId);

        // ループ再生
        void PlayLoop(const std::string& name, float volume = 1.0f);
        void StopLoop(const std::string& name);


        bool IsPlayingName(const std::string& name);
        void StopAll();
        void StopGroup(const std::string& group);

    }
} // namespace HIKARI::SE
