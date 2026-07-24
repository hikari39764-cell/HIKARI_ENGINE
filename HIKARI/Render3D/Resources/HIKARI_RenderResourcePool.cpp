#include "Render3D/Resources/HIKARI_RenderResourcePool.h"

#include <algorithm>
#include <utility>

namespace HIKARI::RENDER3D {

    namespace {

        constexpr uint32_t kFirstResourceGeneration = 1u;

        uint32_t NextGeneration(uint32_t generation) {
            ++generation;
            return generation == 0 ? kFirstResourceGeneration : generation;
        }

        size_t KindIndex(RenderResourceKind kind) {
            return static_cast<size_t>(kind);
        }

        std::string BuildSourceIndexKey(RenderResourceKind kind, const std::string& sourceKey) {
            return std::to_string(static_cast<uint32_t>(kind)) + ":" + sourceKey;
        }

        uint64_t EstimateByteSize(const RenderResourceDesc& desc, ID3D12Resource* resource) {
            if (desc.byteSize != 0) {
                return desc.byteSize;
            }
            if (resource == nullptr) {
                return 0;
            }

            const D3D12_RESOURCE_DESC nativeDesc = resource->GetDesc();
            if (nativeDesc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER) {
                return nativeDesc.Width;
            }

            return 0;
        }

        RenderResourceUsageFlags DefaultUsageForKind(RenderResourceKind kind) {
            switch (kind) {
            case RenderResourceKind::Texture:
                return RenderResourceUsageFlags::ShaderResource | RenderResourceUsageFlags::CopyDest;
            case RenderResourceKind::RenderTarget:
                return RenderResourceUsageFlags::RenderTarget | RenderResourceUsageFlags::ShaderResource;
            case RenderResourceKind::DepthTarget:
                return RenderResourceUsageFlags::DepthStencil | RenderResourceUsageFlags::ShaderResource;
            case RenderResourceKind::Buffer:
                return RenderResourceUsageFlags::ShaderResource | RenderResourceUsageFlags::CopyDest;
            case RenderResourceKind::Mesh:
                return RenderResourceUsageFlags::VertexBuffer | RenderResourceUsageFlags::IndexBuffer;
            case RenderResourceKind::Material:
                return RenderResourceUsageFlags::ShaderResource;
            case RenderResourceKind::ClusterGeometry:
                return RenderResourceUsageFlags::ShaderResource | RenderResourceUsageFlags::IndirectArgument;
            case RenderResourceKind::Unknown:
            case RenderResourceKind::Count:
            default:
                return RenderResourceUsageFlags::None;
            }
        }

        RenderResourceDesc NormalizeDesc(
            RenderResourceKind kind,
            ID3D12Resource* resource,
            RenderResourceDesc desc) {

            desc.kind = kind;
            if (desc.usage == RenderResourceUsageFlags::None) {
                desc.usage = DefaultUsageForKind(kind);
            }

            if (resource != nullptr) {
                const D3D12_RESOURCE_DESC nativeDesc = resource->GetDesc();
                if (desc.width == 0) {
                    desc.width = nativeDesc.Width;
                }
                if (desc.height == 0) {
                    desc.height = nativeDesc.Height;
                }
                if (desc.depthOrArraySize == 0) {
                    desc.depthOrArraySize = nativeDesc.DepthOrArraySize;
                }
                if (desc.mipLevels == 0) {
                    desc.mipLevels = nativeDesc.MipLevels;
                }
                if (desc.format == DXGI_FORMAT_UNKNOWN) {
                    desc.format = nativeDesc.Format;
                }
            }

            desc.byteSize = EstimateByteSize(desc, resource);
            return desc;
        }

    } // namespace

    void RenderResourcePool::Clear() {
        slots_.clear();
        freeSlots_.clear();
        sourceIndex_.clear();
        stats_ = {};
    }

