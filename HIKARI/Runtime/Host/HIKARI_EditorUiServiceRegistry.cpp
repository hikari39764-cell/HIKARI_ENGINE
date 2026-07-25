#include "Runtime/Host/HIKARI_IEditorUiServices.h"

namespace HIKARI::RUNTIME {

    namespace {
        IEditorUiServices* gEditorUiServices = nullptr;
    }

    bool RegisterEditorUiServices(
        IEditorUiServices& services) noexcept {

        if (gEditorUiServices != nullptr &&
            gEditorUiServices != &services) {
            return false;
        }
        gEditorUiServices = &services;
        return true;
    }

    void UnregisterEditorUiServices(
        IEditorUiServices& services) noexcept {

        if (gEditorUiServices == &services) {
            gEditorUiServices = nullptr;
        }
    }

    IEditorUiServices* GetEditorUiServices() noexcept {
        return gEditorUiServices;
    }

} // namespace HIKARI::RUNTIME
