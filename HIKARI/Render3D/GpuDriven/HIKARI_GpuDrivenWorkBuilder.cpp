#include "Render3D/GpuDriven/HIKARI_GpuDrivenWorkBuilder.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    GpuDrivenWorkResult BuildGpuDrivenWork(
        const GpuDrivenWorkContext& context) {

        GpuDrivenWorkResult result{};
        if (context.frame != nullptr) {
            result.sourcePassCount =
                context.frame->CountActivePasses();
            result.sourceInstanceCount =
                context.frame->CountSourceInstances();
        }
        if (context.producer == nullptr) {
            return result;
        }

        GpuDrivenProducerWorkContext dispatchContext{};
        dispatchContext.commandList = context.commandList;
        dispatchContext.viewProj = context.viewProj;
        dispatchContext.cameraPosition = context.cameraPosition;
        dispatchContext.geometryPoolSrv = context.geometryPoolSrv;
        dispatchContext.surfaceGpuSceneGpuAddress =
            context.surfaceGpuSceneGpuAddress;
        dispatchContext.frame = context.frame;
        dispatchContext.passMask = context.passMask;
        dispatchContext.collectCounterReadback = context.collectCounterReadback;
        dispatchContext.depthOcclusion = context.depthOcclusion;

        const GpuDrivenProducerWorkResult dispatchResult =
            context.producer->DispatchWork(dispatchContext);
        result.submitted = dispatchResult.submitted;
        if (dispatchResult.sourcePassCount != 0) {
            result.sourcePassCount = dispatchResult.sourcePassCount;
        }
        if (dispatchResult.sourceInstanceCount != 0) {
            result.sourceInstanceCount = dispatchResult.sourceInstanceCount;
        }
        return result;
    }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
