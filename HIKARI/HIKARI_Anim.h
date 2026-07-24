#pragma once
#include <functional>
#include <cmath>   
#include "Render2D/HIKARI_Transform2D.h"
namespace HIKARI {
    namespace ANIM {

        // -------- イージング --------
        namespace EASE {
            using EaseFn = float(*)(float);
            float Linear(float t);
            float InQuad(float t);
            float OutQuad(float t);
            float InOutQuad(float t);
            float InCubic(float t);
            float OutCubic(float t);
            float InOutCubic(float t);
            float OutBack(float t); 
            float InBack(float t);
            float InOutBack(float t);
        }

        // -------- 基本要素 --------
        struct TweenFloat {
            float from{ 0.0f };
            float to{ 0.0f };
            float time{ 0.0f };
            float duration{ 0.0f };
            EASE::EaseFn ease{ EASE::Linear };
            bool  playing{ false };

            void Play(float f, float t, float d, EASE::EaseFn e = EASE::Linear) {
                from = f; to = t; duration = (d < 0.0f ? 0.0f : d); time = 0.0f; ease = e; playing = true;
            }
            bool Update(float dt, float& outValue) {
                if (!playing) return false;
                time += (dt < 0.0f ? 0.0f : dt);
                float u = duration > 0.0f ? (time / duration) : 1.0f;
                if (u >= 1.0f) { u = 1.0f; playing = false; }
                float k = ease(u);
                outValue = from + (to - from) * k;
                return true;
            }
            bool IsPlaying() const { return playing; }
        };

        struct TweenVec2 {
            TweenFloat x, y;
            void Play(Vector2 f, Vector2 t, float d, EASE::EaseFn e = EASE::Linear) {
                x.Play(f.x, t.x, d, e); y.Play(f.y, t.y, d, e);
            }
            bool Update(float dt, Vector2& out) {
                bool ax = x.Update(dt, out.x);
                bool ay = y.Update(dt, out.y);
                return ax || ay;
            }
            bool IsPlaying() const { return x.IsPlaying() || y.IsPlaying(); }
        };

        // -------- シェイク アニメータ --------
        struct Shake2D {
            float time{ 0.0f };
            float duration{ 0.0f };

            // 振幅
            float ampX{ 0.0f };
            float ampY{ 0.0f };
            float ampRot{ 0.0f };

            // 周波数(Hz)
            float freqX{ 0.0f };
            float freqY{ 0.0f };
            float freqRot{ 0.0f };

            // 位相
            float phaseX{ 0.0f };
            float phaseY{ 0.0f };
            float phaseRot{ 0.0f };

            EASE::EaseFn envelope{ nullptr };

            bool active{ false };

            void Stop() {
                active = false;
                time = duration = 0.0f;
                ampX = ampY = ampRot = 0.0f;
                freqX = freqY = freqRot = 0.0f;
                phaseX = phaseY = phaseRot = 0.0f;
                envelope = nullptr;
            }


            void Play(float amplitude, float durationSec) {
                PlayEx(
                    amplitude,
                    amplitude * 0.8f,
                    amplitude * 0.0f,
                    12.0f, 9.0f, 0.0f,
                    durationSec,
                    nullptr
                );
            }

            void PlayEx(
                float ax, float ay, float aRot,
                float fx, float fy, float fRot,
                float durationSec,
                EASE::EaseFn env = nullptr
            ) {
                if (durationSec <= 0.0f || (ax == 0.0f && ay == 0.0f && aRot == 0.0f)) {
                    Stop();
                    return;
                }
                active = true;
                time = 0.0f;
                duration = durationSec;

                ampX = ax; ampY = ay; ampRot = aRot;
                freqX = fx; freqY = fy; freqRot = fRot;

                float seed = duration + ax * 0.37f + ay * 0.73f + aRot * 1.11f;
                phaseX = seed * 1.234567f;
                phaseY = seed * 3.456789f;
                phaseRot = seed * 7.891011f;

                envelope = env;
            }

            bool IsPlaying() const {
                return active;
            }

