#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

struct Camera
{
	glm::vec3 position{0.0f, 0.0f, 3.0f};
	glm::vec3 front{0.0f, 0.0f, -1.0f};
	glm::vec3 up{0.0f, 1.0f, 0.0f};

	float yaw = -90.0f;
	float pitch = 0.0f;
	float speed = 2.5f;
	float sensitivity = 0.1f;
	float fov = 45.0f;

	glm::mat4 view_matrix() const;
};
