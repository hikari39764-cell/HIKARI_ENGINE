#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
#include "Components/HIKARI_IComponent.h"
#include "HIKARI_RuntimeObjectHandle.h"
#include "HIKARI_SceneDocument.h"
#include "Render3D/HIKARI_Transform3D.h"

namespace HIKARI {

    class World;

    class GameObject {
    public:
        explicit GameObject(std::string name);

        const std::string& GetName() const;
        void SetName(const std::string& name);
        void SetDocumentId(SceneObjectId id);
        SceneObjectId GetDocumentId() const;
        RuntimeObjectHandle GetRuntimeHandle() const noexcept;
        World* GetWorld() noexcept;
        const World* GetWorld() const noexcept;

        const Transform3D& GetTransform() const noexcept;
        bool SetLocalTransform(const Transform3D& transform);
        bool SetLocalPosition(const MATH::Vec3& position);
        bool SetLocalRotation(const MATH::Quat& rotation);
        bool SetLocalScale(const MATH::Vec3& scale);

        GameObject* GetParent() noexcept;
        const GameObject* GetParent() const noexcept;
        const std::vector<GameObject*>& GetChildren() const noexcept;
        bool SetParent(GameObject* parent);

        template<class T, class... Args>
        T* AddComponent(Args&&... args) {
            static_assert(std::is_base_of_v<IComponent, T>, "T must derive from IComponent");
            auto component = std::make_unique<T>(std::forward<Args>(args)...);
            T* ptr = component.get();
            ptr->SetOwner(this);
            ptr->OnAttach();
            components_.push_back(std::move(component));
            MarkRenderStateDirty();
            return ptr;
        }

        template<class T>
        T* GetComponent() {
            static_assert(std::is_base_of_v<IComponent, T>, "T must derive from IComponent");
            for (const auto& component : components_) {
                if (auto* casted = dynamic_cast<T*>(component.get())) {
                    return casted;
                }
            }
            return nullptr;
        }

        template<class T>
        const T* GetComponent() const {
            static_assert(std::is_base_of_v<IComponent, T>, "T must derive from IComponent");
            for (const auto& component : components_) {
                if (auto* casted = dynamic_cast<const T*>(component.get())) {
                    return casted;
                }
            }
            return nullptr;
        }

        template<class T, class Fn>
        void ForEachComponent(Fn&& fn) {
            static_assert(std::is_base_of_v<IComponent, T>,
                "T must derive from IComponent");
            for (const auto& component : components_) {
                if (auto* casted = dynamic_cast<T*>(component.get())) {
                    std::forward<Fn>(fn)(*casted);
                }
            }
        }

        template<class T, class Fn>
        void ForEachComponent(Fn&& fn) const {
            static_assert(std::is_base_of_v<IComponent, T>,
                "T must derive from IComponent");
            for (const auto& component : components_) {
                if (auto* casted =
                        dynamic_cast<const T*>(component.get())) {
                    std::forward<Fn>(fn)(*casted);
                }
            }
        }

        void Update(float dt);
        void Render();
        void RenderImGui();
        IComponent* AddComponentInstance(std::unique_ptr<IComponent> component);

        const std::vector<std::unique_ptr<IComponent>>& GetComponents() const;

        void MarkRenderStateDirty();
        void ClearRenderStateDirty();
        bool IsRenderStateDirty() const;
        uint64_t GetRenderStateRevision() const;
        uint64_t GetRenderStableId() const;

    private:
        friend class World;

        void SetOwnerWorld(World* world);
        void SetRuntimeHandle(RuntimeObjectHandle handle) noexcept;
        void MarkTransformHierarchyDirty();

        std::string name_;
        SceneObjectId documentId_{};
        RuntimeObjectHandle runtimeHandle_{};
        Transform3D transform_{};
        GameObject* parent_ = nullptr;
        std::vector<GameObject*> children_{};
        std::vector<std::unique_ptr<IComponent>> components_;
        World* ownerWorld_ = nullptr;
        uint64_t renderStateRevision_ = 1;
        bool renderStateDirty_ = true;
        bool active_ = true;
    };

} // namespace HIKARI
