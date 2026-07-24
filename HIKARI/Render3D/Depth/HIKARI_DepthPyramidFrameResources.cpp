#include "Render3D/Depth/HIKARI_DepthPyramidFrameResources.h"

#include <utility>

namespace HIKARI::RENDER3D::DEPTH {

    namespace {
        DepthPyramidFrameResources gFrameResources{};
    }

    void DepthPyramidFrameResources::BeginFrame(uint32_t nextFrameIndex) {
        frameIndex = nextFrameIndex;
        publishCount = 0;
        currentValid = false;
        current = {};
    }

    bool DepthPyramidFrameResources::PublishCurrent(const DepthPyramidView& view) {
        if (!view.valid || view.pyramidSrv.ptr == 0 || view.width == 0 || view.height == 0) {
            currentValid = false;
            current = {};
            return false;
        }

        frameIndex = view.frameIndex;
        ++publishCount;
        currentValid = true;
        current = view;
        return true;
    }

    bool DepthPyramidFrameResources::PublishCurrent(
        DepthPyramidView view,
        DepthPyramidViewKind viewKind) {

        view.viewKind = viewKind;
        return PublishCurrent(view);
    }

    const DepthPyramidView* DepthPyramidFrameResources::TryGetCurrent() const {
        return currentValid && current.valid ? &current : nullptr;
    }

    const DepthPyramidView* DepthPyramidFrameResources::TryGetCurrent(
        DepthPyramidSourceKind requiredSource) const {

        const DepthPyramidView* view = TryGetCurrent();
        if (view == nullptr) {
            return nullptr;
        }
        return view->sourceKind == requiredSource ? view : nullptr;
    }

    const DepthPyramidView* DepthPyramidFrameResources::TryGetCurrent(
        DepthPyramidSourceKind requiredSource,
        DepthPyramidViewKind requiredViewKind) const {

        const DepthPyramidView* view = TryGetCurrent(requiredSource);
        if (view == nullptr) {
            return nullptr;
        }
        return view->viewKind == requiredViewKind ? view : nullptr;
    }

    void BeginDepthPyramidFrame(uint32_t frameIndex) {
        gFrameResources.BeginFrame(frameIndex);
    }

    bool PublishFrameDepthPyramid(const DepthPyramidView& view) {
        return gFrameResources.PublishCurrent(view);
    }

    bool PublishFrameDepthPyramid(
        DepthPyramidView view,
        DepthPyramidViewKind viewKind) {

        return gFrameResources.PublishCurrent(std::move(view), viewKind);
    }

    const DepthPyramidView* TryGetFrameDepthPyramidView() {
        return gFrameResources.TryGetCurrent();
    }

    const DepthPyramidView* TryGetFrameDepthPyramidView(DepthPyramidSourceKind requiredSource) {
        return gFrameResources.TryGetCurrent(requiredSource);
    }

    const DepthPyramidView* TryGetFrameDepthPyramidView(
        DepthPyramidSourceKind requiredSource,
        DepthPyramidViewKind requiredViewKind) {

        return gFrameResources.TryGetCurrent(requiredSource, requiredViewKind);
    }

} // namespace HIKARI::RENDER3D::DEPTH
