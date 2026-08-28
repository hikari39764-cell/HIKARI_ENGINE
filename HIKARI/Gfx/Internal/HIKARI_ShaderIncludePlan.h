#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <Windows.h>
#include <dxcapi.h>
#include <wrl/client.h>

#include "Gfx/Internal/HIKARI_ShaderCookFileSystem.h"

namespace HIKARI::GFX::INTERNAL {

    struct ShaderIncludePlan {
        std::unordered_map<
            std::string,
            std::shared_ptr<const ShaderCookFile>> aliases{};
    };

    bool BuildShaderIncludePlan(
        const ShaderCookFile& source,
        const ShaderCookRoot& sourceRoot,
        const std::vector<ShaderCookRoot>& includeRoots,
        std::shared_ptr<const ShaderIncludePlan>& outPlan,
        std::string& outError);

    class TrackingDxcIncludeHandler final : public IDxcIncludeHandler {
    public:
        TrackingDxcIncludeHandler(
            IDxcUtils* utils,
            std::shared_ptr<const ShaderIncludePlan> plan);

        HRESULT STDMETHODCALLTYPE QueryInterface(
            REFIID interfaceId,
            void** outObject) override;
        ULONG STDMETHODCALLTYPE AddRef() override;
        ULONG STDMETHODCALLTYPE Release() override;
        HRESULT STDMETHODCALLTYPE LoadSource(
            LPCWSTR filename,
            IDxcBlob** outSource) override;

        const std::vector<ShaderCookDependency>& Dependencies() const
            noexcept;
        const std::string& LastError() const noexcept;

    private:
        ~TrackingDxcIncludeHandler() = default;

        bool RecordDependency(
            const ShaderCookDependency& dependency,
            std::string& outError);

        std::atomic_ulong references_{ 1u };
        Microsoft::WRL::ComPtr<IDxcUtils> utils_{};
        std::shared_ptr<const ShaderIncludePlan> plan_{};
        std::vector<ShaderCookDependency> dependencies_{};
        std::string lastError_{};
    };

} // namespace HIKARI::GFX::INTERNAL
