#include "component/Component.h"
#include "component/GameObject.h"
std::shared_ptr<GameObject> Component::owner() const {
    checkLogicThread();
    auto object = owner_.lock();
    if (!object)
        throw std::logic_error("Component owner expired or not assigned");
    return object;
}
bool Component::hasOwner() const {
    checkLogicThread();
    return !owner_.expired();
}
void Component::checkLogicThread() const {
    if (logicThread_ != std::this_thread::get_id())
        throw std::logic_error("Component accessed outside its logic thread");
}
