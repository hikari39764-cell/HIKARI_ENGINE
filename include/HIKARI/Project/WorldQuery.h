#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>

#include "HIKARI/Project/GameComponent.h"
#include "HIKARI/Project/WorldView.h"

namespace HIKARI::PROJECT::DETAIL {

    template<class... Components, std::size_t... Indices>
    std::tuple<Components*...> CastWorldQueryComponents(
        const GameProjectWorldQueryRow& row,
        std::index_sequence<Indices...>) noexcept {
        return std::tuple<Components*...>{
            dynamic_cast<Components*>(row.components[Indices])...
        };
    }

} // namespace HIKARI::PROJECT::DETAIL

namespace HIKARI {

    template<class... Components, class Visitor>
    GameProjectWorldQueryResult WorldView::ForEachComponents(
        const ComponentTypeId* typeIds,
        GameProjectWorldQueryAnchor anchor,
        Visitor&& visitor) const {
        static_assert(sizeof...(Components) > 0u);
        static_assert(sizeof...(Components) <=
            kGameProjectWorldQueryMaximumTermCount);
        static_assert((std::is_base_of_v<
            IGameComponent, Components> && ...));
        using Callback = std::decay_t<Visitor>;
        using CallbackResult = std::invoke_result_t<
            Callback&, ObjectView&, Components&...>;
        static_assert(std::is_void_v<CallbackResult> ||
            std::is_same_v<CallbackResult, bool> ||
            std::is_same_v<CallbackResult,
                GameProjectWorldQueryVisitResult>);

        if (typeIds == nullptr) {
            GameProjectWorldQueryResult result{};
            result.code = GameProjectWorldQueryResultCode::InvalidRequest;
            return result;
        }

        std::array<GameProjectWorldQueryTerm,
            sizeof...(Components)> terms{};
        for (std::size_t index = 0u; index < terms.size(); ++index) {
            terms[index].typeId = typeIds[index];
        }
        GameProjectWorldQueryDesc desc{};
        desc.terms = terms.data();
        desc.termCount = static_cast<uint32_t>(terms.size());
        desc.cardinality = terms.size() == 1u
            ? GameProjectWorldQueryCardinality::AllEntries
            : GameProjectWorldQueryCardinality::FirstPerObject;
        desc.anchor = anchor;

        Callback callback = std::forward<Visitor>(visitor);
        struct VisitContext {
            const WorldView* world = nullptr;
            Callback* callback = nullptr;
        } context{ this, std::addressof(callback) };
        return QueryComponents(
            desc,
            &context,
            [](void* opaque,
                const GameProjectWorldQueryRow* row) noexcept {
                auto& visit = *static_cast<VisitContext*>(opaque);
                if (row == nullptr || row->components == nullptr ||
                    row->componentCount != sizeof...(Components)) {
                    return GameProjectWorldQueryVisitResult::Error;
                }
                auto typed =
                    PROJECT::DETAIL::CastWorldQueryComponents<Components...>(
                        *row,
                        std::index_sequence_for<Components...>{});
                const bool typesMatch = std::apply(
                    [](auto*... components) {
                        return ((components != nullptr) && ...);
                    },
                    typed);
                ObjectView object = visit.world->FindObject(row->object);
                if (!typesMatch || !object.IsValid()) {
                    return GameProjectWorldQueryVisitResult::Error;
                }
                try {
                    return std::apply(
                        [&](auto*... components) {
                            if constexpr (std::is_void_v<CallbackResult>) {
                                (*visit.callback)(object, *components...);
                                return GameProjectWorldQueryVisitResult::
                                    Continue;
                            }
                            else if constexpr (std::is_same_v<
                                    CallbackResult, bool>) {
                                return (*visit.callback)(
                                    object, *components...)
                                    ? GameProjectWorldQueryVisitResult::Continue
                                    : GameProjectWorldQueryVisitResult::Stop;
                            }
                            else {
                                return (*visit.callback)(
                                    object, *components...);
                            }
                        },
                        typed);
                }
                catch (...) {
                    return GameProjectWorldQueryVisitResult::Error;
                }
            });
    }

} // namespace HIKARI
