#include "camera.h"

#include <glm/gtc/matrix_transform.hpp>

glm::mat4 Camera::view_matrix() const {
    return glm::lookAt(position, position + front, up);
}