    TextureResourceHandle RenderResourcePool::RegisterExternalTexture(
        ID3D12Resource* resource,
        RenderResourceDesc desc) {
        return TextureResourceHandle::FromUntyped(
            RegisterExternal(RenderResourceKind::Texture, resource, std::move(desc)));
    }

    RenderTargetResourceHandle RenderResourcePool::RegisterExternalRenderTarget(
        ID3D12Resource* resource,
        RenderResourceDesc desc) {
        return RenderTargetResourceHandle::FromUntyped(
            RegisterExternal(RenderResourceKind::RenderTarget, resource, std::move(desc)));
    }

    DepthTargetResourceHandle RenderResourcePool::RegisterExternalDepthTarget(
        ID3D12Resource* resource,
        RenderResourceDesc desc) {
        return DepthTargetResourceHandle::FromUntyped(
            RegisterExternal(RenderResourceKind::DepthTarget, resource, std::move(desc)));
    }

    BufferResourceHandle RenderResourcePool::RegisterExternalBuffer(
        ID3D12Resource* resource,
        RenderResourceDesc desc) {
        return BufferResourceHandle::FromUntyped(
            RegisterExternal(RenderResourceKind::Buffer, resource, std::move(desc)));
    }

    MeshResourceHandle RenderResourcePool::RegisterMesh(RenderResourceDesc desc) {
        return MeshResourceHandle::FromUntyped(RegisterVirtual(RenderResourceKind::Mesh, std::move(desc)));
    }

    MaterialResourceHandle RenderResourcePool::RegisterMaterial(RenderResourceDesc desc) {
        return MaterialResourceHandle::FromUntyped(RegisterVirtual(RenderResourceKind::Material, std::move(desc)));
    }

    ClusterGeometryResourceHandle RenderResourcePool::RegisterClusterGeometry(RenderResourceDesc desc) {
        return ClusterGeometryResourceHandle::FromUntyped(
            RegisterVirtual(RenderResourceKind::ClusterGeometry, std::move(desc)));
    }

    RenderResourceHandle RenderResourcePool::RegisterExternal(
        RenderResourceKind kind,
        ID3D12Resource* resource,
        RenderResourceDesc desc) {
        return RegisterInternal(kind, resource, {}, std::move(desc));
    }

    RenderResourceHandle RenderResourcePool::Adopt(
        RenderResourceKind kind,
        Microsoft::WRL::ComPtr<ID3D12Resource> resource,
        RenderResourceDesc desc) {
        return RegisterInternal(kind, nullptr, std::move(resource), std::move(desc));
    }

    RenderResourceHandle RenderResourcePool::RegisterVirtual(RenderResourceKind kind, RenderResourceDesc desc) {
        return RegisterInternal(kind, nullptr, {}, std::move(desc));
    }

    bool RenderResourcePool::AttachOwnedResource(
        RenderResourceHandle handle,
        Microsoft::WRL::ComPtr<ID3D12Resource> resource,
        RenderResourceDesc desc) {

        Slot* slot = ResolveSlot(handle);
        if (slot == nullptr || !resource) {
            return false;
        }

        RenderResourceRecord& record = slot->record;
        RemoveSourceIndex(record);

        RenderResourceDesc merged = record.desc;
        if (desc.usage != RenderResourceUsageFlags::None) {
            merged.usage = desc.usage;
        }
        if (!desc.debugName.empty()) {
            merged.debugName = std::move(desc.debugName);
        }
        if (!desc.sourceKey.empty()) {
            merged.sourceKey = std::move(desc.sourceKey);
        }
        if (desc.byteSize != 0) {
            merged.byteSize = desc.byteSize;
        }
        if (desc.width != 0) {
            merged.width = desc.width;
        }
        if (desc.height != 0) {
            merged.height = desc.height;
        }
        if (desc.depthOrArraySize != 0) {
            merged.depthOrArraySize = desc.depthOrArraySize;
        }
        if (desc.mipLevels != 0) {
            merged.mipLevels = desc.mipLevels;
        }
        if (desc.format != DXGI_FORMAT_UNKNOWN) {
            merged.format = desc.format;
        }
        if (desc.lifetime != RenderResourceLifetime::Persistent ||
            merged.lifetime == RenderResourceLifetime::Persistent) {
            merged.lifetime = desc.lifetime;
        }

        // 仮想 handle を維持したまま、実 GPU resource だけを差し替える。
        record.desc = NormalizeDesc(handle.kind, resource.Get(), std::move(merged));
        record.externalResource = nullptr;
        record.ownedResource = std::move(resource);
        record.pendingRelease = false;

        IndexSourceKey(record);
        RefreshStats();
        return true;
    }

