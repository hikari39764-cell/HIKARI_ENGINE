#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "Authoring/HIKARI_IInspectorBuilder.h"
#include "Core/Serialization/Json/HIKARI_JsonFieldOptions.h"
#include "Core/Text/HIKARI_HumanizeKey.h"

// JSONFIELDS の項目表から検査器の項目を作る。保存・読み取り・説明と同じ一つの表を使い、項目ごとの UI を書かない。
// 並びは Common の項目が先頭、続いて宣言順（Group が同じ項目は 1 つの節）、最後に既定で閉じた "Advanced" 節。
// Show が false の項目は描かない。Elements は 1 要素 1 節で、複製・上へ・下へ・削除のボタンと、末尾の追加ボタンを持つ
// （std::array の固定長は節だけ）。
// Draw を付けた項目は fn が描く（表での位置・Group・Show は同じ）。
// 項目 ID は表のキーで、Elements の中は "slots[0].name"、Nested の中は "offset.position" のようになる。
// 項目は構造体自身か基底クラスのメンバ（基底の表を派生構造体の表から呼べる）。
// 表示名・説明は英語の原文のまま builder へ渡す（翻訳は検査器の側）。戻り値は値が変わったか（要素の追加・削除を含む）。
namespace HIKARI {
namespace FIELD_INSPECTOR {

// 表を 3 回なぞり、1 回に 1 つの段階を描く。
enum class Phase { Common, Normal, Advanced };

template <class T, class Fields>
bool InspectTable(IInspectorBuilder& builder, T& object, const Fields& fields, const std::string& prefix);

// 整数の項目は int の欄で編集する。int に収まらない値は端へ寄せる。32 ビットの符号なし（マスクや乱数の種）は全ての値に
// 届くよう、ビット列のまま符号付きで見せる。
template <class I>
bool EditInteger(IInspectorBuilder& builder, const AuthoringFieldDescriptor& field, I& value) {
    if constexpr (std::is_same_v<I, uint32_t>) {
        int bits = std::bit_cast<int>(value);
        if (!builder.Int(field, bits)) {
            return false;
        }
        value = std::bit_cast<uint32_t>(bits);
        return true;
    } else {
        constexpr int kMinimum = (std::numeric_limits<int>::min)();
        constexpr int kMaximum = (std::numeric_limits<int>::max)();
        int shown = std::cmp_less(value, kMinimum)
                        ? kMinimum
                        : (std::cmp_greater(value, kMaximum) ? kMaximum : static_cast<int>(value));
        if (!builder.Int(field, shown)) {
            return false;
        }
        if (std::cmp_less(shown, (std::numeric_limits<I>::min)())) {
            value = (std::numeric_limits<I>::min)();
        } else if (std::cmp_greater(shown, (std::numeric_limits<I>::max)())) {
            value = (std::numeric_limits<I>::max)();
        } else {
            value = static_cast<I>(shown);
        }
        return true;
    }
}

// 要素の見出し用に、空でない String の "name" 項目を探す。
template <class E>
struct NameFinder {
    const E& element;
    const std::string*& name;

    template <class C, class M, class... Options>
        requires std::is_base_of_v<C, E>
    void operator()(const char* key, M C::* member, const Options&...) const {
        if constexpr (std::is_same_v<M, std::string>) {
            if (std::string_view(key) == "name" && !(element.*member).empty()) {
                name = &(element.*member);
            }
        }
    }
};

// 見出しは要素の name、無ければ "<Label> <番号>"。ImGui の ID は添字つきのパスで固定する。
template <class E, class Fields>
std::string ElementTitle(const E& element, const Fields& fields, const std::string& label, size_t index) {
    const std::string* name = nullptr;
    fields(NameFinder<E>{element, name});
    return name != nullptr ? *name : label + " " + std::to_string(index + 1u);
}

template <class T>
struct Drawer {
    IInspectorBuilder& builder;
    T& object;
    const std::string& prefix;
    Phase phase;
    bool& changed;
    // 今開いている節の名前（無い間は空）と、その節が開いているか。閉じた節の項目は描かない。
    std::string_view section{};
    bool sectionOpen = true;

    bool Wanted(const JSONFIELDS::Meta& meta) const {
        switch (phase) {
        case Phase::Common: return meta.common;
        case Phase::Advanced: return meta.advanced && !meta.common;
        default: return !meta.common && !meta.advanced;
        }
    }

    template <class Option>
    bool Shown(const Option&) const {
        return true;
    }
    template <class Visible>
    bool Shown(const JSONFIELDS::Show<Visible>& show) const {
        return show.visible(std::as_const(object));
    }

