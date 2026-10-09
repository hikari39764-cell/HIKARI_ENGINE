#pragma once

#include <cstddef>
#include <cstring>
#include <string>
#include <string_view>

#include "Assets/HIKARI_AssetGuid.h"

namespace HIKARI::PROJECT::DETAIL {
template <size_t Capacity>
std::string ReadFixedString(const char (&value)[Capacity]) {
    size_t count = 0u;
    while (count < Capacity && value[count] != '\0') {
        ++count;
    }
    if (count == Capacity) {
        return {};
    }
    return std::string(value, count);
}

inline bool CopyStringToReference(std::string_view source, char* destination, size_t capacity) noexcept {
    if (destination == nullptr || capacity == 0u) {
        return false;
    }
    if (source.size() >= capacity) {
        return false;
    }
    const size_t count = source.size();
    std::memcpy(destination, source.data(), count);
    destination[count] = '\0';
    return true;
}

inline bool ReadStringBuffer(const char* source, size_t capacity, std::string& outValue) {
    if (source == nullptr || capacity == 0u) {
        return false;
    }
    size_t count = 0u;
    while (count < capacity && source[count] != '\0') {
        ++count;
    }
    if (count == capacity) {
        return false;
    }
    outValue.assign(source, count);
    return true;
}

template <size_t Capacity>
bool ReadFixedText(const char (&source)[Capacity], std::string& outValue) {
    if (ReadStringBuffer(source, Capacity, outValue))
        return true;
    outValue.clear();
    return false;
}

// 終端のある空文字列か資産 GUID だけを受け付ける（SDK の固定長の資産 GUID 欄）。
template <size_t Capacity>
bool ReadAssetGuidText(const char (&source)[Capacity], std::string& outGuid) {
    return ReadFixedText(source, outGuid) && (outGuid.empty() || IsValidAssetGuid(outGuid));
}

// 収まらない値は空にする（作者値の GUID は常に収まる）。
template <size_t Capacity>
void WriteAssetGuidText(const std::string& guid, char (&destination)[Capacity]) noexcept {
    if (!CopyStringToReference(guid, destination, Capacity)) {
        destination[0] = '\0';
    }
}

} // namespace HIKARI::PROJECT::DETAIL
