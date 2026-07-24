#include "Scene/Components/Rendering/MaterialFx/HIKARI_MaterialFxComponent.h"

#include <algorithm>
#include <iterator>

#include "Core/Serialization/Json/HIKARI_JsonMath.h"

namespace HIKARI {

    namespace JsonMath = SERIALIZATION::JSON::MATH;

    void MaterialFxComponent::Serialize(
        nlohmann::json& out) const {

        out["profileId"] = profileId_;
        out["valuesInitialized"] = valuesInitialized_;
        if (!valuesInitialized_) {
            return;
        }

        out["paramValues"] = nlohmann::json::array();
        for (const DirectX::XMFLOAT4& value : paramValues_) {
            out["paramValues"].push_back(
                JsonMath::ToJsonArray(value));
        }
    }

    void MaterialFxComponent::Deserialize(
        const nlohmann::json& in) {

        profileId_ = in.value("profileId", profileId_);
        for (DirectX::XMFLOAT4& value : paramValues_) {
            value = {};
        }

        bool hasParamValues = false;
        if (in.contains("paramValues") &&
            in["paramValues"].is_array()) {

            const nlohmann::json& values = in["paramValues"];
            const size_t count = (std::min)(
                values.size(),
                std::size(paramValues_));
            for (size_t i = 0; i < count; ++i) {
                const nlohmann::json& node = values[i];
                if (!node.is_array() || node.size() < 4u) {
                    continue;
                }

                paramValues_[i] = {
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

        valuesInitialized_ =
            in.value("valuesInitialized", hasParamValues);
        if (!valuesInitialized_) {
            for (DirectX::XMFLOAT4& value : paramValues_) {
                value = {};
            }
        }
        NotifyRenderStateDirty();
    }

} // namespace HIKARI
