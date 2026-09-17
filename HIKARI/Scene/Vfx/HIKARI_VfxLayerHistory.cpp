#include "Scene/Vfx/HIKARI_VfxLayerHistory.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::VFX::PLAYBACK {
    using namespace RENDER3D::PARTICLES;

    void LayerHistory::Project(const VfxHistoryData& settings, float time,
        ParticleEmitterSubmission& submission) {
        if (settings.mode == VfxHistoryMode::None) return;
        const auto current = CurrentShapePose(submission);
        const float interval = settings.duration / float(settings.samples - 1);
        if (!samples_.empty() && (time < samples_.back().time ||
            MATH::Length(current.position - samples_.back().pose.position) > settings.breakDistance)) Clear();
        std::erase_if(samples_, [&](const auto& sample) { return time - sample.time > settings.duration; });
        if (submission.overrides.enabled) {
            if (samples_.empty() || time - samples_.back().time >= interval * 0.99f) {
                if (samples_.size() >= settings.samples) samples_.erase(samples_.begin());
                samples_.push_back({time, current});
            }
        }
        // 最新姿勢は毎フレーム更新。履歴のサンプリング間隔と描画速度を分離する。
        submission.shapePoses.reserve(settings.samples);
        const auto fade = [&](const Sample& sample) {
            auto pose = sample.pose;
            const float t = std::clamp(1.0f - (time-sample.time) / settings.duration, 0.0f, 1.0f);
            pose.color.w *= t*t*(3-2*t);
            return pose;
        };
        if (settings.mode == VfxHistoryMode::Afterimage) {
            for (const auto& sample : samples_) {
                if (submission.shapePoses.size() + 1 >= settings.samples || time-sample.time < interval*0.5f) continue;
                submission.shapePoses.push_back(fade(sample));
            }
            if (submission.overrides.enabled) submission.shapePoses.push_back(current);
        } else {
            submission.render.shape = ParticleRenderShape::HistoryRibbon;
            const auto edge = [&](const ParticleShapePose& pose, float side) {
                const auto matrix = MATH::Mat4::TRS(pose.position, pose.rotation, pose.scale);
                const auto point = matrix.TransformPoint({side * submission.render.ribbonWidth * 0.5f, 0, 0, 1});
                return MATH::Vec3{point.x,point.y,point.z};
            };
            for (size_t i = 0; i < samples_.size(); ++i) {
                const auto from = fade(samples_[i]);
                const auto to = i+1 < samples_.size() ? fade(samples_[i+1]) : current;
                if (i+1 == samples_.size() && !submission.overrides.enabled) break;
                const auto a = edge(from, -1), b = edge(from, 1), c = edge(to, 1), d = edge(to, -1);
                if (MATH::Length(a-d)+MATH::Length(b-c) < 0.00001f) continue;
                auto segment = to;
                segment.corners = {a,b,c,d};
                segment.position = (a+b+c+d)*0.25f;
                float radius = 0;
                for (const auto& p : segment.corners) radius = (std::max)(radius, MATH::Length(p-segment.position));
                segment.scale = {radius*2,0,0}; // AS の球境界は全四隅を包含する。
                segment.color.w = 1;
                segment.edgeAlpha = {from.color.w,to.color.w};
                segment.uvRange = {1.0f - (time-samples_[i].time)/settings.duration,
                    i+1 < samples_.size() ? 1.0f-(time-samples_[i+1].time)/settings.duration : 1.0f};
                submission.shapePoses.push_back(segment);
                if (submission.shapePoses.size() >= settings.samples) break;
            }
        }
        submission.overrides.enabled = submission.shapePoses.empty() ? 0u : 1u;
    }
}