    // 項目の属する節へ入る。節が変わった時だけ見出しを出し、節が閉じていれば false。
    bool EnterSection(const JSONFIELDS::Meta& meta) {
        std::string_view wanted{};
        if (phase == Phase::Advanced) {
            wanted = "Advanced";
        } else if (phase == Phase::Normal) {
            wanted = meta.group;
        }
        if (wanted != section) {
            section = wanted;
            sectionOpen = wanted.empty() ||
                          builder.Section(std::string(wanted) + "###" + prefix + "section." + std::string(wanted),
                                          phase != Phase::Advanced);
        }
        return sectionOpen;
    }

    // 今の段階で描く項目か（Hidden でなく、Show が真で、節が開いている）。meta にオプションの集計を返す。
    template <class... Options>
    bool Select(JSONFIELDS::Meta& meta, const Options&... options) {
        meta = JSONFIELDS::MetaOf(options...);
        return Wanted(meta) && !meta.hidden && (Shown(options) && ...) && EnterSection(meta);
    }

    template <JSONFIELDS::FieldKind kind, class V, class... Options>
    bool EditLeaf(const AuthoringFieldDescriptor& field, [[maybe_unused]] const JSONFIELDS::Meta& meta, V& value,
                  [[maybe_unused]] const Options&... options) {
        if constexpr (kind == JSONFIELDS::FieldKind::Bool) {
            return builder.Bool(field, value);
        } else if constexpr (kind == JSONFIELDS::FieldKind::Int) {
            return EditInteger(builder, field, value);
        } else if constexpr (kind == JSONFIELDS::FieldKind::Float) {
            static_assert(std::is_same_v<V, float>, "The inspector edits float members");
            if (!meta.hasRange) {
                return builder.Float(field, value);
            }
            // ドラッグの速さは範囲の 1/1000（欄の既定と同じ）。
            return builder.FloatRange(field, value, static_cast<float>(meta.range.min),
                                      static_cast<float>(meta.range.max),
                                      static_cast<float>((meta.range.max - meta.range.min) / 1000.0));
        } else if constexpr (kind == JSONFIELDS::FieldKind::Vec2) {
            return builder.Vec2(field, value.x, value.y);
        } else if constexpr (kind == JSONFIELDS::FieldKind::Vec3) {
            return meta.color ? builder.Color3(field, value.x, value.y, value.z)
                              : builder.Vec3(field, value.x, value.y, value.z);
        } else if constexpr (kind == JSONFIELDS::FieldKind::String) {
            return builder.String(field, value);
        } else if constexpr (kind == JSONFIELDS::FieldKind::AssetRef) {
            return builder.AssetIdPicker(field, JSONFIELDS::AssetTypeOf(options...), value);
        } else if constexpr (kind == JSONFIELDS::FieldKind::ObjectRef) {
            return builder.SceneObjectIdPicker(field, value);
        } else if constexpr (kind == JSONFIELDS::FieldKind::EventRef) {
            // ペイロードは問わない（合わない選択は検証が拒む）。
            return builder.RuntimeEventIdPicker(field, AuthoringEventPayloadPolicy::Any, BINDING::ValueType::Float,
                                                value);
        } else {
            static_assert(kind == JSONFIELDS::FieldKind::VariableRef);
            return builder.RuntimeVariableIdPicker(field, BINDING::kAllValueTypes, value);
        }
    }

    // 固定長の配列（std::array）は要素ごとの節だけを描く（追加・削除・並べ替えは無い）。先頭の節だけ開く。
    template <class E, std::size_t N, class ElementFields>
    bool EditElements(const std::string& id, const std::string& label, std::array<E, N>& list,
                      const ElementFields& fields) {
        bool edited = false;
        for (size_t index = 0; index < N; ++index) {
            const std::string path = id + "[" + std::to_string(index) + "]";
            if (builder.Section(ElementTitle(list[index], fields, label, index) + "###" + path, index == 0u)) {
                edited |= InspectTable(builder, list[index], fields, path + ".");
            }
        }
        return edited;
    }

    // 配列の各要素を節にして描く。節の末尾に複製・上へ・下へ・削除、配列の末尾に追加のボタンを置く。
    // 複製は要素をそのまま写すので、要素の ID は呼び出し側が正規化して付け直す。
    template <class E, class ElementFields>
    bool EditElements(const std::string& id, const std::string& label, std::vector<E>& list,
                      const ElementFields& fields) {
        enum class Operation { None, Duplicate, MoveUp, MoveDown, Remove };
        Operation operation = Operation::None;
        size_t target = 0;
        bool edited = false;
        for (size_t index = 0; index < list.size(); ++index) {
            const std::string path = id + "[" + std::to_string(index) + "]";
            if (!builder.Section(ElementTitle(list[index], fields, label, index) + "###" + path, true)) {
                continue;
            }
            edited |= InspectTable(builder, list[index], fields, path + ".");
            // 押された操作は 1 つだけ覚え、ループの後で配列を変える。
            const auto pressed = [&](const char* text, const char* suffix, Operation next, bool available) {
                if (available && builder.Button(std::string(text) + "###" + path + suffix)) {
                    operation = next;
                    target = index;
                }
            };
            pressed("Duplicate", ".duplicate", Operation::Duplicate, true);
            pressed("Move Up", ".up", Operation::MoveUp, index > 0);
            pressed("Move Down", ".down", Operation::MoveDown, index + 1 < list.size());
            pressed("Remove", ".remove", Operation::Remove, true);
        }
        const auto at = [&list](size_t index) { return list.begin() + static_cast<std::ptrdiff_t>(index); };
        switch (operation) {
        case Operation::Duplicate: {
            E copy = list[target];
            list.insert(at(target + 1u), std::move(copy));
            break;
        }
        case Operation::MoveUp: std::swap(list[target], list[target - 1u]); break;
        case Operation::MoveDown: std::swap(list[target], list[target + 1u]); break;
        case Operation::Remove: list.erase(at(target)); break;
        case Operation::None: break;
        }
        edited |= operation != Operation::None;
        if (builder.Button("Add " + label + "###" + id + ".add")) {
            list.emplace_back();
            edited = true;
        }
        return edited;
    }

