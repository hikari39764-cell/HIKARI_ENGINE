#include "Scene/Components/HIKARI_CharacterLocomotionComponent.h"

#include "Core/HIKARI_MathConfig.h"

#include <algorithm>
#include <utility>

namespace HIKARI {

CharacterLocomotionSettings NormalizeCharacterLocomotionSettings(CharacterLocomotionSettings settings) {
    settings = JSONFIELDS::Sanitize(std::move(settings), kCharacterLocomotionFields);
    settings.predictiveContactDistance = (std::max)(settings.predictiveContactDistance, settings.characterPadding);
    return settings;
}

void CharacterLocomotionComponent::Serialize(nlohmann::json& out) const {
    out = nlohmann::json::object();
    JSONFIELDS::Write(out, settings_, kCharacterLocomotionFields);
}

void CharacterLocomotionComponent::Deserialize(const nlohmann::json& in) {
    CharacterLocomotionSettings decoded{};
    JSONFIELDS::Read(in, decoded, kCharacterLocomotionFields);
    settings_ = NormalizeCharacterLocomotionSettings(std::move(decoded));
}

float CharacterLocomotionComponent::GetTurnSpeedRadians() const noexcept {
    return settings_.turnSpeedDegrees * MATH::kDegreesToRadians;
}

float CharacterLocomotionComponent::GetMaximumSlopeAngleRadians() const noexcept {
    return settings_.maximumSlopeAngleDegrees * MATH::kDegreesToRadians;
}

} // namespace HIKARI