    bool RenderResourcePool::Release(RenderResourceHandle handle) {
        Slot* slot = ResolveSlot(handle);
        if (slot == nullptr) {
            return false;
        }

        RemoveSourceIndex(slot->record);

        slot->record = {};
        slot->alive = false;
        slot->generation = NextGeneration(slot->generation);
        freeSlots_.push_back(handle.index - 1u);
        RefreshStats();
        return true;
    }

    bool RenderResourcePool::MarkPendingRelease(RenderResourceHandle handle) {
        Slot* slot = ResolveSlot(handle);
        if (slot == nullptr) {
            return false;
        }

        slot->record.pendingRelease = true;
        RefreshStats();
        return true;
    }

    bool RenderResourcePool::IsAlive(RenderResourceHandle handle) const {
        return ResolveSlot(handle) != nullptr;
    }

    ID3D12Resource* RenderResourcePool::GetResource(RenderResourceHandle handle) const {
        const RenderResourceRecord* record = GetRecord(handle);
        return record != nullptr ? record->GetResource() : nullptr;
    }

    const RenderResourceRecord* RenderResourcePool::GetRecord(RenderResourceHandle handle) const {
        const Slot* slot = ResolveSlot(handle);
        return slot != nullptr ? &slot->record : nullptr;
    }

    RenderResourceRecord* RenderResourcePool::GetMutableRecord(RenderResourceHandle handle) {
        Slot* slot = ResolveSlot(handle);
        return slot != nullptr ? &slot->record : nullptr;
    }

    bool RenderResourcePool::SetView(
        RenderResourceHandle handle,
        RenderResourceViewKind kind,
        RenderResourceView view) {

        RenderResourceRecord* record = GetMutableRecord(handle);
        const size_t viewIndex = static_cast<size_t>(kind);
        if (record == nullptr || viewIndex >= record->views.size()) {
            return false;
        }

        record->views[viewIndex] = view;
        return true;
    }

    const RenderResourceView* RenderResourcePool::GetView(
        RenderResourceHandle handle,
        RenderResourceViewKind kind) const {

        const RenderResourceRecord* record = GetRecord(handle);
        const size_t viewIndex = static_cast<size_t>(kind);
        if (record == nullptr || viewIndex >= record->views.size()) {
            return nullptr;
        }

        return &record->views[viewIndex];
    }

    void RenderResourcePool::Touch(RenderResourceHandle handle, uint64_t frameIndex) {
        RenderResourceRecord* record = GetMutableRecord(handle);
        if (record != nullptr) {
            record->lastTouchedFrame = frameIndex;
        }
    }

    RenderResourceHandle RenderResourcePool::FindBySourceKey(
        RenderResourceKind kind,
        const std::string& sourceKey) const {

        if (sourceKey.empty()) {
            return {};
        }

        const auto found = sourceIndex_.find(BuildSourceIndexKey(kind, sourceKey));
        return found != sourceIndex_.end() && IsAlive(found->second) ? found->second : RenderResourceHandle{};
    }

    const RenderResourcePoolStats& RenderResourcePool::GetStats() const {
        return stats_;
    }

