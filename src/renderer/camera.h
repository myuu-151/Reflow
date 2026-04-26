#pragma once

#include "core/reflow.h"

namespace rf {

struct Camera {
    glm::vec3 target   = {0, 0, 0};
    float distance     = 5.0f;
    float yaw          = 45.0f;   // degrees
    float pitch        = 30.0f;   // degrees
    float fov          = 45.0f;
    bool ortho         = false;

    // Orbit controls
    float orbitSpeed   = 0.3f;
    float zoomSpeed    = 0.5f;
    float panSpeed     = 0.004f;

    glm::vec3 get_position() const;
    glm::mat4 get_view() const;
    glm::mat4 get_projection(float aspect) const;

    void orbit(float dx, float dy);
    void pan(float dx, float dy);
    void zoom(float delta);
};

} // namespace rf
