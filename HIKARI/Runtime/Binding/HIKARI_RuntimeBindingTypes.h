#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "Core/Binding/HIKARI_BindingValue.h"
#include "Scene/HIKARI_RuntimeObjectHandle.h"

namespace HIKARI::BINDING {

struct VariableDefinition {
    std::string id{};
    std::string name{};
    std::string description{};
    Value defaultValue{};
    bool hasRange = false;
    float minimum = 0.0f;
    float maximum = 1.0f;
    float step = 0.01f;
};

struct EventDefinition {
    std::string id{};
    std::string name{};
    std::string description{};
    bool hasPayload = false;
    ValueType payloadType = ValueType::Float;
};

struct EventRecord {
    uint64_t sequence = 0u;
    uint64_t frameIndex = 0u;
    std::string eventId{};
    RuntimeObjectHandle sourceObject{};
    bool hasPayload = false;
    Value payload{};
};

} // namespace HIKARI::BINDING
