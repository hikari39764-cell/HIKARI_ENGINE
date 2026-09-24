#include "Scene/Vfx/HIKARI_VfxLayerHistory.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::VFX::PLAYBACK {
using namespace RENDER3D::PARTICLES;

namespace {
ParticleShapePose Interpolate(const ParticleShapePose& a, const ParticleShapePose& b, float t) {
    auto pose = b;
    pose.position = a.position + (b.position - a.position) * t;
    pose.scale = a.scale + (b.scale - a.scale) * t;
    pose.color = {a.color.x + (b.color.x - a.color.x) * t, a.color.y + (b.color.y - a.color.y) * t,
                  a.color.z + (b.color.z - a.color.z) * t, a.color.w + (b.color.w - a.color.w) * t};
    const float dot = a.rotation.x * b.rotation.x + a.rotation.y * b.rotation.y + a.rotation.z * b.rotation.z +
                      a.rotation.w * b.rotation.w;
    const float sign = dot < 0 ? -1.0f : 1.0f;
    pose.rotation = MATH::NormalizeQ(
        {a.rotation.x * (1 - t) + b.rotation.x * t * sign, a.rotation.y * (1 - t) + b.rotation.y * t * sign,
         a.rotation.z * (1 - t) + b.rotation.z * t * sign, a.rotation.w * (1 - t) + b.rotation.w * t * sign});
    pose.age = a.age + (b.age - a.age) * t;
    pose.duration = a.duration + (b.duration - a.duration) * t;
    return pose;
}
} // namespace

void LayerHistory::Project(const VfxHistoryData& settings, float time, ParticleEmitterSubmission& submission) {
    if (settings.mode == VfxHistoryMode::None) {
        Clear();
        return;
    }
    const auto current = CurrentShapePose(submission);
    const float interval = settings.duration / float(settings.samples - 1);
    if (previous_ && (time < previous_->time || time - previous_->time > settings.duration ||
                      std::abs(interval - sampleInterval_) > 0.000001f ||
                      MATH::Length(current.position - previous_->pose.position) > settings.breakDistance))
        Clear();
    std::erase_if(samples_, [&](const auto& sample) { return time - sample.time > settings.duration + 0.000001f; });
    if (submission.overrides.enabled) {
        if (!previous_) {
            samples_.clear();
            samples_.push_back({time, current});
            nextSampleTime_ = time + interval;
            sampleInterval_ = interval;
        } else if (time > previous_->time) {
            // 固定間隔で姿勢を補間。フレーム境界に履歴サンプルを吸着させない。
            uint32_t generated = 0;
            while (nextSampleTime_ <= time + 0.000001f && generated++ < settings.samples) {
                const float t = std::clamp((nextSampleTime_ - previous_->time) / (time - previous_->time), 0.0f, 1.0f);
                if (samples_.size() >= settings.samples)
                    samples_.erase(samples_.begin());
                samples_.push_back({nextSampleTime_, Interpolate(previous_->pose, current, t)});
                nextSampleTime_ += interval;
            }
        }
        previous_ = Sample{time, current};
    } else
        previous_.reset();
    // 最新姿勢は毎フレーム更新。履歴のサンプリング間隔と描画速度を分離する。
    submission.shapePoses.reserve(settings.samples);
    const auto fade = [&](const Sample& sample) {
        auto pose = sample.pose;
        const float t = std::clamp(1.0f - (time - sample.time) / settings.duration, 0.0f, 1.0f);
        pose.scale = pose.scale * (settings.tailWidth + (1 - settings.tailWidth) * t);
        pose.color.x *= settings.tailColor.x + (1 - settings.tailColor.x) * t;
        pose.color.y *= settings.tailColor.y + (1 - settings.tailColor.y) * t;
        pose.color.z *= settings.tailColor.z + (1 - settings.tailColor.z) * t;
        pose.color.w *=
            std::pow(t * t * (3 - 2 * t), settings.fadePower) * (settings.tailColor.w + (1 - settings.tailColor.w) * t);
        return pose;
    };
    if (settings.mode == VfxHistoryMode::Afterimage) {
        for (const auto& sample : samples_) {
            if (submission.shapePoses.size() + 1 >= settings.samples || time - sample.time < interval * 0.5f)
                continue;
            submission.shapePoses.push_back(fade(sample));
        }
        if (submission.overrides.enabled)
            submission.shapePoses.push_back(current);
    } else {
        submission.render.shape = ParticleRenderShape::HistoryRibbon;
        const auto edge = [&](const ParticleShapePose& pose, float side) {
            const auto matrix = MATH::Mat4::TRS(pose.position, pose.rotation, pose.scale);
            const auto point = matrix.TransformPoint({side * submission.render.ribbonWidth * 0.5f, 0, 0, 1});
            return MATH::Vec3{point.x, point.y, point.z};
        };
        for (size_t i = 0; i < samples_.size(); ++i) {
            const auto from = fade(samples_[i]);
            const auto to = i + 1 < samples_.size() ? fade(samples_[i + 1]) : current;
            if (i + 1 == samples_.size() && !submission.overrides.enabled)
                break;
            const auto a = edge(from, -1), b = edge(from, 1), c = edge(to, 1), d = edge(to, -1);
            if (MATH::Length(a - d) + MATH::Length(b - c) < 0.00001f)
                continue;
            auto segment = to;
            segment.corners = {a, b, c, d};
            segment.position = (a + b + c + d) * 0.25f;
            float radius = 0;
            for (const auto& p : segment.corners)
                radius = (std::max)(radius, MATH::Length(p - segment.position));
            segment.scale = {radius * 2, 0, 0}; // AS の球境界は全四隅を包含する。
            segment.startColor = from.color;
            segment.color.w = 1;
            segment.edgeAlpha = {from.color.w, to.color.w};
            segment.uvRange = {1.0f - (time - samples_[i].time) / settings.duration,
                               i + 1 < samples_.size() ? 1.0f - (time - samples_[i + 1].time) / settings.duration
                                                       : 1.0f};
            submission.shapePoses.push_back(segment);
            if (submission.shapePoses.size() >= settings.samples)
                break;
        }
    }
    submission.overrides.enabled = submission.shapePoses.empty() ? 0u : 1u;
}
} // namespace HIKARI::VFX::PLAYBACK
