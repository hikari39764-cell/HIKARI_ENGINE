#include "Render3D/HIKARI_Mesh.h"
#include <cstring>
#include <d3dx12.h>

namespace HIKARI {

    namespace {
		// -------- メッシュバッファ作成の共通処理 --------
        template<class TVertex>
		// 指定された頂点とインデックスのデータを GPU に転送し、バッファビューを設定します。
        bool CreateMeshBuffers(
            ID3D12Device* device,
            const std::vector<TVertex>& vertices,
            const std::vector<uint32_t>& indices,
            Microsoft::WRL::ComPtr<ID3D12Resource>& vertexBuffer,
            Microsoft::WRL::ComPtr<ID3D12Resource>& indexBuffer,
            D3D12_VERTEX_BUFFER_VIEW& vbView,
            D3D12_INDEX_BUFFER_VIEW& ibView,
            uint32_t& indexCount) {
            if (!device || vertices.empty() || indices.empty()) {
                return false;
            }

            const UINT vertexBufferBytes = static_cast<UINT>(sizeof(TVertex) * vertices.size());
            const UINT indexBufferBytes = static_cast<UINT>(sizeof(uint32_t) * indices.size());

            auto vbDesc = CD3DX12_RESOURCE_DESC::Buffer(vertexBufferBytes);
            auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

            if (FAILED(device->CreateCommittedResource(
                &uploadHeap,
                D3D12_HEAP_FLAG_NONE,
                &vbDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(vertexBuffer.GetAddressOf())))) {
                return false;
            }

            void* vbMapped = nullptr;
            if (FAILED(vertexBuffer->Map(0, nullptr, &vbMapped))) {
                return false;
            }
            std::memcpy(vbMapped, vertices.data(), vertexBufferBytes);
            vertexBuffer->Unmap(0, nullptr);

            auto ibDesc = CD3DX12_RESOURCE_DESC::Buffer(indexBufferBytes);
            if (FAILED(device->CreateCommittedResource(
                &uploadHeap,
                D3D12_HEAP_FLAG_NONE,
                &ibDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(indexBuffer.GetAddressOf())))) {
                return false;
            }

            void* ibMapped = nullptr;
            if (FAILED(indexBuffer->Map(0, nullptr, &ibMapped))) {
                return false;
            }
            std::memcpy(ibMapped, indices.data(), indexBufferBytes);
            indexBuffer->Unmap(0, nullptr);

            vbView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
            vbView.StrideInBytes = sizeof(TVertex);
            vbView.SizeInBytes = vertexBufferBytes;

            ibView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
            ibView.Format = DXGI_FORMAT_R32_UINT;
            ibView.SizeInBytes = indexBufferBytes;

            indexCount = static_cast<uint32_t>(indices.size());
            return true;
        }
    }
	// -------- メッシュ作成の共通処理 --------
    bool Mesh::CreateStatic(ID3D12Device* device, const std::vector<VertexStatic3D>& vertices, const std::vector<uint32_t>& indices) {
        return CreateMeshBuffers(device, vertices, indices, vertexBuffer_, indexBuffer_, vbView_, ibView_, indexCount_);
    }
	// -------- スキンメッシュ作成の共通処理 --------
    bool Mesh::CreateSkinned(ID3D12Device* device, const std::vector<VertexSkinnedGpu3D>& vertices, const std::vector<uint32_t>& indices) {
        return CreateMeshBuffers(device, vertices, indices, vertexBuffer_, indexBuffer_, vbView_, ibView_, indexCount_);
    }
	// -------- メッシュの有効性チェック --------
    bool Mesh::IsValid() const {
        return vertexBuffer_ && indexBuffer_ && indexCount_ > 0;
    }
	// -------- 頂点バッファビューの取得 --------
    const D3D12_VERTEX_BUFFER_VIEW& Mesh::GetVBView() const {
        return vbView_;
    }
	// -------- インデックスバッファビューの取得 --------
    const D3D12_INDEX_BUFFER_VIEW& Mesh::GetIBView() const {
        return ibView_;
    }
	// -------- インデックス数の取得 --------
    uint32_t Mesh::GetIndexCount() const {
        return indexCount_;
    }

} // namespace HIKARI
