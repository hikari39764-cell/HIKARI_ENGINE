#include "Assets/Collision/HIKARI_ModelCollisionGrouping.h"

#include <algorithm>
#include <numeric>
#include <unordered_map>
#include <unordered_set>

#include "Assets/Collision/HIKARI_ModelCollisionMeshExtraction.h"
#include "Render3D/Core/HIKARI_BoundsUtils.h"

namespace HIKARI::ASSETS::COLLISION {
    namespace {
        struct NodeBounds {
            int32_t nodeIndex = -1;
            Bounds bounds{};
        };

        float AxisDistance(
            float firstMin,
            float firstMax,
            float secondMin,
            float secondMax) noexcept {

            if (firstMax < secondMin) {
                return secondMin - firstMax;
            }
            if (secondMax < firstMin) {
                return firstMin - secondMax;
            }
            return 0.0f;
        }

        bool BoundsAreNearby(
            const Bounds& first,
            const Bounds& second,
            float distance) noexcept {

            const float dx = AxisDistance(
                first.min.x, first.max.x, second.min.x, second.max.x);
            if (dx > distance) {
                return false;
            }
            const float dy = AxisDistance(
                first.min.y, first.max.y, second.min.y, second.max.y);
            const float dz = AxisDistance(
                first.min.z, first.max.z, second.min.z, second.max.z);
            return dx * dx + dy * dy + dz * dz <= distance * distance;
        }

        std::vector<NodeBounds> BuildNodeBounds(
            const ModelAsset& model,
            std::span<const int32_t> requestedNodes) {

            const std::vector<int32_t> candidates = requestedNodes.empty()
                ? CollectRenderableModelNodeIndices(model)
                : std::vector<int32_t>(
                    requestedNodes.begin(),
                    requestedNodes.end());
            const std::vector<MATH::Mat4> globals =
                BOUNDS::BuildModelNodeGlobals(model);
            std::unordered_set<int32_t> unique{};
            std::vector<NodeBounds> result{};
            result.reserve(candidates.size());
            for (int32_t nodeIndex : candidates) {
                if (nodeIndex < 0 ||
                    nodeIndex >= static_cast<int32_t>(model.nodes.size()) ||
                    !unique.insert(nodeIndex).second) {
                    continue;
                }
                const Bounds bounds = BOUNDS::ComputeModelNodeBounds(
                    model,
                    static_cast<size_t>(nodeIndex),
                    globals);
                if (BOUNDS::IsUsable(bounds)) {
                    result.push_back({ nodeIndex, bounds });
                }
            }
            return result;
        }

        std::vector<ModelCollisionSourceGroup> BuildSpatialGroups(
            std::vector<NodeBounds> nodes,
            float mergeDistance) {

            std::sort(
                nodes.begin(),
                nodes.end(),
                [](const NodeBounds& left, const NodeBounds& right) {
                    return left.bounds.min.x < right.bounds.min.x;
                });
            std::vector<size_t> parent(nodes.size());
            std::iota(parent.begin(), parent.end(), 0u);
            const auto findRoot = [&parent](size_t value) {
                size_t root = value;
                while (parent[root] != root) {
                    root = parent[root];
                }
                while (parent[value] != value) {
                    const size_t next = parent[value];
                    parent[value] = root;
                    value = next;
                }
                return root;
            };
            for (size_t first = 0u; first < nodes.size(); ++first) {
                for (size_t second = first + 1u;
                    second < nodes.size();
                    ++second) {
                    if (nodes[second].bounds.min.x -
                            nodes[first].bounds.max.x > mergeDistance) {
                        break;
                    }
                    if (!BoundsAreNearby(
                            nodes[first].bounds,
                            nodes[second].bounds,
                            mergeDistance)) {
                        continue;
                    }
                    const size_t firstRoot = findRoot(first);
                    const size_t secondRoot = findRoot(second);
                    if (firstRoot != secondRoot) {
                        parent[secondRoot] = firstRoot;
                    }
                }
            }

            std::unordered_map<size_t, ModelCollisionSourceGroup> grouped{};
            for (size_t index = 0u; index < nodes.size(); ++index) {
                ModelCollisionSourceGroup& group = grouped[findRoot(index)];
                group.nodeIndices.push_back(nodes[index].nodeIndex);
                group.sourceBoundsVolume += BOUNDS::ComputeVolume(nodes[index].bounds);
            }
            std::vector<ModelCollisionSourceGroup> result{};
            result.reserve(grouped.size());
            for (auto& [root, group] : grouped) {
                (void)root;
                std::sort(group.nodeIndices.begin(), group.nodeIndices.end());
                result.push_back(std::move(group));
            }
            std::sort(
                result.begin(),
                result.end(),
                [](const auto& left, const auto& right) {
                    return left.nodeIndices.front() < right.nodeIndices.front();
                });
            return result;
        }
    }

    std::vector<ModelCollisionSourceGroup> BuildModelCollisionSourceGroups(
        const ModelAsset& model,
        ModelCollisionGroupingMode mode,
        std::span<const int32_t> selectedNodeIndices,
        float mergeDistance) {

        const std::span<const int32_t> requested =
            mode == ModelCollisionGroupingMode::AllCombined
            ? std::span<const int32_t>{}
            : selectedNodeIndices;
        std::vector<NodeBounds> nodes = BuildNodeBounds(model, requested);
        if (nodes.empty()) {
            return {};
        }
        if (mode == ModelCollisionGroupingMode::SelectedIndividually) {
            std::vector<ModelCollisionSourceGroup> groups{};
            groups.reserve(nodes.size());
            for (const NodeBounds& node : nodes) {
                groups.push_back({
                    { node.nodeIndex },
                    BOUNDS::ComputeVolume(node.bounds)
                });
            }
            return groups;
        }
        if (mode == ModelCollisionGroupingMode::SelectedSpatialGroups) {
            return BuildSpatialGroups(
                std::move(nodes),
                (std::max)(mergeDistance, 0.0f));
        }

        ModelCollisionSourceGroup combined{};
        combined.nodeIndices.reserve(nodes.size());
        for (const NodeBounds& node : nodes) {
            combined.nodeIndices.push_back(node.nodeIndex);
            combined.sourceBoundsVolume += BOUNDS::ComputeVolume(node.bounds);
        }
        std::sort(
            combined.nodeIndices.begin(),
            combined.nodeIndices.end());
        return { std::move(combined) };
    }

} // namespace HIKARI::ASSETS::COLLISION
