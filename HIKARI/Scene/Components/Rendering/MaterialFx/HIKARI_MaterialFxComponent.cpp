#include "Scene/Components/Rendering/MaterialFx/HIKARI_MaterialFxComponent.h"

#include <utility>

#include "Scene/HIKARI_GameObject.h"

namespace HIKARI {

bool MaterialFxComponent::CommitParameterState(MaterialFxParameterSet next) {
    if (parameters_.HasSameState(next)) {
        return false;
    }
    parameters_ = std::move(next);
    NotifyRenderStateDirty();
    return true;
}

void MaterialFxComponent::NotifyRenderStateDirty() {
    if (GameObject* owner = GetOwner()) {
        owner->MarkRenderStateDirty();
    }
}

bool MaterialFxComponent::SetProfileId(std::string profileId) {
    MaterialFxParameterSet next = parameters_;
    if (!next.SetProfileId(std::move(profileId))) {
        return false;
    }
    (void)CommitParameterState(std::move(next));
    return true;
}

const std::string& MaterialFxComponent::GetProfileId() const { return parameters_.GetProfileId(); }

const std::array<DirectX::XMFLOAT4, VFX::kMaterialFxUserCount>& MaterialFxComponent::GetParamValues() const {
    return parameters_.GetValues();
}

bool MaterialFxComponent::AreValuesInitialized() const { return parameters_.AreValuesInitialized(); }

void MaterialFxComponent::SetParamValues(const DirectX::XMFLOAT4 (&values)[VFX::kMaterialFxUserCount],
                                         bool initialized) {
    MaterialFxParameterSet next = parameters_;
    next.SetRawValues(values, initialized);
    (void)CommitParameterState(std::move(next));
}

bool MaterialFxComponent::SetFloat(const std::string& key, float value) {
    MaterialFxParameterSet next = parameters_;
    if (next.SetFloat(key, value) != MaterialFxParameterResult::Success) {
        return false;
    }
    (void)CommitParameterState(std::move(next));
    return true;
}

bool MaterialFxComponent::SetFloat2(const std::string& key, const DirectX::XMFLOAT2& value) {
    MaterialFxParameterSet next = parameters_;
    if (next.SetFloat2(key, value) != MaterialFxParameterResult::Success) {
        return false;
    }
    (void)CommitParameterState(std::move(next));
    return true;
}

bool MaterialFxComponent::SetFloat3(const std::string& key, const DirectX::XMFLOAT3& value) {
    MaterialFxParameterSet next = parameters_;
    if (next.SetFloat3(key, value) != MaterialFxParameterResult::Success) {
        return false;
    }
    (void)CommitParameterState(std::move(next));
    return true;
}

bool MaterialFxComponent::SetFloat4(const std::string& key, const DirectX::XMFLOAT4& value) {
    MaterialFxParameterSet next = parameters_;
    if (next.SetFloat4(key, value) != MaterialFxParameterResult::Success) {
        return false;
    }
    (void)CommitParameterState(std::move(next));
    return true;
}

bool MaterialFxComponent::SetToggle(const std::string& key, bool value) {
    MaterialFxParameterSet next = parameters_;
    if (next.SetToggle(key, value) != MaterialFxParameterResult::Success) {
        return false;
    }
    (void)CommitParameterState(std::move(next));
    return true;
}

bool MaterialFxComponent::GetFloat(const std::string& key, float& out) const {
    return parameters_.TryGetFloat(key, out) == MaterialFxParameterResult::Success;
}

bool MaterialFxComponent::GetFloat2(const std::string& key, DirectX::XMFLOAT2& out) const {
    return parameters_.TryGetFloat2(key, out) == MaterialFxParameterResult::Success;
}

bool MaterialFxComponent::GetFloat3(const std::string& key, DirectX::XMFLOAT3& out) const {
    return parameters_.TryGetFloat3(key, out) == MaterialFxParameterResult::Success;
}

bool MaterialFxComponent::GetFloat4(const std::string& key, DirectX::XMFLOAT4& out) const {
    return parameters_.TryGetFloat4(key, out) == MaterialFxParameterResult::Success;
}

bool MaterialFxComponent::GetToggle(const std::string& key, bool& out) const {
    return parameters_.TryGetToggle(key, out) == MaterialFxParameterResult::Success;
}

bool MaterialFxComponent::ResetToProfileDefaults() {
    MaterialFxParameterSet next = parameters_;
    if (!next.ResetToProfileDefaults()) {
        return false;
    }
    (void)CommitParameterState(std::move(next));
    return true;
}

} // namespace HIKARI
