#include "Gfx/HIKARI_ResourceStateTracker.h"

#include "Core/HIKARI_Logger.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_D3D12DebugTools.h"
#include "Gfx/HIKARI_DXCheck.h"

#include <cassert>
#include <cstdint>
#include <sstream>

namespace {
	// リソースの説明を生成する。リソースのアドレスを 16 進数で表示する。
    std::string DescribeResource(ID3D12Resource* resource) {
        std::ostringstream oss;
        oss << "resource=0x" << std::hex << reinterpret_cast<uintptr_t>(resource);
        return oss.str();
    }

}

namespace HIKARI::GFX {

    void ResourceStateTracker::Reset() {
        knownStates_.clear();
    }
	// リソースをトラックする。resource にはトラックするリソースのポインタを、initialState にはそのリソースの初期状態を指定する。resource が nullptr の場合は何もしない。
    void ResourceStateTracker::Track(
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES initialState) {
        if (resource == nullptr) {
            return;
        }

        knownStates_[resource] = initialState;
    }
	// リソースのトラックを解除する。resource にはトラックを解除するリソースのポインタを指定する。resource が nullptr の場合は何もしない。
    void ResourceStateTracker::Forget(ID3D12Resource* resource) {
        if (resource == nullptr) {
            return;
        }

        knownStates_.erase(resource);
    }
	// リソースがトラックされているかどうかを確認する。resource には確認するリソースのポインタを指定する。resource が nullptr の場合は false を返す。
    bool ResourceStateTracker::Has(ID3D12Resource* resource) const {
        return resource != nullptr && knownStates_.find(resource) != knownStates_.end();
    }
	// リソースの現在の状態を取得する
    D3D12_RESOURCE_STATES ResourceStateTracker::GetState(
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES fallback) const {
        const auto found = knownStates_.find(resource);
        if (found == knownStates_.end()) {
            return fallback;
        }

        return found->second;
    }
	// リソースの状態を遷移させる。cmd にはコマンドリストのポインタを、resource には遷移させるリソースのポインタを、after には遷移後の状態を指定する。cmd または resource が nullptr の場合は何もしない。
    void ResourceStateTracker::Transition(
        ID3D12GraphicsCommandList* cmd,
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES after) {
        if (cmd == nullptr || resource == nullptr) {
            return;
        }
		// 遷移前の状態はトラックされている状態を使用する。トラックされていない場合はエラーをログに出力し、遷移前の状態は after と同じにする。
        const auto found = knownStates_.find(resource);
        if (found == knownStates_.end()) {
            const std::string message =
                "[ResourceStateTracker][ERROR] Transition called for untracked resource. " +
                DescribeResource(resource);
            DEBUGLOG::PushRenderError(message);
            HIKARI_LOG_ERROR(message);
            assert(false && "ResourceStateTracker::Transition called for an untracked resource.");
            return;
        }

        Transition(cmd, resource, found->second, after);
    }
	// リソースの状態を遷移させる。cmd にはコマンドリストのポインタを、resource には遷移させるリソースのポインタを、before には遷移前の状態を、after には遷移後の状態を指定する。cmd または resource が nullptr の場合は何もしない。
    void ResourceStateTracker::Transition(
        ID3D12GraphicsCommandList* cmd,
        ID3D12Resource* resource,
        D3D12_RESOURCE_STATES before,
        D3D12_RESOURCE_STATES after) {
        if (cmd == nullptr || resource == nullptr) {
            return;
        }

        D3D12_RESOURCE_STATES actualBefore = before;

        const auto found = knownStates_.find(resource);
        if (found != knownStates_.end()) {
            if (found->second != before) {
                std::ostringstream oss;
                oss << "[ResourceStateTracker][ERROR] before state mismatch. "
                    << DescribeResource(resource)
                    << " tracked=" << GFX::ResourceStateToString(found->second)
                    << " requestedBefore=" << GFX::ResourceStateToString(before)
                    << " requestedAfter=" << GFX::ResourceStateToString(after);

                const std::string message = oss.str();
                DEBUGLOG::PushRenderError(message);
                HIKARI_LOG_ERROR(message);
                assert(false && "ResourceStateTracker::Transition before state mismatch.");
            }

            actualBefore = found->second;
        }

        if (actualBefore == after) {
            knownStates_[resource] = after;
            return;
        }

        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(resource, actualBefore, after);
        cmd->ResourceBarrier(1, &barrier);
        knownStates_[resource] = after;
    }

} // namespace HIKARI::GFX
