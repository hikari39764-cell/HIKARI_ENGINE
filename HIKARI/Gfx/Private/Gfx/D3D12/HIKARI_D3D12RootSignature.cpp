#include "Gfx/D3D12/HIKARI_D3D12RootSignature.h"
#include <wrl/client.h>
#include "Diagnostics/HIKARI_DebugLogBuffer.h"

namespace HIKARI::GFX {
HRESULT CreateRootSignature(ID3D12Device* device, const D3D12_ROOT_SIGNATURE_DESC& description,
                            ID3D12RootSignature** output) {
    if (!device || !output)
        return E_INVALIDARG;
    Microsoft::WRL::ComPtr<ID3DBlob> signature, errors;
    const HRESULT result = D3D12SerializeRootSignature(&description, D3D_ROOT_SIGNATURE_VERSION_1,
                                                       signature.GetAddressOf(), errors.GetAddressOf());
    if (FAILED(result)) {
        if (errors && errors->GetBufferPointer())
            DEBUGLOG::PushRenderError(static_cast<const char*>(errors->GetBufferPointer()));
        return result;
    }
    return device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
                                       IID_PPV_ARGS(output));
}
} // namespace HIKARI::GFX
