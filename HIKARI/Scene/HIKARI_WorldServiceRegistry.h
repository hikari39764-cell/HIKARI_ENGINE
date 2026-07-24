#pragma once

#include <cstddef>
#include <type_traits>
#include <typeindex>
#include <unordered_map>

namespace HIKARI {

    class WorldServiceRegistry {
    public:
        template<class Service>
        bool Register(Service& service) {
            static_assert(
                !std::is_const_v<Service>,
                "World services must be mutable owner-managed objects");
            using ServiceType = std::remove_cv_t<Service>;
            const auto [it, inserted] = services_.emplace(
                std::type_index(typeid(ServiceType)),
                &service);
            return inserted || it->second == &service;
        }

        template<class Service>
        bool Remove(const Service* expectedService = nullptr) noexcept {
            using ServiceType = std::remove_cv_t<Service>;
            const auto it = services_.find(
                std::type_index(typeid(ServiceType)));
            if (it == services_.end()) {
                return false;
            }
            if (expectedService != nullptr &&
                static_cast<const void*>(it->second) !=
                    static_cast<const void*>(expectedService)) {
                return false;
            }
            services_.erase(it);
            return true;
        }

        template<class Service>
        Service* Find() noexcept {
            using ServiceType = std::remove_cv_t<Service>;
            const auto it = services_.find(
                std::type_index(typeid(ServiceType)));
            return it == services_.end()
                ? nullptr
                : static_cast<ServiceType*>(it->second);
        }

        template<class Service>
        const Service* Find() const noexcept {
            using ServiceType = std::remove_cv_t<Service>;
            const auto it = services_.find(
                std::type_index(typeid(ServiceType)));
            return it == services_.end()
                ? nullptr
                : static_cast<const ServiceType*>(it->second);
        }

        template<class Service>
        bool Contains() const noexcept {
            return Find<Service>() != nullptr;
        }

        void Clear() noexcept {
            services_.clear();
        }

        size_t Size() const noexcept {
            return services_.size();
        }

    private:
        std::unordered_map<std::type_index, void*> services_{};
    };

} // namespace HIKARI
