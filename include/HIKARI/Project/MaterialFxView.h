#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/ValueTypes.h"

namespace HIKARI {

inline constexpr size_t kGameProjectMaterialFxProfileIdMaximumBytes = 255u;
inline constexpr size_t kGameProjectMaterialFxParameterKeyMaximumBytes = 255u;

class ObjectView;
class PresentationModelView;
struct GameProjectMaterialFxBridgeAccess;
struct GameProjectSdkAccess;

// Non-owning runtime view over either an authored MaterialFxComponent or
// a render-only PresentationModel record. The target is resolved on every
// call. All operations are owner-thread only, runtime-only, and become
// invalid when the World or target lifetime ends.
class MaterialFxView {
public:
    MaterialFxView() = default;

    HIKARI_GAME_PROJECT_SDK_API bool IsValid() const noexcept;

    // Returns the UTF-8 byte count excluding the trailing NUL. The value
    // may change after any mutation or target lifetime transition.
    HIKARI_GAME_PROJECT_SDK_API size_t GetProfileIdSize() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool CopyProfileId(char* destination, size_t capacity) const noexcept;

    // A non-empty profile must resolve through the engine MaterialFx
    // profile registry. ClearProfile is the explicit no-effect path.
    HIKARI_GAME_PROJECT_SDK_API bool SetProfileId(std::string_view profileId) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool ClearProfile() const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool ResetToProfileDefaults() const noexcept;

    // Parameter names are case-sensitive and must match both the key and
    // declared profile type. Non-finite inputs are rejected atomically.
    HIKARI_GAME_PROJECT_SDK_API bool SetFloat(std::string_view key, float value) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool SetFloat2(std::string_view key, GameProjectFloat2 value) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool SetFloat3(std::string_view key, GameProjectFloat3 value) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool SetFloat4(std::string_view key, GameProjectFloat4 value) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool SetToggle(std::string_view key, uint8_t value) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool TryGetFloat(std::string_view key, float& outValue) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool TryGetFloat2(std::string_view key, GameProjectFloat2& outValue) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool TryGetFloat3(std::string_view key, GameProjectFloat3& outValue) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool TryGetFloat4(std::string_view key, GameProjectFloat4& outValue) const noexcept;
    HIKARI_GAME_PROJECT_SDK_API bool TryGetToggle(std::string_view key, uint8_t& outValue) const noexcept;

private:
    enum class TargetKind : uint32_t {
        None = 0u,
        SceneObject,
        PresentationModel,
    };

    friend class ObjectView;
    friend class PresentationModelView;
    friend struct GameProjectMaterialFxBridgeAccess;
    friend struct GameProjectSdkAccess;

    explicit MaterialFxView(GameProjectObjectHandle object) noexcept
        : world_(object.world), object_(object), targetKind_(TargetKind::SceneObject) {}

    MaterialFxView(GameProjectWorldHandle world, uint64_t ownerObjectHandle, uint64_t instanceId) noexcept
        : world_(world), ownerObjectHandle_(ownerObjectHandle), instanceId_(instanceId),
          targetKind_(TargetKind::PresentationModel) {}

    GameProjectWorldHandle world_{};
    GameProjectObjectHandle object_{};
    uint64_t ownerObjectHandle_ = 0u;
    uint64_t instanceId_ = 0u;
    TargetKind targetKind_ = TargetKind::None;
    uint32_t reserved_ = 0u;
};

static_assert(std::is_standard_layout_v<MaterialFxView>);
static_assert(std::is_trivially_copyable_v<MaterialFxView>);

} // namespace HIKARI
