#include "Audio/HIKARI_Audio.h"
#include "HIKARI_Bgm.h"
#include <cmath>

namespace HIKARI {
    namespace BGM {

        static std::vector<Entry> gEntries;
        static std::unordered_map<std::string, int> gNameToId;
        static int gVoiceId = -1;     // 現在再生中のボイスID
        static int gCurrent = -1;     // 現在再生しているBGMの gEntries 上のインデックス

        // フェード状態管理
        enum class FadeMode { None, In, Out, Cross };
        static FadeMode gFadeMode = FadeMode::None;
        static float gFadeTime = 0.0f;
        static float gFadeDur = 0.0f;
        static float gFadeStartVol = 0.0f;
        static float gFadeTargetVol = 0.0f;
        // クロスフェード用
        static int   gCrossOldVoice = -1;
        static int   gCrossOldIndex = -1;
        static int   gCrossNextIndex = -1;
        static float gCrossNextTargetVol = 0.6f;

        static inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

        void Register(const std::string& name, const std::string& path, const std::string& group) {
            if (gNameToId.find(name) != gNameToId.end()) return;
            Entry e; e.name = name; e.path = path; e.group = group; e.handle = -1;
            int id = static_cast<int>(gEntries.size());
            gEntries.push_back(e);
            gNameToId[name] = id;
        }

        static inline void EnsureLoaded(int id) {
            if (id < 0 || id >= static_cast<int>(gEntries.size())) return;
            if (gEntries[id].handle < 0) { gEntries[id].handle = HIKARI::AUDIO::Load(gEntries[id].path.c_str()); }
        }

        bool LoadGroup(const std::string& group) {
            bool any = false;
            for (auto& e : gEntries) {
                if (e.group == group && e.handle < 0) { e.handle = HIKARI::AUDIO::Load(e.path.c_str()); any = true; }
            }
            return any;
        }

        bool LoadAll() {
            bool any = false;
            for (auto& e : gEntries) {
                if (e.handle < 0) { e.handle = HIKARI::AUDIO::Load(e.path.c_str()); any = true; }
            }
            return any;
        }

        void Play(const std::string& name, float volume, bool loop) {
            auto it = gNameToId.find(name); if (it == gNameToId.end()) return;
            int id = it->second; EnsureLoaded(id);
            if (gEntries[id].handle < 0) return;

            // 前のBGMが再生中なら停止
            if (gVoiceId != -1) { HIKARI::AUDIO::Stop(gVoiceId); gVoiceId = -1; }

            gVoiceId = HIKARI::AUDIO::Play(gEntries[id].handle, loop, volume);
            gCurrent = id;
            gFadeMode = FadeMode::None;
        }

        void Stop() { if (gVoiceId != -1) { HIKARI::AUDIO::Stop(gVoiceId); gVoiceId = -1; } gCurrent = -1; gFadeMode = FadeMode::None; }

        void SetVolume(float volume) { if (gVoiceId != -1) { HIKARI::AUDIO::SetVolume(gVoiceId, volume); } }

        void FadeIn(float seconds, float targetVolume) {
            if (gVoiceId == -1) return;
            gFadeMode = FadeMode::In; gFadeTime = 0.0f; gFadeDur = (seconds < 0.0f ? 0.0f : seconds);
            gFadeStartVol = 0.0f; gFadeTargetVol = targetVolume;
            HIKARI::AUDIO::SetVolume(gVoiceId, 0.0f);
        }

        void FadeOut(float seconds) {
            if (gVoiceId == -1) return;
            float curVol = 1.0f; // Novice は音量取得ができないため、近似的に 1.0 として扱う
            gFadeMode = FadeMode::Out; gFadeTime = 0.0f; gFadeDur = (seconds < 0.0f ? 0.0f : seconds);
            gFadeStartVol = curVol; gFadeTargetVol = 0.0f;
        }

        void CrossFade(const std::string& nextName, float seconds, float nextTargetVolume) {
            auto it = gNameToId.find(nextName); if (it == gNameToId.end()) return;
            int nextIdx = it->second; EnsureLoaded(nextIdx);
            if (gEntries[nextIdx].handle < 0) return;

            // 次の曲を音量0で再生開始
            int nextVoice = HIKARI::AUDIO::Play(gEntries[nextIdx].handle, true, 0.0f);

            // クロスフェード設定
            gFadeMode = FadeMode::Cross; gFadeTime = 0.0f; gFadeDur = (seconds < 0.0f ? 0.0f : seconds);
            gCrossOldVoice = gVoiceId; gCrossOldIndex = gCurrent;
            gVoiceId = nextVoice; gCurrent = nextIdx; gCrossNextIndex = nextIdx; gCrossNextTargetVol = nextTargetVolume;
        }

        void Update(float dt) {
            if (gFadeMode == FadeMode::None) return;
            if (dt < 0.0f) dt = 0.0f;
            gFadeTime += dt;
            float t = (gFadeDur > 0.0f) ? Clamp01(gFadeTime / gFadeDur) : 1.0f;

            switch (gFadeMode) {
            case FadeMode::In: {
                float v = gFadeStartVol + (gFadeTargetVol - gFadeStartVol) * t;
                if (gVoiceId != -1) HIKARI::AUDIO::SetVolume(gVoiceId, v);
                if (t >= 1.0f) gFadeMode = FadeMode::None;
                break;
            }
            case FadeMode::Out: {
                float v = gFadeStartVol + (gFadeTargetVol - gFadeStartVol) * t;
                if (gVoiceId != -1) HIKARI::AUDIO::SetVolume(gVoiceId, v);
                if (t >= 1.0f) { if (gVoiceId != -1) HIKARI::AUDIO::Stop(gVoiceId); gVoiceId = -1; gCurrent = -1; gFadeMode = FadeMode::None; }
                break;
            }
            case FadeMode::Cross: {
                // 古い曲をフェードアウトし、新しい曲をフェードイン
                float vNew = gCrossNextTargetVol * t;
                if (gVoiceId != -1) HIKARI::AUDIO::SetVolume(gVoiceId, vNew);
                float vOld = (1.0f - t);
                if (gCrossOldVoice != -1) HIKARI::AUDIO::SetVolume(gCrossOldVoice, vOld);
                if (t >= 1.0f) { if (gCrossOldVoice != -1) HIKARI::AUDIO::Stop(gCrossOldVoice); gCrossOldVoice = -1; gFadeMode = FadeMode::None; }
                break;
            }
            default: break;
            }
        }

        bool IsPlaying() { return (gVoiceId != -1) && HIKARI::AUDIO::IsPlaying(gVoiceId); }

        std::string Now() { return (gCurrent >= 0 && gCurrent < (int)gEntries.size()) ? gEntries[gCurrent].name : std::string(); }

    }
} // namespace HIKARI::BGM
