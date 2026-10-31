#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "Authoring/HIKARI_ComponentAuthoringTypes.h"
#include "Core/Math/HIKARI_Math3D.h"

// 項目表の Draw に渡す共通の編集欄（JSONFIELDS の Draw{...}）。builder の型に依らない関数オブジェクトなので、設定構造体の
// ヘッダから検査器の実装を引かずに使える。呼び出しは fn(builder, field, 項目の参照, その項目を持つ構造体の const 参照)。
namespace HIKARI::FIELD_DRAW {

// 複数行の文字列（std::string）。
struct MultilineText {
    template <class Builder, class Field, class Owner>
    bool operator()(Builder& builder, const Field& field, std::string& value, const Owner&) const {
        return builder.MultilineString(field, value);
    }
};

// 入力アクションの選択欄（std::string の ID）。Type は選べるアクションの値の型（INPUT::InputActionValueType の値）。
template <auto Type>
struct InputActionPicker {
    template <class Builder, class Field, class Owner>
    bool operator()(Builder& builder, const Field& field, std::string& value, const Owner&) const {
        return builder.InputActionIdPicker(field, Type, value);
    }
};

// 決まった文字列（std::string の ID）から選ぶ欄。今の値が選択肢に無い時は先頭を見せる。
template <std::size_t N>
struct StringChoice {
    std::array<AuthoringChoice, N> choices;

    template <class Builder, class Field, class Owner>
    bool operator()(Builder& builder, const Field& field, std::string& value, const Owner&) const {
        int selected = 0;
        for (std::size_t index = 0; index < N; ++index) {
            if (choices[index].id == value) {
                selected = static_cast<int>(index);
            }
        }
        if (!builder.Choice(field, selected, choices)) {
            return false;
        }
        value = std::string(choices[static_cast<std::size_t>(std::clamp(selected, 0, static_cast<int>(N) - 1))].id);
        return true;
    }
};

// 軸ごとのオン / オフ（0 か 1 を入れた Vec3）を X / Y / Z のチェック欄にする。各欄の ID は項目 ID に軸の名前を足したもの。
struct AxisFlags {
    template <class Builder, class Field, class Owner>
    bool operator()(Builder& builder, const Field& field, MATH::Vec3& axes, const Owner&) const {
        static constexpr const char* kNames[3]{"X", "Y", "Z"};
        float* const components[3]{&axes.x, &axes.y, &axes.z};
        bool changed = false;
        for (size_t axis = 0; axis < 3u; ++axis) {
            const std::string id = std::string(field.id) + kNames[axis];
            const std::string label = std::string(field.label) + " " + kNames[axis];
            bool on = *components[axis] >= 0.5f;
            if (builder.Bool(AuthoringFieldDescriptor{id, label, field.description, field.writable}, on)) {
                *components[axis] = on ? 1.0f : 0.0f;
                changed = true;
            }
        }
        return changed;
    }
};

// 決まった 3 つの数値から選ぶ欄（撮影解像度など）。今の値は、それ以上になる最初の選択肢として見せる。
struct NumberChoice3 {
    std::array<uint32_t, 3> values;
    std::array<AuthoringChoice, 3> choices;

    template <class Builder, class Field, class Owner>
    bool operator()(Builder& builder, const Field& field, uint32_t& value, const Owner&) const {
        int selected = value <= values[0] ? 0 : (value <= values[1] ? 1 : 2);
        if (!builder.Choice(field, selected, choices)) {
            return false;
        }
        value = values[static_cast<std::size_t>(std::clamp(selected, 0, 2))];
        return true;
    }
};

} // namespace HIKARI::FIELD_DRAW
