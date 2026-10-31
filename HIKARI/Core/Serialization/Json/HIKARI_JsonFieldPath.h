#pragma once

#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "Core/Serialization/Json/HIKARI_JsonFields.h"

// 項目表のパス指定。"slots[1].effectAssetGuid"、"offset.position" のように、入れ子はドット、配列の添字は [n] で書く。
// 保存した JSON のキーがそのままパスになる。
namespace HIKARI::JSONFIELDS {

struct PathStep {
    std::string_view name;
    std::optional<size_t> index;
    // この段より後ろのパス（無ければ空）。
    std::string_view rest;
};

// パスの先頭の 1 段を切り出す。形が不正なら nullopt。
inline std::optional<PathStep> SplitPath(std::string_view path) {
    const size_t nameEnd = path.find_first_of(".[");
    PathStep step{path.substr(0, nameEnd), std::nullopt, {}};
    if (step.name.empty()) {
        return std::nullopt;
    }
    size_t next = nameEnd;
    if (next != std::string_view::npos && path[next] == '[') {
        const size_t close = path.find(']', next);
        if (close == std::string_view::npos) {
            return std::nullopt;
        }
        size_t index = 0;
        const char* digits = path.data() + next + 1;
        const char* digitsEnd = path.data() + close;
        const auto parsed = std::from_chars(digits, digitsEnd, index);
        if (digits == digitsEnd || parsed.ec != std::errc{} || parsed.ptr != digitsEnd) {
            return std::nullopt;
        }
        step.index = index;
        next = close + 1;
    }
    if (next < path.size()) {
        if (path[next] != '.' || next + 1 == path.size()) {
            return std::nullopt;
        }
        step.rest = path.substr(next + 1);
    }
    return step;
}

// Write した JSON の中でパスが指す値。無ければ nullptr。
inline const nlohmann::json* FindByPath(const nlohmann::json& root, std::string_view path) {
    const std::optional<PathStep> step = SplitPath(path);
    const nlohmann::json* node = step ? JSONREAD::FindMember(root, std::string(step->name).c_str()) : nullptr;
    if (node != nullptr && step->index) {
        node = node->is_array() && *step->index < node->size() ? &(*node)[*step->index] : nullptr;
    }
    return node == nullptr || step->rest.empty() ? node : FindByPath(*node, step->rest);
}

// パスが指す項目の値（保存と同じ JSON の形）。パスが表に無ければ nullopt。
template <class T, class Fields>
std::optional<nlohmann::json> GetField(const T& object, const Fields& fields, std::string_view path) {
    nlohmann::json json = nlohmann::json::object();
    Write(json, object, fields);
    const nlohmann::json* found = FindByPath(json, path);
    return found != nullptr ? std::optional<nlohmann::json>(*found) : std::nullopt;
}

namespace DETAIL {

template <class T, class Fields>
std::string SetIn(T& object, const Fields& fields, std::string_view path, const nlohmann::json& value);

// path の先頭の段が指す項目に value を書く。reason は失敗の理由（成功なら空のまま）。
template <class T>
struct Setter {
    T& object;
    const PathStep& step;
    const nlohmann::json& value;
    std::string& reason;
    bool& found;

    // 先頭の段がこの項目か。そうなら読み取り専用でないことも確かめる。
    template <class... Options>
    bool Targets(const char* key, const Options&... options) const {
        if (step.name != key) {
            return false;
        }
        found = true;
        if (MetaOf(options...).readOnly) {
            reason = "is read-only";
            return false;
        }
        return true;
    }

    template <class C, class M, class... Options>
        requires std::is_base_of_v<C, T>
    void operator()(const char* key, M C::* member, const Options&... options) const {
        if (!Targets(key, options...)) {
            return;
        }
        if constexpr ((kIsElements<Options> || ...)) {
            auto& list = object.*member;
            if (!step.index || *step.index >= list.size()) {
                reason = "expects an element index within range";
            } else {
                reason = SetIn(list[*step.index], SubFields(options...), step.rest, value);
            }
        } else if constexpr ((kIsNested<Options> || ...)) {
            reason = step.index ? "is not a list" : SetIn(object.*member, SubFields(options...), step.rest, value);
        } else if (step.index || !step.rest.empty()) {
            reason = "has no sub-fields";
        } else {
            CheckObjectRef<M, Options...>();
            auto next = Unwrap(object.*member);
            try {
                AssignJson(value, next);
            } catch (const std::exception& error) {
                reason = error.what();
                return;
            }
            if constexpr ((std::is_same_v<Options, Range> || ...)) {
                Clamp(next, Unwrap(object.*member), MetaOf(options...).range);
            }
            Unwrap(object.*member) = std::move(next);
        }
    }
    template <class C, class E, std::size_t N, class... Options>
        requires std::is_base_of_v<C, T>
    void operator()(const char* key, E C::* member, const TEXT::EnumName<E> (&names)[N],
                    const Options&... options) const {
        if (!Targets(key, options...)) {
            return;
        }
        if (step.index || !step.rest.empty()) {
            reason = "has no sub-fields";
        } else if (!value.is_string() || !TEXT::TryParseEnum(names, value.get<std::string>(), object.*member)) {
            reason = "expects one of the listed names";
        }
    }
};

template <class T, class Fields>
std::string SetIn(T& object, const Fields& fields, std::string_view path, const nlohmann::json& value) {
    const std::optional<PathStep> step = SplitPath(path);
    if (!step) {
        return path.empty() ? "is a struct or list; address one of its fields" : "is not a valid path";
    }
    std::string reason;
    bool found = false;
    fields(Setter<T>{object, *step, value, reason, found});
    return found ? reason : "no such field: " + std::string(step->name);
}

} // namespace DETAIL

// パスが指す項目に value を書く。型を確かめ、Range のある項目は範囲へ丸める。失敗したら理由を error へ書いて
// false を返し、object は変えない。構造体や配列そのものは書けないので、その中の項目をパスで指す。
template <class T, class Fields>
bool SetField(T& object, const Fields& fields, std::string_view path, const nlohmann::json& value, std::string& error) {
    const std::string reason = DETAIL::SetIn(object, fields, path, value);
    if (reason.empty()) {
        return true;
    }
    error = std::string(path) + ": " + reason;
    return false;
}

} // namespace HIKARI::JSONFIELDS
