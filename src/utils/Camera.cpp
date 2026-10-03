#include "utils/Camera.h"
#include "system/InputManager.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
namespace {
void finite(float value) {
    if (!std::isfinite(value))
        throw std::invalid_argument("Camera value must be finite");
}
void positive(float value) {
    finite(value);
    if (value <= 0)
        throw std::invalid_argument("Camera value must be positive");
}
void vectorFinite(glm::vec3 value) {
    for (int i = 0; i < 3; i++)
        finite(value[i]);
}
} // namespace
void Camera::checkLogicThread() const { engine::LogicAsset::checkLogicThread(); }
Camera::Camera(glm::vec3 position, glm::vec3 up, float yaw, float pitch, float aspect) {
    vectorFinite(position);
    vectorFinite(up);
    positive(glm::length(up));
    positive(aspect);
    finite(yaw);
    finite(pitch);
    Position = position;
    WorldUp = glm::normalize(up);
    Yaw = yaw;
    Pitch = std::clamp(pitch, -89.f, 89.f);
    aspect_ratio = aspect;
    updateCameraVectors();
}
Camera::Camera(float x, float y, float z, float ux, float uy, float uz, float yaw, float pitch, float aspect)
    : Camera({x, y, z}, {ux, uy, uz}, yaw, pitch, aspect) {}
void Camera::setPosition(glm::vec3 value) {
    checkLogicThread();
    vectorFinite(value);
    Position = value;
}
void Camera::setAngles(float yaw, float pitch) {
    checkLogicThread();
    finite(yaw);
    finite(pitch);
    // Validate the candidate basis before committing, including nonstandard world-up.
    auto front =
        glm::vec3(std::cos(glm::radians(yaw)) * std::cos(glm::radians(std::clamp(pitch, -89.f, 89.f))),
                  std::sin(glm::radians(std::clamp(pitch, -89.f, 89.f))),
                  std::sin(glm::radians(yaw)) * std::cos(glm::radians(std::clamp(pitch, -89.f, 89.f))));
    if (glm::length(glm::cross(front, WorldUp)) < 1e-6f)
        throw std::invalid_argument("Camera front parallel to up");
    Yaw = yaw;
    Pitch = std::clamp(pitch, -89.f, 89.f);
    updateCameraVectors();
}
void Camera::setWorldUp(glm::vec3 value) {
    checkLogicThread();
    vectorFinite(value);
    positive(glm::length(value));
    value = glm::normalize(value);
    if (glm::length(glm::cross(Front, value)) < 1e-6f)
        throw std::invalid_argument("Camera up parallel to front");
    WorldUp = value;
    updateCameraVectors();
}
void Camera::setZoom(float value) {
    checkLogicThread();
    finite(value);
    if (value < 1 || value >= 179)
        throw std::invalid_argument("Camera FOV must be in [1,179)");
    Zoom = value;
}
void Camera::setAspect(float value) {
    checkLogicThread();
    positive(value);
    aspect_ratio = value;
}
void Camera::setClipPlanes(float n, float f) {
    checkLogicThread();
    positive(n);
    positive(f);
    if (f <= n)
        throw std::invalid_argument("Camera far must exceed near");
    zNear = n;
    zFar = f;
}
void Camera::setExposure(float value) {
    checkLogicThread();
    finite(value);
    if (value < 0)
        throw std::invalid_argument("Camera exposure must be nonnegative");
    exposure = value;
}
void Camera::setMovementSpeed(float value) {
    checkLogicThread();
    positive(value);
    MovementSpeed = value;
}
void Camera::setMouseSensitivity(float value) {
    checkLogicThread();
    positive(value);
    MouseSensitivity = value;
}
glm::mat4 Camera::GetViewMatrix() const {
    checkLogicThread();
    return glm::lookAt(Position, Position + Front, Up);
}
glm::mat4 Camera::GetPerspective() const {
    checkLogicThread();
    return glm::perspective(glm::radians(Zoom), aspect_ratio, zNear, zFar);
}
void Camera::ProcessKeyboard(Camera_Movement direction, float dt) {
    checkLogicThread();
    finite(dt);
    if (dt < 0)
        throw std::invalid_argument("Camera timestep must be nonnegative");
    if (fixed)
        return;
    const float velocity = MovementSpeed * dt;
    glm::vec3 delta(0);
    switch (direction) {
    case Camera_Movement::FORWARD:
        delta = Front;
        break;
    case Camera_Movement::BACKWARD:
        delta = -Front;
        break;
    case Camera_Movement::LEFT:
        delta = -Right;
        break;
    case Camera_Movement::RIGHT:
        delta = Right;
        break;
    case Camera_Movement::UP:
        delta = WorldUp;
        break;
    case Camera_Movement::DOWN:
        delta = -WorldUp;
        break;
    }
    setPosition(Position + delta * velocity);
}
void Camera::ProcessMouseMovement(float x, float y, bool) {
    checkLogicThread();
    finite(x);
    finite(y);
    if (fixed)
        return;
    setAngles(Yaw + x * MouseSensitivity, Pitch + y * MouseSensitivity);
}
void Camera::ProcessMouseScroll(float offset) {
    checkLogicThread();
    finite(offset);
    if (!fixed)
        setZoom(std::clamp(Zoom - offset, 1.f, 45.f));
}
void Camera::updateCameraVectors() {
    Front = glm::normalize(glm::vec3(std::cos(glm::radians(Yaw)) * std::cos(glm::radians(Pitch)),
                                     std::sin(glm::radians(Pitch)),
                                     std::sin(glm::radians(Yaw)) * std::cos(glm::radians(Pitch))));
    const auto right = glm::cross(Front, WorldUp);
    if (glm::length(right) < 1e-6f)
        throw std::invalid_argument("Camera basis is degenerate");
    Right = glm::normalize(right);
    Up = glm::normalize(glm::cross(Right, Front));
}
void Camera::tick() {
    checkLogicThread();
    if (fixed)
        return;
    auto input = InputManager::GetInstance();
    float multiplier = 1;
    if (input->getKeyStatus(ALT_PRESSED) == PRESSED)
        multiplier = .3f;
    if (input->getKeyStatus(SHIFT_PRESSED) == PRESSED)
        multiplier = 5;
    const auto dt = multiplier * input->deltaFrame;
    if (input->getKeyStatus(W_PRESSED) == PRESSED)
        ProcessKeyboard(Camera_Movement::FORWARD, dt);
    if (input->getKeyStatus(S_PRESSED) == PRESSED)
        ProcessKeyboard(Camera_Movement::BACKWARD, dt);
    if (input->getKeyStatus(A_PRESSED) == PRESSED)
        ProcessKeyboard(Camera_Movement::LEFT, dt);
    if (input->getKeyStatus(D_PRESSED) == PRESSED)
        ProcessKeyboard(Camera_Movement::RIGHT, dt);
    if (input->getKeyStatus(E_PRESSED) == PRESSED)
        ProcessKeyboard(Camera_Movement::UP, dt);
    if (input->getKeyStatus(Q_PRESSED) == PRESSED)
        ProcessKeyboard(Camera_Movement::DOWN, dt);
    if (input->scrollMove && input->getKeyStatus(MOUSE_SCROLL) == PRESSED)
        ProcessMouseScroll(input->mouseScrollY);
    if (!input->cursorEnbaled && input->mouseMove)
        ProcessMouseMovement(input->deltaX, input->deltaY);
    if (input->viewPortChange && input->width > 0 && input->height > 0)
        setAspect(float(input->width) / input->height);
}
