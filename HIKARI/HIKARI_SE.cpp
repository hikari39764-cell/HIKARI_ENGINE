#include "Audio/HIKARI_Audio.h"
#include "HIKARI_SE.h"
#include <unordered_set>

namespace HIKARI {
    namespace SE {

        static std::vector<Entry> gEntries;
        static std::unordered_map<std::string, int> gNameToId;
        // ループ再生と「一度だけ再生」の管理マップ
        static std::unordered_map<std::string, int> gLoopVoices;   // name -> voiceId（ループ用の声ID）
        static std::unordered_map<std::string, int> gOnceVoices;   // name -> last voiceId（再生中ならそのIDを使い回す）

        static inline void EnsureLoaded(int id) {
            if (id < 0 || id >= static_cast<int>(gEntries.size())) return;
            if (gEntries[id].handle < 0) { gEntries[id].handle = HIKARI::AUDIO::Load(gEntries[id].path.c_str()); }
        }

        void Register(const std::string& name, const std::string& path, const std::string& group) {
            if (gNameToId.find(name) != gNameToId.end()) return;
            Entry e; e.name = name; e.path = path; e.group = group; e.handle = -1;
            int id = static_cast<int>(gEntries.size());
            gEntries.push_back(e);
            gNameToId[name] = id;
        }

        bool LoadGroup(const std::string& group) {
            bool any = false;
            for (auto& e : gEntries) { if (e.group == group && e.handle < 0) { e.handle = HIKARI::AUDIO::Load(e.path.c_str()); any = true; } }
            return any;
        }

        bool LoadAll() {
            bool any = false;
            for (auto& e : gEntries) { if (e.handle < 0) { e.handle = HIKARI::AUDIO::Load(e.path.c_str()); any = true; } }
            return any;
        }

        int Play(const std::string& name, float volume) {
            auto it = gNameToId.find(name); if (it == gNameToId.end()) return -1;
            int id = it->second; EnsureLoaded(id);
            if (gEntries[id].handle < 0) return -1;
            return HIKARI::AUDIO::Play(gEntries[id].handle, false, volume);
        }

        int PlayOnce(const std::string& name, float volume) {
            auto it = gNameToId.find(name); if (it == gNameToId.end()) return -1;
            int id = it->second; EnsureLoaded(id);
            if (gEntries[id].handle < 0) return -1;

            int prev = -1;
            auto it2 = gOnceVoices.find(name);
            if (it2 != gOnceVoices.end()) prev = it2->second;
            if (prev != -1 && HIKARI::AUDIO::IsPlaying(prev)) {
                return prev; // まだ再生中の場合：新しく重ねずに既存の再生IDを返す
            }
            int voice = HIKARI::AUDIO::Play(gEntries[id].handle, false, volume);
            gOnceVoices[name] = voice;
            return voice;
        }

        void Stop(int voiceId) { if (voiceId != -1) HIKARI::AUDIO::Stop(voiceId); }

        void PlayLoop(const std::string& name, float volume) {
            auto it = gLoopVoices.find(name);
            if (it != gLoopVoices.end()) {
                int v = it->second; if (v != -1 && HIKARI::AUDIO::IsPlaying(v)) return; // すでにループ再生中なら何もしない
            }
            auto ix = gNameToId.find(name); if (ix == gNameToId.end()) return;
            int id = ix->second; EnsureLoaded(id); if (gEntries[id].handle < 0) return;
            int vId = HIKARI::AUDIO::Play(gEntries[id].handle, true, volume);
            gLoopVoices[name] = vId;
        }

        void StopLoop(const std::string& name) {
            auto it = gLoopVoices.find(name);
            if (it == gLoopVoices.end()) return;
            if (it->second != -1) { HIKARI::AUDIO::Stop(it->second); }
            gLoopVoices.erase(it);
        }

        bool IsPlayingName(const std::string& name) {
            // PlayOnceやLoop再生のいずれかで再生中かを判定
            auto it = gOnceVoices.find(name);
            if (it != gOnceVoices.end() && it->second != -1 && HIKARI::AUDIO::IsPlaying(it->second)) return true;
            auto it2 = gLoopVoices.find(name);
            if (it2 != gLoopVoices.end() && it2->second != -1 && HIKARI::AUDIO::IsPlaying(it2->second)) return true;
            return false;
        }

        void StopAll() {
            for (auto& kv : gLoopVoices) { if (kv.second != -1) HIKARI::AUDIO::Stop(kv.second); }
            gLoopVoices.clear();
            for (auto& kv : gOnceVoices) { if (kv.second != -1) HIKARI::AUDIO::Stop(kv.second); }
            gOnceVoices.clear();
        }

        void StopGroup(const std::string& group) {
            // 指定したグループに属する音声をすべて停止する
            for (auto& kv : gLoopVoices) {
                const std::string& nm = kv.first; int voice = kv.second;
                auto it = gNameToId.find(nm); if (it == gNameToId.end()) continue;
                int id = it->second; if (gEntries[id].group == group && voice != -1) { HIKARI::AUDIO::Stop(voice); kv.second = -1; }
            }
            for (auto& kv : gOnceVoices) {
                const std::string& nm = kv.first; int voice = kv.second;
                auto it = gNameToId.find(nm); if (it == gNameToId.end()) continue;
                int id = it->second; if (gEntries[id].group == group && voice != -1) { HIKARI::AUDIO::Stop(voice); kv.second = -1; }
            }
        }

    }
} // namespace HIKARI::SE
