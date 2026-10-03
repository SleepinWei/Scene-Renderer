#include "component/GameObject.h"
#include "renderer/RenderScene.h"
#include "system/meta_register.h"
GameObject::GameObject() = default;
GameObject::GameObject(std::string value) : name(std::move(value)) {}
GameObject::~GameObject() = default;
void GameObject::checkLogicThread() const {
    if (sealed_ || logicThread_ != std::this_thread::get_id())
        throw std::logic_error("GameObject is sealed or accessed outside its logic thread");
}
void GameObject::sealForTransfer() { sealForTransfer(nullptr); }
void GameObject::sealForTransfer(const RenderScene *previous) {
    checkLogicThread();
    if (auto scene = scene_.lock(); scene && scene.get() != previous)
        throw std::logic_error("Published object must transfer through its world");
    sealed_ = true;
    for (const auto &entry : components_)
        entry.second->logicThread_ = std::thread::id{};
}
void GameObject::bindScene(const std::shared_ptr<RenderScene> &scene, const RenderScene *previous) {
    auto old = scene_.lock();
    if (old && old != scene && old.get() != previous)
        throw std::logic_error("Object already published in another world");
    if (logicThread_ != std::this_thread::get_id() && !sealed_)
        throw std::logic_error("Foreign object must be sealed before ownership handoff");
    logicThread_ = std::this_thread::get_id();
    sealed_ = false;
    scene_ = scene;
    for (const auto &entry : components_)
        entry.second->logicThread_ = logicThread_;
}
void GameObject::unbindScene(const RenderScene *scene) {
    if (scene_.lock().get() == scene)
        scene_.reset();
}
void GameObject::componentsChanged() {
    ++componentRevision_;
    if (auto scene = scene_.lock())
        scene->objectComponentsChanged(assetId);
}
std::shared_ptr<Component> GameObject::GetComponent(std::string name) const {
    checkLogicThread();
    auto found = components_.find(name);
    return found == components_.end() ? nullptr : found->second;
}
std::shared_ptr<GameObject> GameObject::addComponent(const std::shared_ptr<Component> &component) {
    checkLogicThread();
    if (!component)
        throw std::invalid_argument("Cannot attach null or unnamed component");
    component->checkLogicThread();
    if (component->typeName().empty())
        throw std::invalid_argument("Cannot attach null or unnamed component");
    if (auto owner = component->owner_.lock(); owner && owner.get() != this)
        throw std::invalid_argument("Component already belongs to another object");
    const auto type = std::type_index(typeid(*component));
    auto named = components_.find(component->typeName());
    if (named != components_.end()) {
        if (typeid(*named->second) != typeid(*component))
            throw std::logic_error("Component name registered to another type");
        return shared_from_this();
    }
    if (types_.count(type))
        throw std::logic_error("Component type registered under another name");
    auto self = shared_from_this();
    auto inserted = components_.emplace(component->typeName(), component);
    try {
        types_.emplace(type, component);
    } catch (...) {
        components_.erase(inserted.first);
        throw;
    }
    component->owner_ = self;
    component->logicThread_ = logicThread_;
    componentsChanged();
    return self;
}
bool GameObject::removeComponent(uint64_t id) {
    checkLogicThread();
    for (auto it = components_.begin(); it != components_.end(); ++it)
        if (it->second->assetId == id) {
            auto component = it->second;
            types_.erase(std::type_index(typeid(*component)));
            components_.erase(it);
            component->owner_.reset();
            componentsChanged();
            return true;
        }
    return false;
}
void GameObject::loadFromJson(json &data) {
    checkLogicThread();
    name = data.at("name").get<std::string>();
    if (data.contains("components"))
        for (auto &entry : data.at("components").items()) {
            auto component = Meta::generateComponent(entry.key());
            component->loadFromJson(entry.value());
            addComponent(component);
        }
    if (data.contains("isDeferred"))
        setDeferred(data.at("isDeferred").get<bool>());
}
bool GameObject::isDeferred() const {
    checkLogicThread();
    return deferred_;
}
void GameObject::setDeferred(bool value) {
    checkLogicThread();
    if (deferred_ != value) {
        deferred_ = value;
        componentsChanged();
    }
}
