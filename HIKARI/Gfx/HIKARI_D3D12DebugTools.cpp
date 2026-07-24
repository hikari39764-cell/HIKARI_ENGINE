#include "HIKARI_D3D12DebugTools.h"

#include <d3d12sdklayers.h>
#include <dxgi1_3.h>
#include <dxgidebug.h>
#include <wrl/client.h>
#include <sstream>
#include <vector>
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_GfxDebugConfig.h"

namespace HIKARI::GFX {
    using Microsoft::WRL::ComPtr;

    namespace {
		// D3D12_MESSAGE_SEVERITY を文字列に変換する
        const char* ToString(D3D12_MESSAGE_SEVERITY severity) {
            switch (severity) {
            case D3D12_MESSAGE_SEVERITY_CORRUPTION: return "CORRUPTION";
            case D3D12_MESSAGE_SEVERITY_ERROR: return "ERROR";
            case D3D12_MESSAGE_SEVERITY_WARNING: return "WARNING";
            case D3D12_MESSAGE_SEVERITY_INFO: return "INFO";
            case D3D12_MESSAGE_SEVERITY_MESSAGE: return "MESSAGE";
            default: return "UNKNOWN";
            }
        }
		// D3D12_MESSAGE_CATEGORY を文字列に変換する
        const char* ToString(D3D12_MESSAGE_CATEGORY category) {
            switch (category) {
            case D3D12_MESSAGE_CATEGORY_APPLICATION_DEFINED: return "APPLICATION_DEFINED";
            case D3D12_MESSAGE_CATEGORY_MISCELLANEOUS: return "MISCELLANEOUS";
            case D3D12_MESSAGE_CATEGORY_INITIALIZATION: return "INITIALIZATION";
            case D3D12_MESSAGE_CATEGORY_CLEANUP: return "CLEANUP";
            case D3D12_MESSAGE_CATEGORY_COMPILATION: return "COMPILATION";
            case D3D12_MESSAGE_CATEGORY_STATE_CREATION: return "STATE_CREATION";
            case D3D12_MESSAGE_CATEGORY_STATE_SETTING: return "STATE_SETTING";
            case D3D12_MESSAGE_CATEGORY_STATE_GETTING: return "STATE_GETTING";
            case D3D12_MESSAGE_CATEGORY_RESOURCE_MANIPULATION: return "RESOURCE_MANIPULATION";
            case D3D12_MESSAGE_CATEGORY_EXECUTION: return "EXECUTION";
            case D3D12_MESSAGE_CATEGORY_SHADER: return "SHADER";
            default: return "UNKNOWN";
            }
        }

        const char* ToString(DXGI_INFO_QUEUE_MESSAGE_SEVERITY severity) {
            switch (severity) {
            case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION: return "CORRUPTION";
            case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR: return "ERROR";
            case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_WARNING: return "WARNING";
            case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_INFO: return "INFO";
            case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_MESSAGE: return "MESSAGE";
            default: return "UNKNOWN";
            }
        }

