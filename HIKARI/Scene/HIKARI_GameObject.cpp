#include "HIKARI_GameObject.h"

#include <algorithm>

#include "HIKARI_World.h"

namespace HIKARI {

    GameObject::GameObject(std::string name)
        : name_(std::move(name)) {
    }

    const std::string& GameObject::GetName() const {
        return name_;
    }

    void GameObject::SetName(const std::string& name) {
        if (name_ == name) {
            return;
        }
        name_ = name;
        MarkRenderStateDirty();
    }

    void GameObject::SetDocumentId(SceneObjectId id) {
        if (documentId_ == id) {
            return;
        }
        const uint64_t oldStableId = GetRenderStableId();
        const SceneObjectId previousId = documentId_;
        documentId_ = id;
        if (ownerWorld_ != nullptr) {
            ownerWorld_->OnDocumentIdChanged(this, previousId, documentId_);
        }
        if (ownerWorld_ != nullptr && oldStableId != GetRenderStableId()) {
            ownerWorld_->MarkRenderObjectRemoved(oldStableId);
        }
        MarkRenderStateDirty();
    }

    SceneObjectId GameObject::GetDocumentId() const {
        return documentId_;
    }

    RuntimeObjectHandle GameObject::GetRuntimeHandle() const noexcept {
        return runtimeHandle_;
    }

    World* GameObject::GetWorld() noexcept {
        return ownerWorld_;
    }

    const World* GameObject::GetWorld() const noexcept {
        return ownerWorld_;
    }

    const Transform3D& GameObject::GetTransform() const noexcept {
        return transform_;
    }

    bool GameObject::SetLocalTransform(const Transform3D& transform) {
        if (transform_.HasSameLocalValue(transform)) {
            return false;
        }
        transform_.position = transform.position;
        transform_.rotation = transform.rotation;
        transform_.scale = transform.scale;
        transform_.useExplicitMatrix = transform.useExplicitMatrix;
        transform_.explicitMatrix = transform.explicitMatrix;
        MarkTransformHierarchyDirty();
        return true;
    }

    bool GameObject::SetLocalPosition(const MATH::Vec3& position) {
        if (transform_.position.x == position.x &&
            transform_.position.y == position.y &&
            transform_.position.z == position.z) {
            return false;
        }
        transform_.position = position;
        transform_.useExplicitMatrix = false;
        MarkTransformHierarchyDirty();
        return true;
    }

    bool GameObject::SetLocalRotation(const MATH::Quat& rotation) {
        if (transform_.rotation.x == rotation.x &&
            transform_.rotation.y == rotation.y &&
            transform_.rotation.z == rotation.z &&
            transform_.rotation.w == rotation.w) {
            return false;
        }
        transform_.rotation = rotation;
        transform_.useExplicitMatrix = false;
        MarkTransformHierarchyDirty();
        return true;
    }

    bool GameObject::SetLocalScale(const MATH::Vec3& scale) {
        if (transform_.scale.x == scale.x &&
            transform_.scale.y == scale.y &&
            transform_.scale.z == scale.z) {
            return false;
        }
        transform_.scale = scale;
        transform_.useExplicitMatrix = false;
        MarkTransformHierarchyDirty();
        return true;
    }

    GameObject* GameObject::GetParent() noexcept {
        return parent_;
    }

    const GameObject* GameObject::GetParent() const noexcept {
        return parent_;
    }

    const std::vector<GameObject*>& GameObject::GetChildren() const noexcept {
        return children_;
    }

    bool GameObject::SetParent(GameObject* parent) {
        if (parent_ == parent) {
            return true;
        }
        if (parent == this ||
            (parent != nullptr && parent->ownerWorld_ != ownerWorld_)) {
            return false;
        }
        for (const GameObject* ancestor = parent;
            ancestor != nullptr;
            ancestor = ancestor->parent_) {
            if (ancestor == this) {
                return false;
            }
        }

        if (parent_ != nullptr) {
            auto& siblings = parent_->children_;
            siblings.erase(
                std::remove(siblings.begin(), siblings.end(), this),
                siblings.end());
        }
        parent_ = parent;
        if (parent_ != nullptr) {
            parent_->children_.push_back(this);
        }
        transform_.SetParent(
            parent_ != nullptr ? &parent_->transform_ : nullptr);
        MarkTransformHierarchyDirty();
        return true;
    }

    void GameObject::Update(float dt) {
        if (!active_) {
            return;
        }

        for (const auto& component : components_) {
            component->Update(dt);
        }
    }

    void GameObject::Render() {
        if (!active_) {
            return;
        }

        for (const auto& component : components_) {
            component->Render();
        }
    }

    void GameObject::RenderImGui() {
        if (!active_) {
            return;
        }

        for (const auto& component : components_) {
            component->RenderImGui();
        }
    }

    IComponent* GameObject::AddComponentInstance(std::unique_ptr<IComponent> component) {
        if (!component) {
            return nullptr;
        }

        IComponent* ptr = component.get();
        ptr->SetOwner(this);
        ptr->OnAttach();
        components_.push_back(std::move(component));
        MarkRenderStateDirty();
        return ptr;
    }

    const std::vector<std::unique_ptr<IComponent>>& GameObject::GetComponents() const {
        return components_;
    }

    void GameObject::MarkRenderStateDirty() {
        ++renderStateRevision_;
        if (!renderStateDirty_) {
            renderStateDirty_ = true;
        }
        if (ownerWorld_ != nullptr) {
            ownerWorld_->MarkRenderObjectDirty(this);
        }
    }

    void GameObject::ClearRenderStateDirty() {
        renderStateDirty_ = false;
    }

    bool GameObject::IsRenderStateDirty() const {
        return renderStateDirty_;
    }

    uint64_t GameObject::GetRenderStateRevision() const {
        return renderStateRevision_;
    }

    uint64_t GameObject::GetRenderStableId() const {
        if (documentId_.value != 0) {
            return documentId_.value;
        }
        const uint64_t runtimeId = runtimeHandle_.ToValue();
        return runtimeId != 0
            ? runtimeId
            : reinterpret_cast<uint64_t>(this);
    }

    void GameObject::SetOwnerWorld(World* world) {
        ownerWorld_ = world;
        if (ownerWorld_ != nullptr) {
            ownerWorld_->MarkRenderObjectDirty(this);
        }
    }

    void GameObject::SetRuntimeHandle(
        RuntimeObjectHandle handle) noexcept {
        runtimeHandle_ = handle;
    }

    void GameObject::MarkTransformHierarchyDirty() {
        MarkRenderStateDirty();
        for (GameObject* child : children_) {
            if (child != nullptr) {
                child->MarkTransformHierarchyDirty();
            }
        }
    }

} // namespace HIKARI
