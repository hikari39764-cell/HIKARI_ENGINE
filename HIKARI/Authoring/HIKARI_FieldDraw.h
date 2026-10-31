#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

#include "Authoring/HIKARI_ComponentAuthoringTypes.h"

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
