#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

#include "HIKARI/Project/GameProjectSdk.h"
#include "HIKARI/Project/SpriteMaterialTypes.h"
#include "HIKARI/Project/WorldTypes.h"

namespace HIKARI {

class ObjectView;
struct GameProjectSpriteMaterialBridgeAccess;

// Non-owning identity view over one object's WorldSprite material binding.
// It is valid only while the World/object live and on the World owner thread.
class SpriteMaterialView {
public:
    SpriteMaterialView() = default;

    HIKARI_GAME_PROJECT_SDK_API bool IsValid() const noexcept;
    // Resolves an imported .hspritematerial and atomically binds it. Success
    // clears previous parameter overrides and enables the new material; failure
    // leaves the current binding unchanged.
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult BindAsset(std::string_view materialAssetGuid) const noexcept;
    // Removes the binding and its overrides. Clearing an already-unbound view
    // succeeds.
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult ClearBinding() const noexcept;
    // Disabling the material preserves its binding and parameter overrides.
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult SetEnabled(bool enabled) const noexcept;
    // Sets a metadata-declared value of the exact declared type. Values outside
    // an optional metadata range are rejected without changing component state.
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult
    SetParameter(std::string_view key, const GameProjectBindingValue& value) const noexcept;
    // Restores the selected value to its material preset, or shader default
    // when the material does not provide a preset.
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult ResetParameter(std::string_view key) const noexcept;
    // Restores every parameter to its material preset or shader default.
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult ResetParameters() const noexcept;
    // outState must have the exact V1 size/version. parameterCount describes
    // the bound shader's metadata; an unbound view reports zero parameters.
    HIKARI_GAME_PROJECT_SDK_API GameProjectWorldResult
    TryGetState(GameProjectSpriteMaterialState& outState) const noexcept;
    // Reads the current effective value (component override, material preset,
    // or shader default). On failure, outValue is unchanged.
    HIKARI_GAME_PROJECT_SDK_API bool TryGetParameter(std::string_view key,
                                                     GameProjectBindingValue& outValue) const noexcept;
    // Reads metadata by stable zero-based slot-table order. outInfo must have
    // the exact V1 size/version and zero reserved bytes; failure leaves it
    // unchanged. Strings are complete UTF-8 bytes including their NUL within
    // the fixed 128-byte fields.
    HIKARI_GAME_PROJECT_SDK_API bool TryGetParameterInfo(
        uint32_t index, GameProjectSpriteMaterialParameterInfo& outInfo) const noexcept;

private:
    friend class ObjectView;
    friend struct GameProjectSpriteMaterialBridgeAccess;

    explicit SpriteMaterialView(GameProjectObjectHandle object) noexcept : object_(object) {}

    GameProjectObjectHandle object_{};
};

static_assert(std::is_standard_layout_v<SpriteMaterialView>);
static_assert(std::is_trivially_copyable_v<SpriteMaterialView>);

} // namespace HIKARI