    uint32_t RenderResourcePool::AllocateSlot() {
        if (!freeSlots_.empty()) {
            const uint32_t slotIndex = freeSlots_.back();
            freeSlots_.pop_back();
            return slotIndex;
        }

        slots_.push_back({});
        return static_cast<uint32_t>(slots_.size() - 1u);
    }

    const RenderResourcePool::Slot* RenderResourcePool::ResolveSlot(RenderResourceHandle handle) const {
        if (!handle.IsValid() || handle.index == 0) {
            return nullptr;
        }

        const uint32_t slotIndex = handle.index - 1u;
        if (slotIndex >= slots_.size()) {
            return nullptr;
        }

        const Slot& slot = slots_[slotIndex];
        if (!slot.alive || slot.generation != handle.generation || slot.record.handle.kind != handle.kind) {
            return nullptr;
        }

        return &slot;
    }

    RenderResourcePool::Slot* RenderResourcePool::ResolveSlot(RenderResourceHandle handle) {
        return const_cast<Slot*>(
            static_cast<const RenderResourcePool*>(this)->ResolveSlot(handle));
    }

    RenderResourceHandle RenderResourcePool::RegisterInternal(
        RenderResourceKind kind,
        ID3D12Resource* externalResource,
        Microsoft::WRL::ComPtr<ID3D12Resource> ownedResource,
        RenderResourceDesc desc) {

        if (kind == RenderResourceKind::Unknown || kind == RenderResourceKind::Count) {
            return {};
        }

        // リソース層は GPU 実体を持たない論理リソースも扱う。
        ID3D12Resource* nativeResource = ownedResource ? ownedResource.Get() : externalResource;
        desc = NormalizeDesc(kind, nativeResource, std::move(desc));

        const uint32_t slotIndex = AllocateSlot();
        Slot& slot = slots_[slotIndex];
        slot.alive = true;

        RenderResourceHandle handle{};
        handle.kind = kind;
        handle.index = slotIndex + 1u;
        handle.generation = slot.generation;

        RenderResourceRecord record{};
        record.handle = handle;
        record.desc = std::move(desc);
        record.externalResource = externalResource;
        record.ownedResource = std::move(ownedResource);

        slot.record = std::move(record);
        IndexSourceKey(slot.record);
        RefreshStats();
        return handle;
    }

    void RenderResourcePool::RemoveSourceIndex(const RenderResourceRecord& record) {
        if (record.desc.sourceKey.empty()) {
            return;
        }

        const std::string key = BuildSourceIndexKey(record.handle.kind, record.desc.sourceKey);
        const auto found = sourceIndex_.find(key);
        if (found != sourceIndex_.end() && found->second == record.handle) {
            sourceIndex_.erase(found);
        }
    }

    void RenderResourcePool::IndexSourceKey(const RenderResourceRecord& record) {
        if (!record.handle.IsValid() || record.desc.sourceKey.empty()) {
            return;
        }

        sourceIndex_[BuildSourceIndexKey(record.handle.kind, record.desc.sourceKey)] = record.handle;
    }

    void RenderResourcePool::RefreshStats() {
        stats_ = {};
        stats_.slotCount = static_cast<uint32_t>((std::min<size_t>)(slots_.size(), UINT32_MAX));
        stats_.freeSlotCount = static_cast<uint32_t>((std::min<size_t>)(freeSlots_.size(), UINT32_MAX));
        stats_.sourceMappedCount = static_cast<uint32_t>((std::min<size_t>)(sourceIndex_.size(), UINT32_MAX));

        for (const Slot& slot : slots_) {
            if (!slot.alive) {
                continue;
            }

            ++stats_.aliveCount;
            stats_.aliveByteSize += slot.record.desc.byteSize;
            if (slot.record.pendingRelease) {
                ++stats_.pendingReleaseCount;
            }

            const size_t kindIndex = KindIndex(slot.record.handle.kind);
            if (kindIndex < stats_.aliveCountByKind.size()) {
                ++stats_.aliveCountByKind[kindIndex];
            }
        }
    }

} // namespace HIKARI::RENDER3D
