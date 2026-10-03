#pragma once
#include "engine/CommandInbox.h"
#include <glm/glm.hpp>
#include <variant>
#include <cstdint>
namespace engine {
// Values only: no mutable object/component pointers or executable callbacks.
struct SetTransform {
    uint64_t objectId = 0, componentId = 0;
    glm::vec3 position{0}, rotation{0}, scale{1};
};
struct RemoveObject {
    uint64_t objectId = 0;
};
struct RemoveComponent {
    uint64_t objectId = 0, componentId = 0;
};
struct SetDeferred {
    uint64_t objectId = 0;
    bool deferred = true;
};
using WorldCommand = std::variant<SetTransform, RemoveObject, RemoveComponent, SetDeferred>;
using WorldCommandInbox = CommandInbox<WorldCommand>;
} // namespace engine
