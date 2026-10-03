#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "engine/LogicAsset.h"
#include <stdexcept>

enum class Camera_Movement { FORWARD, BACKWARD, LEFT, RIGHT, UP, DOWN };
constexpr float YAW = -90.f, PITCH = 0.f, SPEED = 2.5f, SENSITIVITY = .1f, ZOOM = 45.f;
class Camera : public engine::LogicAsset {
  public:
    Camera(glm::vec3 position = {0, 0, 10}, glm::vec3 up = {0, 1, 0}, float yaw = YAW, float pitch = PITCH,
           float aspect = 16.f / 9.f);
    Camera(float, float, float, float, float, float, float, float, float aspect = 16.f / 9.f);
    Camera(const Camera &) = delete;
    Camera &operator=(const Camera &) = delete;
    void checkLogicThread() const;
    const glm::vec3 &getPosition() const {
        checkLogicThread();
        return Position;
    }
    const glm::vec3 &getFront() const {
        checkLogicThread();
        return Front;
    }
    const glm::vec3 &getUp() const {
        checkLogicThread();
        return Up;
    }
    const glm::vec3 &getRight() const {
        checkLogicThread();
        return Right;
    }
    const glm::vec3 &getWorldUp() const {
        checkLogicThread();
        return WorldUp;
    }
    float getYaw() const {
        checkLogicThread();
        return Yaw;
    }
    float getPitch() const {
        checkLogicThread();
        return Pitch;
    }
    float getMovementSpeed() const {
        checkLogicThread();
        return MovementSpeed;
    }
    float getMouseSensitivity() const {
        checkLogicThread();
        return MouseSensitivity;
    }
    float getZoom() const {
        checkLogicThread();
        return Zoom;
    }
    float getAspect() const {
        checkLogicThread();
        return aspect_ratio;
    }
    float getNear() const {
        checkLogicThread();
        return zNear;
    }
    float getFar() const {
        checkLogicThread();
        return zFar;
    }
    float getExposure() const {
        checkLogicThread();
        return exposure;
    }
    bool isFixed() const {
        checkLogicThread();
        return fixed;
    }
    void setPosition(glm::vec3);
    void setAngles(float yaw, float pitch);
    void setWorldUp(glm::vec3);
    void setZoom(float);
    void setAspect(float);
    void setClipPlanes(float nearPlane, float farPlane);
    void setExposure(float);
    void setMovementSpeed(float);
    void setMouseSensitivity(float);
    void setFixed(bool value) {
        checkLogicThread();
        fixed = value;
    }
    glm::mat4 GetViewMatrix() const;
    glm::mat4 GetPerspective() const;
    void ProcessKeyboard(Camera_Movement, float);
    void ProcessMouseMovement(float, float, bool constrainPitch = true);
    void ProcessMouseScroll(float);
    void tick();

  private:
    void updateCameraVectors();
    glm::vec3 Position, Front, Up, Right, WorldUp;
    float Yaw = YAW, Pitch = PITCH, MovementSpeed = SPEED, MouseSensitivity = SENSITIVITY, Zoom = ZOOM;
    float aspect_ratio = 16.f / 9.f, zNear = .1f, zFar = 1000.f, exposure = 1.f;
    bool fixed = false;
};