    // "Label (unit)" の形の表示名。
    static std::string DisplayLabel(const JSONFIELDS::Meta& meta, const char* key) {
        std::string label = JSONFIELDS::LabelOf(meta, key);
        if (*meta.unit != '\0') {
            label.append(" (").append(meta.unit).append(")");
        }
        return label;
    }

    // 項目の記述子。文字列は呼び出しの間だけ生きる id と label が持つ。
    static AuthoringFieldDescriptor FieldOf(const std::string& id, const std::string& label,
                                            const JSONFIELDS::Meta& meta) {
        return {id, label, meta.help, !meta.readOnly, meta.hasRange && meta.range.slider};
    }

    template <class C, class M, class... Options>
        requires std::is_base_of_v<C, T>
    void operator()(const char* key, M C::* member, const Options&... options) {
        JSONFIELDS::Meta meta;
        if (!Select(meta, options...)) {
            return;
        }
        const std::string id = prefix + key;
        [[maybe_unused]] const std::string label = DisplayLabel(meta, key);
        [[maybe_unused]] const AuthoringFieldDescriptor field = FieldOf(id, label, meta);
        constexpr JSONFIELDS::FieldKind kind = JSONFIELDS::KindOf<M, Options...>();
        if constexpr ((JSONFIELDS::DETAIL::kIsDraw<Options> || ...)) {
            changed |= JSONFIELDS::DrawOf(options...)(builder, field, object.*member, std::as_const(object));
        } else if constexpr (kind == JSONFIELDS::FieldKind::Elements) {
            changed |=
                EditElements(id, JSONFIELDS::LabelOf(meta, key), object.*member, JSONFIELDS::SubFields(options...));
        } else if constexpr (kind == JSONFIELDS::FieldKind::Nested) {
            changed |= InspectTable(builder, object.*member, JSONFIELDS::SubFields(options...), id + ".");
        } else {
            changed |= EditLeaf<kind>(field, meta, JSONFIELDS::Unwrap(object.*member), options...);
        }
    }

    // 列挙は表の名前を選択肢にする（表示は語に分けた名前）。
    template <class C, class E, std::size_t N, class... Options>
        requires std::is_base_of_v<C, T>
    void operator()(const char* key, E C::* member, [[maybe_unused]] const TEXT::EnumName<E> (&names)[N],
                    const Options&... options) {
        JSONFIELDS::Meta meta;
        if (!Select(meta, options...)) {
            return;
        }
        const std::string id = prefix + key;
        const std::string label = DisplayLabel(meta, key);
        const AuthoringFieldDescriptor field = FieldOf(id, label, meta);
        if constexpr ((JSONFIELDS::DETAIL::kIsDraw<Options> || ...)) {
            changed |= JSONFIELDS::DrawOf(options...)(builder, field, object.*member, std::as_const(object));
        } else {
            std::array<std::string, N> text{};
            std::array<std::string_view, N> labels{};
            for (std::size_t index = 0; index < N; ++index) {
                text[index] = TEXT::HumanizeKey(names[index].name);
                labels[index] = text[index];
            }
            changed |= EnumChoice(builder, field, object.*member, names, &labels);
        }
    }
};

template <class T, class Fields>
bool InspectTable(IInspectorBuilder& builder, T& object, const Fields& fields, const std::string& prefix) {
    bool changed = false;
    for (const Phase phase : {Phase::Common, Phase::Normal, Phase::Advanced}) {
        fields(Drawer<T>{builder, object, prefix, phase, changed});
    }
    return changed;
}

} // namespace FIELD_INSPECTOR

template <class T, class Fields>
bool InspectFields(IInspectorBuilder& builder, T& object, const Fields& fields) {
    return FIELD_INSPECTOR::InspectTable(builder, object, fields, std::string{});
}

} // namespace HIKARI