        const char* ToString(DXGI_INFO_QUEUE_MESSAGE_CATEGORY category) {
            switch (category) {
            case DXGI_INFO_QUEUE_MESSAGE_CATEGORY_UNKNOWN: return "UNKNOWN";
            case DXGI_INFO_QUEUE_MESSAGE_CATEGORY_MISCELLANEOUS: return "MISCELLANEOUS";
            case DXGI_INFO_QUEUE_MESSAGE_CATEGORY_INITIALIZATION: return "INITIALIZATION";
            case DXGI_INFO_QUEUE_MESSAGE_CATEGORY_CLEANUP: return "CLEANUP";
            case DXGI_INFO_QUEUE_MESSAGE_CATEGORY_COMPILATION: return "COMPILATION";
            case DXGI_INFO_QUEUE_MESSAGE_CATEGORY_STATE_CREATION: return "STATE_CREATION";
            case DXGI_INFO_QUEUE_MESSAGE_CATEGORY_STATE_SETTING: return "STATE_SETTING";
            case DXGI_INFO_QUEUE_MESSAGE_CATEGORY_STATE_GETTING: return "STATE_GETTING";
            case DXGI_INFO_QUEUE_MESSAGE_CATEGORY_RESOURCE_MANIPULATION: return "RESOURCE_MANIPULATION";
            case DXGI_INFO_QUEUE_MESSAGE_CATEGORY_EXECUTION: return "EXECUTION";
            case DXGI_INFO_QUEUE_MESSAGE_CATEGORY_SHADER: return "SHADER";
            default: return "UNKNOWN";
            }
        }
    }
	// D3D12 の InfoQueue を設定する
    // device には ID3D12Device のポインタを指定する
    // _DEBUG が定義されている場合は、InfoQueue を取得して、エラーと警告でブレークするように設定する。_DEBUG が定義されていない場合は、何もしない
    void ConfigureD3D12InfoQueue(ID3D12Device* device) {
        if (!device) {
            return;
        }

        ComPtr<ID3D12InfoQueue> infoQueue;
        if (FAILED(device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
            return;
        }

        const GfxDebugConfig& config = GetGfxDebugConfig();
        if (config.enableInfoQueueBreakOnError) {
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
        }
        infoQueue->SetBreakOnSeverity(
            D3D12_MESSAGE_SEVERITY_WARNING,
            config.enableInfoQueueBreakOnWarning ? TRUE : FALSE);
    }
	// D3D12 の InfoQueue をダンプする
    void DumpD3D12InfoQueue(ID3D12Device* device, const char* reason) {
        if (!device) {
            return;
        }

        ComPtr<ID3D12InfoQueue> infoQueue;
        if (FAILED(device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
            return;
        }

        const UINT64 count = infoQueue->GetNumStoredMessages();
        {
            std::ostringstream oss;
            oss << "[D3D12InfoQueue] Dump reason=" << (reason ? reason : "")
                << " count=" << count;
            DEBUGLOG::PushRenderError(oss.str());
        }

        for (UINT64 i = 0; i < count; ++i) {
            SIZE_T messageLength = 0;
            infoQueue->GetMessage(i, nullptr, &messageLength);
            if (messageLength == 0) {
                continue;
            }

            std::vector<char> bytes(messageLength);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
            if (SUCCEEDED(infoQueue->GetMessage(i, message, &messageLength))) {
                std::ostringstream oss;
                oss << "[D3D12][" << ToString(message->Severity) << "]["
                    << ToString(message->Category) << "][" << message->ID << "] "
                    << (message->pDescription ? message->pDescription : "");
                DEBUGLOG::PushRenderError(oss.str());
            }
        }
    }
	// D3D12 の InfoQueue をクリアする
    void ClearD3D12InfoQueue(ID3D12Device* device) {
        if (!device) {
            return;
        }
        ComPtr<ID3D12InfoQueue> infoQueue;
        if (SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&infoQueue)))) {
            infoQueue->ClearStoredMessages();
        }
    }

    void DumpDxgiInfoQueue(const char* reason) {
        ComPtr<IDXGIInfoQueue> infoQueue;
        if (FAILED(DXGIGetDebugInterface1(0u, IID_PPV_ARGS(&infoQueue)))) {
            return;
        }

        const UINT64 count = infoQueue->GetNumStoredMessages(DXGI_DEBUG_ALL);
        {
            std::ostringstream oss;
            oss << "[DXGIInfoQueue] Dump reason=" << (reason ? reason : "")
                << " count=" << count;
            DEBUGLOG::PushRenderError(oss.str());
        }

        for (UINT64 i = 0; i < count; ++i) {
            SIZE_T messageLength = 0;
            infoQueue->GetMessage(DXGI_DEBUG_ALL, i, nullptr, &messageLength);
            if (messageLength == 0) {
                continue;
            }

            std::vector<char> bytes(messageLength);
            auto* message =
                reinterpret_cast<DXGI_INFO_QUEUE_MESSAGE*>(bytes.data());
            if (SUCCEEDED(infoQueue->GetMessage(
                    DXGI_DEBUG_ALL,
                    i,
                    message,
                    &messageLength))) {
                std::ostringstream oss;
                oss << "[DXGI][" << ToString(message->Severity) << "]["
                    << ToString(message->Category) << "][" << message->ID << "] "
                    << (message->pDescription ? message->pDescription : "");
                DEBUGLOG::PushRenderError(oss.str());
            }
        }
    }

    void ClearDxgiInfoQueue() {
        ComPtr<IDXGIInfoQueue> infoQueue;
        if (SUCCEEDED(DXGIGetDebugInterface1(0u, IID_PPV_ARGS(&infoQueue)))) {
            infoQueue->ClearStoredMessages(DXGI_DEBUG_ALL);
        }
    }
}
