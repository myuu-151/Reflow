#include "renderer/camera.h"
#include <cmath>
#include <algorithm>

namespace rf {

glm::vec3 Camera::get_position() const
{
    float yr = glm::radians(yaw);
    float pr = glm::radians(pitch);
    return target + glm::vec3(
        distance * cos(pr) * sin(yr),
        distance * sin(pr),
        distance * cos(pr) * cos(yr)
    );
}

glm::mat4 Camera::get_view() const
{
    return glm::lookAt(get_position(), target, glm::vec3(0, 1, 0));
}

glm::mat4 Camera::get_projection(float aspect) const
{
    return glm::perspective(glm::radians(fov), aspect, 0.01f, 100.0f);
}

void Camera::orbit(float dx, float dy)
{
    yaw   += dx * orbitSpeed;
    pitch += dy * orbitSpeed;
    pitch = std::clamp(pitch, -89.0f, 89.0f);
}

void Camera::pan(float dx, float dy)
{
    glm::vec3 pos = get_position();
    glm::vec3 fwd = glm::normalize(target - pos);
    glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
    glm::vec3 up = glm::normalize(glm::cross(right, fwd));

    float scale = distance * panSpeed;
    target += right * (-dx * scale) + up * (dy * scale);
}

void Camera::zoom(float delta)
{
    distance -= delta * zoomSpeed;
    distance = std::clamp(distance, 0.1f, 50.0f);
}

} // namespace rf
