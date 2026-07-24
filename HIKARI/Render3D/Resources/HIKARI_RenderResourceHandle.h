#pragma once

#include <cstdint>

namespace HIKARI::RENDER3D {

    enum class RenderResourceKind : uint8_t {
        Unknown,
        Texture,
        RenderTarget,
        DepthTarget,
        Buffer,
        Mesh,
        Material,
        ClusterGeometry,
        Count
    };

    struct RenderResourceHandle {
        RenderResourceKind kind = RenderResourceKind::Unknown;
        uint32_t index = 0;
        uint32_t generation = 0;

        constexpr bool IsValid() const {
            return kind != RenderResourceKind::Unknown && index != 0 && generation != 0;
        }

        constexpr explicit operator bool() const {
            return IsValid();
        }

        friend constexpr bool operator==(RenderResourceHandle lhs, RenderResourceHandle rhs) {
            return lhs.kind == rhs.kind && lhs.index == rhs.index && lhs.generation == rhs.generation;
        }

        friend constexpr bool operator!=(RenderResourceHandle lhs, RenderResourceHandle rhs) {
            return !(lhs == rhs);
        }
    };

    template <RenderResourceKind KindValue>
    struct TypedRenderResourceHandle {
        uint32_t index = 0;
        uint32_t generation = 0;

        static constexpr RenderResourceKind Kind = KindValue;

        constexpr bool IsValid() const {
            return index != 0 && generation != 0;
        }

        constexpr explicit operator bool() const {
            return IsValid();
        }

        constexpr RenderResourceHandle ToUntyped() const {
            return { KindValue, index, generation };
        }

        static constexpr TypedRenderResourceHandle FromUntyped(RenderResourceHandle handle) {
            return handle.kind == KindValue
                ? TypedRenderResourceHandle{ handle.index, handle.generation }
                : TypedRenderResourceHandle{};
        }

        friend constexpr bool operator==(
            TypedRenderResourceHandle lhs,
            TypedRenderResourceHandle rhs) {
            return lhs.index == rhs.index && lhs.generation == rhs.generation;
        }

        friend constexpr bool operator!=(
            TypedRenderResourceHandle lhs,
            TypedRenderResourceHandle rhs) {
            return !(lhs == rhs);
        }
    };

    using TextureResourceHandle = TypedRenderResourceHandle<RenderResourceKind::Texture>;
    using RenderTargetResourceHandle = TypedRenderResourceHandle<RenderResourceKind::RenderTarget>;
    using DepthTargetResourceHandle = TypedRenderResourceHandle<RenderResourceKind::DepthTarget>;
    using BufferResourceHandle = TypedRenderResourceHandle<RenderResourceKind::Buffer>;
    using MeshResourceHandle = TypedRenderResourceHandle<RenderResourceKind::Mesh>;
    using MaterialResourceHandle = TypedRenderResourceHandle<RenderResourceKind::Material>;
    using ClusterGeometryResourceHandle = TypedRenderResourceHandle<RenderResourceKind::ClusterGeometry>;

} // namespace HIKARI::RENDER3D
