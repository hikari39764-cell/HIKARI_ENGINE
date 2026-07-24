#include "Render3D/GpuDriven/HIKARI_GpuDrivenFrame.h"

#include <algorithm>

namespace HIKARI::RENDER3D::GPUDRIVEN {
	// GpuDrivenPassKindの値をインデックスに変換する関数です。
    size_t ToPassIndex(GpuDrivenPassKind passKind) {
        const size_t index = static_cast<size_t>(passKind);
        return index < kGpuDrivenPassCount ? index : 0u;
    }

    bool GpuSceneRange::IsValid() const {
        return instanceCount != 0u;
    }

    void GpuDrivenPassFrame::Reset() {
        instances = nullptr;
        gpuSceneRange = {};
        preferredBackend = GpuDrivenBackendKind::MeshShader;
        clusterEligible = false;
    }

    bool GpuDrivenPassFrame::HasSource() const {
        return gpuSceneRange.IsValid();
    }

    void GpuDrivenFrame::Reset() {
        for (GpuDrivenPassFrame& pass : passes) {
            pass.Reset();
        }
    }

    GpuDrivenPassFrame& GpuDrivenFrame::GetPass(GpuDrivenPassKind passKind) {
        return passes[ToPassIndex(passKind)];
    }

    const GpuDrivenPassFrame& GpuDrivenFrame::GetPass(GpuDrivenPassKind passKind) const {
        return passes[ToPassIndex(passKind)];
    }

    size_t GpuDrivenFrame::CountActivePasses() const {
        size_t count = 0;
        for (const GpuDrivenPassFrame& pass : passes) {
            if (pass.HasSource()) {
                ++count;
            }
        }
        return count;
    }
	// GpuDrivenFrame内のパスの中で、クラスタリングが可能なパスの数をカウントして返します。
    size_t GpuDrivenFrame::CountClusterEligiblePasses() const {
        size_t count = 0;
        for (const GpuDrivenPassFrame& pass : passes) {
            if (pass.HasSource() && pass.clusterEligible) {
                ++count;
            }
        }
        return count;
    }
	// GPUドライブフレーム内のソースを持つインスタンスの総数をカウントする関数です。
    size_t GpuDrivenFrame::CountSourceInstances() const {
        size_t count = 0;
        for (const GpuDrivenPassFrame& pass : passes) {
            if (pass.HasSource()) {
                count += pass.gpuSceneRange.instanceCount;
            }
        }
        return count;
    }
	// GPUドライブフレーム内のクラスタリング可能なインスタンスの総数をカウントする関数です。
    size_t GpuDrivenFrame::CountClusterEligibleInstances() const {
        size_t count = 0;
        for (const GpuDrivenPassFrame& pass : passes) {
            if (pass.HasSource() && pass.clusterEligible) {
                count += pass.gpuSceneRange.instanceCount;
            }
        }
        return count;
    }
	// GpuDrivenFrameBuildInput構造体を使用してGpuDrivenFrameを構築する関数です。
    GpuDrivenFrame BuildGpuDrivenFrame(const GpuDrivenFrameBuildInput& input) {
        GpuDrivenFrame frame{};
        frame.Reset();

        for (size_t passIndex = 0; passIndex < kGpuDrivenPassCount; ++passIndex) {
            const GpuDrivenPassBuildInput& source = input.passes[passIndex];
            uint32_t instanceCount = source.gpuSceneInstanceCount;
            if (instanceCount == 0u && source.instances != nullptr) {
                instanceCount =
                    static_cast<uint32_t>(
                        (std::min)(
                            source.instances->size(),
                            static_cast<size_t>(UINT32_MAX)));
            }
            if (instanceCount == 0u) {
                continue;
            }

            GpuDrivenPassFrame& pass = frame.passes[passIndex];
            pass.instances = source.instances;
            pass.gpuSceneRange.baseIndex = source.gpuSceneBaseIndex;
            pass.gpuSceneRange.instanceCount = instanceCount;
            pass.gpuSceneRange.passKind =
                static_cast<GpuDrivenPassKind>(passIndex);
            pass.preferredBackend = source.preferredBackend;
            pass.clusterEligible = source.clusterEligible;
        }
        return frame;
    }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
