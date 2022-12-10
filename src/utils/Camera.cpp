#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vector>
#include<memory>
#include"Camera.h"
#include"../system/InputManager.h"
#include"../utils/BoundingBox.h"

extern std::unique_ptr<InputManager> inputManager;

void Camera::tick() {
	float deltaTime = inputManager->deltaFrame;
	float speedScale = 1;
	if (inputManager->getKeyStatus(ALT_PRESSED) == PRESSED)
		speedScale = 0.3;
	if (inputManager->getKeyStatus(SHIFT_PRESSED) == PRESSED)
		speedScale = 5;
	// wasd
	if (inputManager->getKeyStatus(W_PRESSED) == PRESSED) {
		ProcessKeyboard(Camera_Movement::FORWARD, speedScale * deltaTime);
	}
	if (inputManager->getKeyStatus(S_PRESSED) == PRESSED) {
		ProcessKeyboard(Camera_Movement::BACKWARD, speedScale * deltaTime);
	}
	if (inputManager->getKeyStatus(A_PRESSED) == PRESSED)
		ProcessKeyboard(Camera_Movement::LEFT, speedScale * deltaTime);
    if (inputManager->getKeyStatus(D_PRESSED) == PRESSED) {
        ProcessKeyboard(Camera_Movement::RIGHT, speedScale * deltaTime);
    }
	if (inputManager->getKeyStatus(E_PRESSED) == PRESSED)
		ProcessKeyboard(Camera_Movement::UP, speedScale * deltaTime);
	if (inputManager->getKeyStatus(Q_PRESSED) == PRESSED) {
		ProcessKeyboard(Camera_Movement::DOWN, speedScale * deltaTime);
	}

	// scroll
	if (inputManager->scrollMove) {
		if (inputManager->getKeyStatus(MOUSE_SCROLL) == PRESSED) {
			ProcessMouseScroll(inputManager->mouseScrollY);
		}
	}

	// Rotation
	if (!inputManager->cursorEnbaled && inputManager->mouseMove) {
		// if the cursor is not enabled
		ProcessMouseMovement(inputManager->deltaX, inputManager->deltaY);
	}
	
	// view port change 
	if (inputManager->viewPortChange) {
		aspect_ratio = inputManager->width * 1.0 / inputManager->height;
		//aspect_ratio = std::max(aspect_ratio, 0.1f);
	}
}

// constructor with vectors
Camera::Camera(glm::vec3 position, 
	glm::vec3 up, 
	float yaw, float pitch,
	float aspect) : Front(glm::vec3(0.0f, 0.0f, -1.0f)), MovementSpeed(SPEED), MouseSensitivity(SENSITIVITY), Zoom(ZOOM)
{
	Position = position;
	WorldUp = up;
	Yaw = yaw;
	Pitch = pitch;
	updateCameraVectors();
	aspect_ratio = aspect; 
	zNear = 0.1f;
	zFar = 100.0f;
	exposure = 1.0f;
}
// constructor with scalar values
Camera::Camera(float posX, 
	float posY, 
	float posZ, 
	float upX, 
	float upY, 
	float upZ, 
	float yaw, 
	float pitch,
	float aspect ) : Front(glm::vec3(0.0f, 0.0f, -1.0f)), MovementSpeed(SPEED), MouseSensitivity(SENSITIVITY), Zoom(ZOOM)
{
	Position = glm::vec3(posX, posY, posZ);
	WorldUp = glm::vec3(upX, upY, upZ);
	Yaw = yaw;
	Pitch = pitch;
	updateCameraVectors();
	aspect_ratio = aspect; 
	zNear = 0.1f;
	zFar = 100.0f;
	exposure = 1.0f;
}

// returns the view matrix calculated using Euler Angles and the LookAt Matrix
glm::mat4 Camera::GetViewMatrix ()const
{
	return glm::lookAt(Position, Position + Front, Up);
}

glm::mat4 Camera:: GetPerspective()const {
	return glm::perspective(glm::radians(Zoom), aspect_ratio, 0.1f, 1000.0f);
}

