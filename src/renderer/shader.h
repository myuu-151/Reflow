#pragma once

#include "core/reflow.h"
#include <string>

namespace rf {

struct Shader {
    GLuint id = 0;

    bool compile(const char* vertSrc, const char* fragSrc);
    void use() const;
    void set_mat4(const char* name, const glm::mat4& m) const;
    void set_vec3(const char* name, const glm::vec3& v) const;
    void set_float(const char* name, float f) const;
    void set_int(const char* name, int i) const;
};

// Built-in shaders
Shader create_mesh_shader();
Shader create_grid_shader();
Shader create_wireframe_shader();

} // namespace rf
