#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Render3D/Resources/HIKARI_RenderResourceHandle.h"

namespace HIKARI::RENDER3D {

    enum class RenderResourceUsageFlags : uint32_t {
        None = 0,
        ShaderResource = 1u << 0,
        UnorderedAccess = 1u << 1,
        ConstantBuffer = 1u << 2,
        VertexBuffer = 1u << 3,
        IndexBuffer = 1u << 4,
        RenderTarget = 1u << 5,
        DepthStencil = 1u << 6,
        CopySource = 1u << 7,
        CopyDest = 1u << 8,
        IndirectArgument = 1u << 9,
    };

    inline constexpr RenderResourceUsageFlags operator|(
        RenderResourceUsageFlags lhs,
        RenderResourceUsageFlags rhs) {
        return static_cast<RenderResourceUsageFlags>(
            static_cast<uint32_t>(lhs) | static_cast<uint32_t>(rhs));
    }

    inline constexpr RenderResourceUsageFlags operator&(
        RenderResourceUsageFlags lhs,
        RenderResourceUsageFlags rhs) {
        return static_cast<RenderResourceUsageFlags>(
            static_cast<uint32_t>(lhs) & static_cast<uint32_t>(rhs));
    }

    inline constexpr bool HasFlag(RenderResourceUsageFlags flags, RenderResourceUsageFlags value) {
        return (static_cast<uint32_t>(flags & value)) != 0u;
    }

    enum class RenderResourceLifetime : uint8_t {
        External,
        Persistent,
        FrameTransient,
        ImportedAsset,
    };

    enum class RenderResourceViewKind : uint8_t {
        Srv,
        Uav,
        Cbv,
        Rtv,
        Dsv,
        Count
    };

    struct RenderResourceView {
        uint32_t descriptorIndex = UINT32_MAX;
        D3D12_CPU_DESCRIPTOR_HANDLE cpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE gpu{};

        bool IsValid() const {
            return descriptorIndex != UINT32_MAX || cpu.ptr != 0 || gpu.ptr != 0;
        }
    };

    struct RenderResourceDesc {
        RenderResourceKind kind = RenderResourceKind::Unknown;
        RenderResourceUsageFlags usage = RenderResourceUsageFlags::None;
        RenderResourceLifetime lifetime = RenderResourceLifetime::Persistent;

        std::string debugName{};
        std::string sourceKey{};

        uint64_t byteSize = 0;
        uint64_t width = 0;
        uint32_t height = 0;
        uint16_t depthOrArraySize = 0;
        uint16_t mipLevels = 0;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    };

    struct RenderResourceRecord {
        RenderResourceHandle handle{};
        RenderResourceDesc desc{};
        Microsoft::WRL::ComPtr<ID3D12Resource> ownedResource{};
        ID3D12Resource* externalResource = nullptr;
        std::array<RenderResourceView, static_cast<size_t>(RenderResourceViewKind::Count)> views{};
        uint64_t lastTouchedFrame = 0;
        bool pendingRelease = false;

        ID3D12Resource* GetResource() const {
            return ownedResource ? ownedResource.Get() : externalResource;
        }
    };

    struct RenderResourcePoolStats {
        uint32_t slotCount = 0;
        uint32_t aliveCount = 0;
        uint32_t freeSlotCount = 0;
        uint32_t pendingReleaseCount = 0;
        uint32_t sourceMappedCount = 0;
        uint64_t aliveByteSize = 0;
        std::array<uint32_t, static_cast<size_t>(RenderResourceKind::Count)> aliveCountByKind{};
    };

    class RenderResourcePool {
    public:
        void Clear();

        TextureResourceHandle RegisterExternalTexture(ID3D12Resource* resource, RenderResourceDesc desc = {});
        RenderTargetResourceHandle RegisterExternalRenderTarget(ID3D12Resource* resource, RenderResourceDesc desc = {});
        DepthTargetResourceHandle RegisterExternalDepthTarget(ID3D12Resource* resource, RenderResourceDesc desc = {});
        BufferResourceHandle RegisterExternalBuffer(ID3D12Resource* resource, RenderResourceDesc desc = {});

        MeshResourceHandle RegisterMesh(RenderResourceDesc desc);
        MaterialResourceHandle RegisterMaterial(RenderResourceDesc desc);
        ClusterGeometryResourceHandle RegisterClusterGeometry(RenderResourceDesc desc);

        RenderResourceHandle RegisterExternal(
            RenderResourceKind kind,
            ID3D12Resource* resource,
            RenderResourceDesc desc = {});
        RenderResourceHandle Adopt(
            RenderResourceKind kind,
            Microsoft::WRL::ComPtr<ID3D12Resource> resource,
            RenderResourceDesc desc = {});
        RenderResourceHandle RegisterVirtual(RenderResourceKind kind, RenderResourceDesc desc = {});
        bool AttachOwnedResource(
            RenderResourceHandle handle,
            Microsoft::WRL::ComPtr<ID3D12Resource> resource,
            RenderResourceDesc desc = {});

        bool Release(RenderResourceHandle handle);
        bool MarkPendingRelease(RenderResourceHandle handle);
        bool IsAlive(RenderResourceHandle handle) const;

        ID3D12Resource* GetResource(RenderResourceHandle handle) const;
        const RenderResourceRecord* GetRecord(RenderResourceHandle handle) const;
        RenderResourceRecord* GetMutableRecord(RenderResourceHandle handle);

        bool SetView(RenderResourceHandle handle, RenderResourceViewKind kind, RenderResourceView view);
        const RenderResourceView* GetView(RenderResourceHandle handle, RenderResourceViewKind kind) const;

        void Touch(RenderResourceHandle handle, uint64_t frameIndex);
        RenderResourceHandle FindBySourceKey(RenderResourceKind kind, const std::string& sourceKey) const;

        const RenderResourcePoolStats& GetStats() const;

        template <RenderResourceKind KindValue>
        ID3D12Resource* GetResource(TypedRenderResourceHandle<KindValue> handle) const {
            return GetResource(handle.ToUntyped());
        }

        template <RenderResourceKind KindValue>
        bool Release(TypedRenderResourceHandle<KindValue> handle) {
            return Release(handle.ToUntyped());
        }

    private:
        struct Slot {
            uint32_t generation = 1;
            bool alive = false;
            RenderResourceRecord record{};
        };

        uint32_t AllocateSlot();
        const Slot* ResolveSlot(RenderResourceHandle handle) const;
        Slot* ResolveSlot(RenderResourceHandle handle);

        RenderResourceHandle RegisterInternal(
            RenderResourceKind kind,
            ID3D12Resource* externalResource,
            Microsoft::WRL::ComPtr<ID3D12Resource> ownedResource,
            RenderResourceDesc desc);

        void RemoveSourceIndex(const RenderResourceRecord& record);
        void IndexSourceKey(const RenderResourceRecord& record);
        void RefreshStats();

        std::vector<Slot> slots_{};
        std::vector<uint32_t> freeSlots_{};
        std::unordered_map<std::string, RenderResourceHandle> sourceIndex_{};
        RenderResourcePoolStats stats_{};
    };

} // namespace HIKARI::RENDER3D
