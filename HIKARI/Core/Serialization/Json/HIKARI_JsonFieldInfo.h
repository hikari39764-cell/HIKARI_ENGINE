#pragma once

#include <cstdint>
#include <string>
#include <vector>

// JSONFIELDS::Describe が項目表から作る静的な説明。組件の型情報（ComponentTypeInfo::fields）が持ち、
// 智能体の説明・検索・以後の言語バインディングが読む。作り方は HIKARI_JsonFields.h。
namespace HIKARI::JSONFIELDS {

enum class FieldKind : uint8_t {
    Bool,
    Int,
    Float,
    Vec2,
    Vec3,
    String,
    // 名前の表を持つ列挙。保存名は enumNames。
    Enum,
    // 資産の GUID。種類は assetType（AssetType の値）。
    AssetRef,
    // SceneObjectId。
    ObjectRef,
    // 実行時イベントの ID（std::string）。
    EventRef,
    // 実行時変数の ID（std::string）。
    VariableRef,
    // std::vector<構造体>（子項目は children）。
    Elements,
    // 入れ子の構造体（子項目は children）。
    Nested,
};

struct FieldInfo {
    // JSON のキーであり、パス指定と検査器の項目 ID の元（配列の中は slots[0].name のように添字が付く）。
    std::string id{};
    // 英語の原文。Label が無ければ id から作る。
    std::string label{};
    std::string help{};
    std::string unit{};
    std::string group{};
    FieldKind kind = FieldKind::Bool;
    bool hasRange = false;
    double minimum = 0.0;
    double maximum = 0.0;
    bool slider = false;
    bool common = false;
    bool advanced = false;
    bool readOnly = false;
    // 検査器には出さない保存項目。
    bool hidden = false;
    // Vec3 を色として編集する。
    bool color = false;
    // 条件付きで出す項目（条件そのものは検査器の表だけが持つ）。
    bool hasShow = false;
    int assetType = -1;
    std::vector<std::string> enumNames{};
    std::vector<FieldInfo> children{};
};

} // namespace HIKARI::JSONFIELDS
