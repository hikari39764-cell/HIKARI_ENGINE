#include "Scene/Components/Rendering/MaterialFx/HIKARI_MaterialFxComponent.h"

#include <algorithm>
#include <iterator>
#include <utility>

#include "Core/Serialization/Json/HIKARI_JsonMath.h"

namespace HIKARI {

    namespace JsonMath = SERIALIZATION::JSON::MATH;

    void MaterialFxComponent::Serialize(
        nlohmann::json& out) const {

        out["profileId"] = parameters_.GetProfileId();
        out["valuesInitialized"] = parameters_.AreValuesInitialized();
        if (!parameters_.AreValuesInitialized()) {
            return;
        }

        out["paramValues"] = nlohmann::json::array();
        for (const DirectX::XMFLOAT4& value : parameters_.GetValues()) {
            out["paramValues"].push_back(
                JsonMath::ToJsonArray(value));
        }
    }

    void MaterialFxComponent::Deserialize(
        const nlohmann::json& in) {

        const std::string profileId = in.value(
            "profileId",
            parameters_.GetProfileId());
        DirectX::XMFLOAT4
            decodedValues[VFX::kMaterialFxUserCount]{};

        bool hasParamValues = false;
        if (in.contains("paramValues") &&
            in["paramValues"].is_array()) {

            const nlohmann::json& values = in["paramValues"];
            const size_t count = (std::min)(
                values.size(),
                std::size(decodedValues));
            for (size_t i = 0; i < count; ++i) {
                const nlohmann::json& node = values[i];
                if (!node.is_array() || node.size() < 4u) {
                    continue;
                }

                decodedValues[i] = {
                    node[0].is_number()
                        ? node[0].get<float>()
                        : 0.0f,
                    node[1].is_number()
                        ? node[1].get<float>()
                        : 0.0f,
                    node[2].is_number()
                        ? node[2].get<float>()
                        : 0.0f,
                    node[3].is_number()
                        ? node[3].get<float>()
                        : 0.0f
                };
            }
            hasParamValues = true;
        }

        const bool valuesInitialized =
            in.value("valuesInitialized", hasParamValues);
        MaterialFxParameterSet next = parameters_;
        next.RestorePersistedState(
            profileId,
            decodedValues,
            valuesInitialized);
        (void)CommitParameterState(std::move(next));
    }

} // namespace HIKARI
