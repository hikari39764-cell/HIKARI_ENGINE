#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "HIKARI/Project/WorldInstanceTypes.h"
#include "HIKARI/Runtime/ComponentTypeId.h"
#include "Scene/HIKARI_WorldMassStore.h"
#include "Scene/Partition/HIKARI_ScenePartition.h"

namespace HIKARI {

bool IsValidWorldInstanceSetDesc(const GameProjectWorldInstanceSetDesc& desc) noexcept;

// 場景の実例集。場景の文書と同じ寿命で、編集と Play が同じものを読む（Play で世界を作り直しても消えない）。
// 個体は格子ごとに持ち、保存は変わった格子だけを書く。描画へは WorldMass と同じ形で渡す。
class WorldInstanceStore {
public:
    struct SaveReport {
        size_t writtenCells = 0u;
        size_t removedCells = 0u;
        bool setsWritten = false;
    };

    // 同じ名前の実例集があれば定義を置き換える（個体は残る）。
    bool AddSet(const GameProjectWorldInstanceSetDesc& desc, ComponentTypeId* outId = nullptr);
    bool RemoveSet(ComponentTypeId id);
    const GameProjectWorldInstanceSetDesc* FindSet(ComponentTypeId id) const noexcept;
    // 足した順に ID を書き、全部の数を返す。
    uint32_t CopySetIds(std::span<ComponentTypeId> outIds) const noexcept;

    // 一格の個体を丸ごと置き換える。位置が別の格子でも構わない（格子は保存の単位で、描画には関係しない）。
    bool ReplaceCell(ComponentTypeId id, WorldCellCoord cell, std::span<const GameProjectWorldInstance> instances);
    std::span<const GameProjectWorldInstance> ReadCell(ComponentTypeId id, WorldCellCoord cell) const noexcept;
    uint64_t CountOf(ComponentTypeId id) const noexcept;
    uint64_t TotalCount() const noexcept;
    uint64_t Revision() const noexcept { return revision_; }
    bool HasUnsavedChanges() const noexcept { return setsDirty_ || !dirtyCells_.empty(); }

    bool Load(const std::filesystem::path& cellDirectory, std::string& outError);
    // 同じ場所へ前に読み書きしていれば変わった格子だけを書く。別の場所なら全部書く。
    bool Save(const std::filesystem::path& cellDirectory, SaveReport& outReport, std::string& outError);
    void Clear() noexcept;

    // 変わった格子を実例集の描画用の配列へ映して、発布用の見え方を返す。変わっていなければ配列には触らない。
    // 配列は格子ごとの区画に余りを持たせてあり、個体の増減は変わった範囲（dirtyRanges）だけで伝わる。配列の大きさが
    // 変わるのは余りを使い切った時と空きを詰め直す時だけ（その時だけ layoutVersion が進む）。
    void CollectRenderViews(std::vector<WorldMassRenderStoreView>& outViews);

private:
    // 描画用の配列の中の一格の区画。個体は first から count 個で、capacity までは隠した空き。
    struct RenderBlock {
        uint32_t first = 0u;
        uint32_t capacity = 0u;
        uint32_t count = 0u;
    };
    struct Set {
        GameProjectWorldInstanceSetDesc desc{};
        GameProjectWorldMassRenderProxyDesc proxy{};
        GameProjectWorldMassRenderProxyDesc farProxy{};
        std::map<WorldCellCoord, std::vector<GameProjectWorldInstance>> cells{};
        std::vector<WorldMassRenderInstance> renderInstances{};
        std::map<WorldCellCoord, RenderBlock> renderBlocks{};
        std::set<WorldCellCoord> renderPending{}; // 描画用の配列へまだ映していない格子
        // 区画から溢れたことのある格子（筆で個体を足し続けている所）。並べ直す時に余りを多めに取る。
        std::set<WorldCellCoord> renderGrowing{};
        // renderBase の版から renderVersion の版までに変わった範囲。
        std::vector<WorldMassRenderDirtyRange> renderDirtyRanges{};
        uint32_t renderUsed = 0u;      // 区画に割り当てた長さ（配列の先頭から）
        uint32_t renderAbandoned = 0u; // 引っ越しや削除で捨てた区画の長さ
        uint64_t count = 0u;
        uint64_t renderLayout = 0u;  // 配列の大きさか区画の並びが変わるたびに進む
        uint64_t renderVersion = 0u; // 配列の中身が変わるたびに進む
        uint64_t renderBase = 0u;
        bool renderRebuild = true; // 区画を一から並べ直す（定義が変わった、余りが尽きた、空きが増えすぎた）
    };

    static void RebuildRenderArray(Set& set);
    static void PatchRenderArray(Set& set);

    std::unordered_map<uint64_t, Set> sets_{};
    std::vector<uint64_t> order_{}; // 追加した順（描画の並びが毎回同じになるように）
    std::set<WorldCellCoord> dirtyCells_{};
    std::filesystem::path savedDirectory_{};
    uint64_t revision_ = 1u;
    bool setsDirty_ = false;
};

} // namespace HIKARI
