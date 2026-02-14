#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <GLFW/glfw3.h>

struct Camera {
    glm::vec3 position{0.0f, 0.0f, 3.0f};
    glm::vec3 front{0.0f, 0.0f, -1.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};

    float yaw        = -90.0f;
    float pitch      =   0.0f;
    float speed      =   2.5f;
    float sensitivity=   0.1f;
    float fov        =  45.0f;

    bool   firstMouse = true;
    double lastX = 0.0, lastY = 0.0;

    glm::mat4 view_matrix() const {
        return glm::lookAt(position, position + front, up);
    }

    void process_keyboard(GLFWwindow* window, float dt) {
        float v = speed * dt;
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) position += front * v;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) position -= front * v;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            position -= glm::normalize(glm::cross(front, up)) * v;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            position += glm::normalize(glm::cross(front, up)) * v;
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)         position += up * v;
        if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)  position -= up * v;
    }

    void process_mouse(double xpos, double ypos) {
        if (firstMouse) { lastX = xpos; lastY = ypos; firstMouse = false; }

        float xoff = static_cast<float>(xpos - lastX) * sensitivity;
        float yoff = static_cast<float>(lastY - ypos) * sensitivity;
        lastX = xpos;
        lastY = ypos;

        yaw   += xoff;
        pitch += yoff;
        if (pitch >  89.0f) pitch =  89.0f;
        if (pitch < -89.0f) pitch = -89.0f;

        glm::vec3 d;
        d.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
        d.y = sin(glm::radians(pitch));
        d.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
        front = glm::normalize(d);
    }
};
