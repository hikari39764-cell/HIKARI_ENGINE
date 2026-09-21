#include "Editor/Workspaces/ModelMaterial/HIKARI_ModelMaterialGraphUi.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>
#include <unordered_set>

namespace HIKARI::EDITOR::MATERIAL_GRAPH_UI {
namespace {
constexpr float kColumnGap = 100.0f;
constexpr float kRowGap = 54.0f;
constexpr float kIslandGap = 120.0f;
constexpr float kGroupPadding = 32.0f;
struct Box {
  MATH::Vec2 position{}, size{};
};
using Links = std::vector<std::pair<size_t, size_t>>;

// 入力順は安定 ID 順。壊れた循環も有限回で配置し、接続自体は変更しない。
void ArrangeBoxes(std::vector<Box> &boxes, const Links &links) {
  const size_t count = boxes.size();
  std::vector<std::vector<size_t>> incoming(count), outgoing(count), adjacent(count);
  for (const auto [a, b] : links) {
    if (a == b)
      continue;
    outgoing[a].push_back(b);
    incoming[b].push_back(a);
    adjacent[a].push_back(b);
    adjacent[b].push_back(a);
  }
  std::vector<bool> visited(count), placed(count);
  std::vector<size_t> depth(count), degree(count), order(count);
  float islandY = 0.0f;
  for (size_t seed = 0; seed < count; ++seed) {
    if (visited[seed])
      continue;
    std::vector<size_t> component{seed};
    visited[seed] = true;
    for (size_t i = 0; i < component.size(); ++i) {
      for (size_t next : adjacent[component[i]]) {
        if (!visited[next]) {
          visited[next] = true;
          component.push_back(next);
        }
      }
    }
    std::sort(component.begin(), component.end());
    std::set<size_t> ready;
    for (size_t id : component) {
      degree[id] = incoming[id].size();
      if (degree[id] == 0)
        ready.insert(id);
    }
    for (size_t step = 0; step < component.size(); ++step) {
      if (ready.empty()) {
        ready.insert(*std::find_if(component.begin(), component.end(),
                                   [&](size_t id) { return !placed[id]; }));
      }
      const size_t id = *ready.begin();
      ready.erase(ready.begin());
      placed[id] = true;
      for (size_t next : outgoing[id]) {
        if (placed[next])
          continue;
        depth[next] = (std::max)(depth[next], depth[id] + 1);
        if (degree[next] > 0 && --degree[next] == 0)
          ready.insert(next);
      }
    }
    std::map<size_t, std::vector<size_t>> columns;
    for (size_t id : component)
      columns[depth[id]].push_back(id);
    const auto updateOrder = [&] {
      for (const auto &[column, ids] : columns)
        for (size_t i = 0; i < ids.size(); ++i)
          order[ids[i]] = i;
    };
    updateOrder();
    // 隣接列の重心で順序を改善。ID で同点を解決し、再実行を安定させる。
    for (int pass = 0; pass < 4; ++pass) {
      const auto sortColumn = [&](std::vector<size_t> &ids, bool forward) {
        const auto score = [&](size_t id) {
          const auto &neighbors = forward ? incoming[id] : outgoing[id];
          float sum = 0.0f;
          size_t n = 0;
          for (size_t v : neighbors) {
            if (forward ? depth[v] < depth[id] : depth[v] > depth[id]) {
              sum += static_cast<float>(order[v]);
              ++n;
            }
          }
          return n ? sum / static_cast<float>(n) : static_cast<float>(order[id]);
        };
        std::vector<std::pair<float, size_t>> scored;
        for (size_t id : ids)
          scored.emplace_back(score(id), id);
        std::sort(scored.begin(), scored.end());
        for (size_t i = 0; i < ids.size(); ++i)
          ids[i] = scored[i].second;
        updateOrder();
      };
      for (auto &[column, ids] : columns)
        sortColumn(ids, true);
      for (auto it = columns.rbegin(); it != columns.rend(); ++it)
        sortColumn(it->second, false);
    }
    float height = 0.0f;
    for (const auto &[column, ids] : columns) {
      float h = -kRowGap;
      for (size_t id : ids)
        h += boxes[id].size.y + kRowGap;
      height = (std::max)(height, h);
    }
    float x = 0.0f;
    for (const auto &[column, ids] : columns) {
      float width = 0.0f, h = -kRowGap;
      for (size_t id : ids) {
        width = (std::max)(width, boxes[id].size.x);
        h += boxes[id].size.y + kRowGap;
      }
      float y = islandY + (height - h) * 0.5f;
      for (size_t id : ids) {
        boxes[id].position = {x, y};
        y += boxes[id].size.y + kRowGap;
      }
      x += width + kColumnGap;
    }
    islandY += height + kIslandGap;
  }
}
} // namespace

MATH::Vec2 NodeLayoutSize(const MaterialGraphData &graph, const MaterialGraphNodeData &node) {
  // 編集 LOD ではウィジェットのピクセル寸法を維持するため、その最小ズームで余白を確保。
  return {kNodeWidth / kInlineControlsMinimumZoom,
          NodeLogicalHeight(graph, node, false) / kInlineControlsMinimumZoom};
}

bool AutoLayoutMaterialGraph(MaterialGraphData &graph, const std::vector<std::string> &selection) {
  const std::unordered_set<std::string> selected(selection.begin(), selection.end());
  std::vector<MaterialGraphNodeData *> nodes;
  for (auto &node : graph.nodes)
    if (selection.empty() || selected.contains(node.id))
      nodes.push_back(&node);
  if (nodes.empty())
    return false;
  std::sort(nodes.begin(), nodes.end(), [](const auto *a, const auto *b) { return a->id < b->id; });
  std::unordered_map<std::string, size_t> index;
  std::vector<Box> boxes;
  MATH::Vec2 anchor{(std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)()};
  for (size_t i = 0; i < nodes.size(); ++i) {
    index.emplace(nodes[i]->id, i);
    boxes.push_back({{}, NodeLayoutSize(graph, *nodes[i])});
    if (std::isfinite(nodes[i]->position.x))
      anchor.x = (std::min)(anchor.x, nodes[i]->position.x);
    if (std::isfinite(nodes[i]->position.y))
      anchor.y = (std::min)(anchor.y, nodes[i]->position.y);
  }
  if (selection.empty() || anchor.x == (std::numeric_limits<float>::max)() ||
      anchor.y == (std::numeric_limits<float>::max)())
    anchor = {40.0f, 40.0f};
  Links links;
  for (const auto &edge : graph.edges) {
    const auto a = index.find(edge.sourceNodeId), b = index.find(edge.targetNodeId);
    if (a != index.end() && b != index.end())
      links.emplace_back(a->second, b->second);
  }
  std::sort(links.begin(), links.end());
  links.erase(std::unique(links.begin(), links.end()), links.end());
  // グループの重複所属は同じ配置ブロックとして扱う。
  std::vector<size_t> parents(nodes.size());
  std::iota(parents.begin(), parents.end(), size_t{0});
  const auto root = [&](size_t id) {
    while (parents[id] != id)
      id = parents[id];
    return id;
  };
  for (const auto &group : graph.groups) {
    size_t first = nodes.size();
    for (const auto &id : group.nodeIds) {
      const auto found = index.find(id);
      if (found == index.end())
        continue;
      if (first == nodes.size())
        first = found->second;
      const size_t a = root(first), b = root(found->second);
      parents[(std::max)(a, b)] = (std::min)(a, b);
    }
  }
  std::map<size_t, std::vector<size_t>> clusters;
  for (size_t i = 0; i < nodes.size(); ++i)
    clusters[root(i)].push_back(i);
  std::vector<size_t> blockIndex(nodes.size());
  std::vector<Box> blocks;
  for (const auto &[key, ids] : clusters) {
    std::unordered_map<size_t, size_t> localIndex;
    std::vector<Box> local;
    for (size_t id : ids) {
      blockIndex[id] = blocks.size();
      localIndex[id] = local.size();
      local.push_back(boxes[id]);
    }
    Links localLinks;
    for (const auto [a, b] : links)
      if (localIndex.contains(a) && localIndex.contains(b))
        localLinks.emplace_back(localIndex[a], localIndex[b]);
    ArrangeBoxes(local, localLinks);
    Box block;
    for (size_t i = 0; i < ids.size(); ++i) {
      boxes[ids[i]].position = {local[i].position.x + kGroupPadding,
                                local[i].position.y + kGroupPadding};
      block.size.x =
          (std::max)(block.size.x, local[i].position.x + local[i].size.x + 2 * kGroupPadding);
      block.size.y =
          (std::max)(block.size.y, local[i].position.y + local[i].size.y + 2 * kGroupPadding);
    }
    blocks.push_back(block);
  }
  Links blockLinks;
  for (const auto [a, b] : links)
    if (blockIndex[a] != blockIndex[b])
      blockLinks.emplace_back(blockIndex[a], blockIndex[b]);
  std::sort(blockLinks.begin(), blockLinks.end());
  blockLinks.erase(std::unique(blockLinks.begin(), blockLinks.end()), blockLinks.end());
  ArrangeBoxes(blocks, blockLinks);
  MATH::Vec2 minimum{(std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)()};
  for (size_t i = 0; i < boxes.size(); ++i) {
    boxes[i].position.x += blocks[blockIndex[i]].position.x;
    boxes[i].position.y += blocks[blockIndex[i]].position.y;
    minimum.x = (std::min)(minimum.x, boxes[i].position.x);
    minimum.y = (std::min)(minimum.y, boxes[i].position.y);
  }
  for (auto &box : boxes) {
    box.position.x += anchor.x - minimum.x;
    box.position.y += anchor.y - minimum.y;
  }
  // 選択外は固定。衝突時は選択ブロック全体を下へ移動する。
  if (!selection.empty()) {
    float offset = 0.0f;
    for (size_t pass = 0; pass <= graph.nodes.size(); ++pass) {
      float nextOffset = offset;
      for (const auto &other : graph.nodes) {
        if (index.contains(other.id))
          continue;
        const auto size = NodeLayoutSize(graph, other);
        for (const auto &box : boxes) {
          if (box.position.x < other.position.x + size.x + kRowGap &&
              box.position.x + box.size.x + kRowGap > other.position.x &&
              box.position.y + offset < other.position.y + size.y + kRowGap &&
              box.position.y + offset + box.size.y + kRowGap > other.position.y)
            nextOffset =
                (std::max)(nextOffset, other.position.y + size.y + kRowGap - box.position.y);
        }
      }
      if (nextOffset == offset)
        break;
      offset = nextOffset;
    }
    for (auto &box : boxes)
      box.position.y += offset;
  }
  bool changed = false;
  for (size_t i = 0; i < nodes.size(); ++i) {
    changed |=
        nodes[i]->position.x != boxes[i].position.x || nodes[i]->position.y != boxes[i].position.y;
    nodes[i]->position = boxes[i].position;
  }
  for (auto &group : graph.groups) {
    MATH::Vec2 lo{(std::numeric_limits<float>::max)(), (std::numeric_limits<float>::max)()};
    MATH::Vec2 hi{-(std::numeric_limits<float>::max)(), -(std::numeric_limits<float>::max)()};
    bool any = false, affected = selection.empty();
    for (const auto &id : group.nodeIds) {
      const auto *node = FindNode(graph, id);
      if (!node)
        continue;
      any = true;
      affected |= selected.contains(id);
      const auto extent = NodeLayoutSize(graph, *node);
      lo.x = (std::min)(lo.x, node->position.x);
      lo.y = (std::min)(lo.y, node->position.y);
      hi.x = (std::max)(hi.x, node->position.x + extent.x);
      hi.y = (std::max)(hi.y, node->position.y + extent.y);
    }
    if (!any || !affected)
      continue;
    const MATH::Vec2 position{lo.x - kGroupPadding, lo.y - kGroupPadding};
    const MATH::Vec2 size{hi.x - lo.x + 2 * kGroupPadding, hi.y - lo.y + 2 * kGroupPadding};
    changed |= group.position.x != position.x || group.position.y != position.y ||
               group.size.x != size.x || group.size.y != size.y;
    group.position = position;
    group.size = size;
  }
  return changed;
}
} // namespace HIKARI::EDITOR::MATERIAL_GRAPH_UI