            void Update(float dt, Vector2& pos, float& rot) {
                if (!active) return;
                if (dt < 0.0f) dt = 0.0f;

                time += dt;
                float u = (duration > 0.0f) ? (time / duration) : 1.0f;
                if (u >= 1.0f) {
                    Stop();
                    return;
                }

                float tEnv = 1.0f - u;
                float envK = envelope ? envelope(tEnv) : tEnv;

                const float twoPi = 6.2831853f;
                phaseX += freqX * dt * twoPi;
                phaseY += freqY * dt * twoPi;
                phaseRot += freqRot * dt * twoPi;

                pos.x += ampX * envK * std::sin(phaseX);
                pos.y += ampY * envK * std::cos(phaseY);
                rot += ampRot * envK * std::sin(phaseRot);
            }
        };


        // -------- Transform アニメータ --------
        //複数の Play* を同時に実行可。
        struct TransformAnimator {
            Transform2D* target{ nullptr };
            TweenVec2 move;
            TweenVec2 scale;
            TweenFloat rotate;
            Shake2D shake;

            explicit TransformAnimator(Transform2D* t = nullptr) : target(t) {}
            void Bind(Transform2D* t) { target = t; }

            void PlayMoveTo(Vector2 to, float seconds, EASE::EaseFn ease = EASE::OutCubic) {
                if (!target) return; move.Play(target->position, to, seconds, ease);
            }
            void PlayScaleTo(Vector2 to, float seconds, EASE::EaseFn ease = EASE::OutBack) {
                if (!target) return; scale.Play(target->scale, to, seconds, ease);
            }
            void PlayRotateTo(float to, float seconds, EASE::EaseFn ease = EASE::InOutCubic) {
                if (!target) return; rotate.Play(target->rotation, to, seconds, ease);
            }

            // ---- シェイク制御 ----
            void PlayShake(float amplitude, float seconds) {
                shake.Play(amplitude, seconds);
            }
            void PlayShakeEx(
                float ax, float ay, float aRot,
                float fx, float fy, float fRot,
                float seconds,
                EASE::EaseFn envelope = nullptr
            ) {
                shake.PlayEx(ax, ay, aRot, fx, fy, fRot, seconds, envelope);
            }
            void StopShake() {
                shake.Stop();
            };

            // 更新：計算結果を Transform2D に書き戻す
            void Update(float dt) {
                if (!target) return;
                Vector2 p = target->position;
                Vector2 s = target->scale;
                float   r = target->rotation;
                if (move.IsPlaying()) { move.Update(dt, p); }
                if (scale.IsPlaying()) { scale.Update(dt, s); }
                if (rotate.IsPlaying()) { rotate.Update(dt, r); }
                if (shake.IsPlaying()) {
                    shake.Update(dt, p, r);
                }
                target->position = p; target->scale = s; target->rotation = r;
            }

            bool Busy() const { return (move.IsPlaying() || scale.IsPlaying() || rotate.IsPlaying()); }
        };

        // -------- シンプルなシーケンス--------
        // 複数のラムダをディレイ付きで連結。カットシーンに便利。
        struct Step {
            float delay{ 0.0f };                      // 呼び出し前の待機時間
            std::function<void()> call;             // 実行する処理
        };

        struct Sequence {
            std::vector<Step> steps;
            size_t index{ 0 };
            float  timer{ 0.0f };
            bool   playing{ false };

            void Clear() { steps.clear(); index = 0; timer = 0.0f; playing = false; }
            void Add(float waitSec, std::function<void()> fn) { steps.push_back({ waitSec, std::move(fn) }); }
            void Play() { index = 0; timer = 0.0f; playing = true; }

            void Update(float dt) {
                if (!playing || index >= steps.size()) return;
                timer += (dt < 0.0f ? 0.0f : dt);
                if (timer >= steps[index].delay) {
                    if (steps[index].call) steps[index].call();
                    timer = 0.0f; ++index;
                    if (index >= steps.size()) playing = false;
                }
            }

            bool Finished() const { return !playing; }
        };

    }
} // namespace HIKARI::ANIM
