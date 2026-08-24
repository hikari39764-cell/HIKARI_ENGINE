#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

namespace HIKARI::SERVICES {

    // The storage service deliberately deals only in opaque bytes.  The game
    // project owns the envelope/schema and this service owns the safe file
    // boundary.
    enum class GameProjectSaveStorageRoot : uint8_t {
        User,
        Project,
    };

    class GameProjectSaveStorageService final {
    public:
        static constexpr size_t kMaxSlotBytes = 1024u * 1024u;
        static constexpr size_t kMaxSlotNameBytes = 64u;

        GameProjectSaveStorageService() = default;

        // projectRoot is the opened project directory.  userSaveRoot may be
        // supplied by a host/test; when empty, Configure derives a per-project
        // root from LOCALAPPDATA (or APPDATA as a compatibility fallback).
        bool Configure(
            const std::filesystem::path& projectRoot,
            const std::filesystem::path& userSaveRoot = {},
            std::string* outMessage = nullptr);

        void Reset() noexcept;
        bool IsConfigured() const noexcept;

        const std::filesystem::path& GetProjectSaveRoot() const noexcept;
        const std::filesystem::path& GetUserSaveRoot() const noexcept;

        bool Write(
            std::string_view slotName,
            std::span<const std::byte> bytes,
            std::string* outMessage = nullptr);

        bool Write(
            GameProjectSaveStorageRoot root,
            std::string_view slotName,
            std::span<const std::byte> bytes,
            std::string* outMessage = nullptr);

        bool Read(
            std::string_view slotName,
            std::span<std::byte> destination,
            size_t& outBytesRead,
            std::string* outMessage = nullptr) const;

        bool Read(
            GameProjectSaveStorageRoot root,
            std::string_view slotName,
            std::span<std::byte> destination,
            size_t& outBytesRead,
            std::string* outMessage = nullptr) const;

        bool Exists(std::string_view slotName) const;
        bool Exists(
            GameProjectSaveStorageRoot root,
            std::string_view slotName) const;

        size_t Size(std::string_view slotName) const;
        size_t Size(
            GameProjectSaveStorageRoot root,
            std::string_view slotName) const;

        // Delete is idempotent for a missing slot.
        bool Delete(
            std::string_view slotName,
            std::string* outMessage = nullptr);

        bool Delete(
            GameProjectSaveStorageRoot root,
            std::string_view slotName,
            std::string* outMessage = nullptr);

    private:
        bool TryResolveSlotPath(
            GameProjectSaveStorageRoot root,
            std::string_view slotName,
            std::filesystem::path& outPath,
            std::string* outMessage) const;

        const std::filesystem::path& ResolveRoot(
            GameProjectSaveStorageRoot root) const noexcept;

        std::filesystem::path projectSaveRoot_{};
        std::filesystem::path userSaveRoot_{};
    };

    GameProjectSaveStorageService& GetGameProjectSaveStorageService();

} // namespace HIKARI::SERVICES
