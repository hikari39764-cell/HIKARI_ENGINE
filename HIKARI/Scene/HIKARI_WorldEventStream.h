#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace HIKARI {

    class WorldEventStream {
    public:
        void BeginScope(uint64_t scopeIndex) noexcept {
            scopeIndex_ = scopeIndex;
            for (auto& [_, channel] : channels_) {
                channel->Clear();
            }
        }

        template<class Event>
        void Publish(Event event) {
            using EventType = std::remove_cvref_t<Event>;
            GetOrCreateChannel<EventType>().events.push_back(
                std::move(event));
        }

        template<class Event, class... Args>
        const Event& Emplace(Args&&... args) {
            auto& events = GetOrCreateChannel<Event>().events;
            events.emplace_back(std::forward<Args>(args)...);
            return events.back();
        }

        template<class Event>
        std::span<const Event> Read() const noexcept {
            const Channel<Event>* channel = FindChannel<Event>();
            return channel != nullptr
                ? std::span<const Event>(channel->events)
                : std::span<const Event>{};
        }

        template<class Event>
        bool HasEvents() const noexcept {
            return !Read<Event>().empty();
        }

        uint64_t GetScopeIndex() const noexcept {
            return scopeIndex_;
        }

        void Clear() noexcept {
            channels_.clear();
            scopeIndex_ = 0;
        }

    private:
        struct IChannel {
            virtual ~IChannel() = default;
            virtual void Clear() noexcept = 0;
        };

        template<class Event>
        struct Channel final : IChannel {
            void Clear() noexcept override {
                events.clear();
            }
            std::vector<Event> events{};
        };

        template<class Event>
        Channel<Event>& GetOrCreateChannel() {
            const std::type_index key(typeid(Event));
            const auto found = channels_.find(key);
            if (found != channels_.end()) {
                return *static_cast<Channel<Event>*>(found->second.get());
            }
            auto channel = std::make_unique<Channel<Event>>();
            Channel<Event>* result = channel.get();
            channels_.emplace(key, std::move(channel));
            return *result;
        }

        template<class Event>
        const Channel<Event>* FindChannel() const noexcept {
            const auto it = channels_.find(
                std::type_index(typeid(Event)));
            return it == channels_.end()
                ? nullptr
                : static_cast<const Channel<Event>*>(it->second.get());
        }

        std::unordered_map<std::type_index, std::unique_ptr<IChannel>>
            channels_{};
        uint64_t scopeIndex_ = 0;
    };

} // namespace HIKARI