// processes input received from any keyboard-like input system. Accepts input parameter in the form of camera defined ENUM (to abstract it from windowing systems)
void Camera::ProcessKeyboard(Camera_Movement direction, float deltaTime)
{
	float velocity = MovementSpeed * deltaTime;
	if (direction == Camera_Movement::FORWARD)
		Position += Front * velocity;
	if (direction == Camera_Movement::BACKWARD)
		Position -= Front * velocity;
	if (direction == Camera_Movement::LEFT)
		Position -= Right * velocity;
	if (direction == Camera_Movement::RIGHT)
		Position += Right * velocity;
	if (direction == Camera_Movement::UP)
		Position += WorldUp * velocity;
	if (direction == Camera_Movement::DOWN)
		Position -= WorldUp * velocity;
}

// processes input received from a mouse input system. Expects the offset value in both the x and y direction.
void Camera::ProcessMouseMovement(float xoffset, float yoffset, GLboolean constrainPitch)
{
	xoffset *= MouseSensitivity;
	yoffset *= MouseSensitivity;

	Yaw += xoffset;
	Pitch += yoffset;

	// make sure that when pitch is out of bounds, screen doesn't get flipped
	if (constrainPitch)
	{
		if (Pitch > 89.0f)
			Pitch = 89.0f;
		if (Pitch < -89.0f)
			Pitch = -89.0f;
	}

	// update Front, Right and Up Vectors using the updated Euler angles
	updateCameraVectors();
}

// processes input received from a mouse scroll-wheel event. Only requires input on the vertical wheel-axis
void Camera::ProcessMouseScroll(float yoffset)
{
	Zoom -= (float)yoffset;
	if (Zoom < 1.0f)
		Zoom = 1.0f;
	if (Zoom > 45.0f)
		Zoom = 45.0f;
}

void Camera::updateCameraVectors()
{
	// calculate the new Front vector
	glm::vec3 front;
	front.x = cos(glm::radians(Yaw)) * cos(glm::radians(Pitch));
	front.y = sin(glm::radians(Pitch));
	front.z = sin(glm::radians(Yaw)) * cos(glm::radians(Pitch));
	Front = glm::normalize(front);
	// also re-calculate the Right and Up vector
	Right = glm::normalize(glm::cross(Front, WorldUp));  // normalize the vectors, because their length gets closer to 0 the more you look up or down which results in slower movement.
	Up = glm::normalize(glm::cross(Right, Front));
}

Frustum Camera::GetFrustum()const {
	Frustum frustum;
	const float halfVSide = zFar * tanf(glm::radians(this->Zoom) * 0.5f);
	const float halfHSide = halfVSide * aspect_ratio;
	const glm::vec3 frontMultFar = zFar * Front;

	frustum.nearFace = { Front , Position + zNear*Front};
	frustum.farFace = { -Front, Position + frontMultFar};
	frustum.rightFace = { glm::normalize(glm::cross(Up,frontMultFar + Right * halfHSide)),Position }; 
	frustum.leftFace = {glm::normalize(glm::cross(frontMultFar - Right * halfHSide, Up)),Position };
	frustum.topFace = {glm::normalize(glm::cross(Right, frontMultFar - Up * halfVSide)),Position };
	frustum.bottomFace = {glm::normalize(glm::cross(frontMultFar + Up * halfVSide, Right)),Position};

	frustum.nearFace.distance = glm::dot(frustum.nearFace.closestPoint, frustum.nearFace.normal);
	frustum.farFace.distance = glm::dot(frustum.farFace.closestPoint, frustum.farFace.normal);
	frustum.rightFace.distance = glm::dot(frustum.rightFace.closestPoint, frustum.rightFace.normal);
	frustum.leftFace.distance = glm::dot(frustum.leftFace.closestPoint, frustum.leftFace.normal);
	frustum.topFace.distance = glm::dot(frustum.topFace.closestPoint, frustum.topFace.normal);
	frustum.bottomFace.distance = glm::dot(frustum.bottomFace.closestPoint, frustum.bottomFace.normal);
	return frustum;
}