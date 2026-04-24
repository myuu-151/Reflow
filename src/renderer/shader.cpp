#include "renderer/shader.h"
#include <cstdio>

namespace rf {

bool Shader::compile(const char* vertSrc, const char* fragSrc)
{
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vertSrc, nullptr);
    glCompileShader(vs);
    int ok;
    glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(vs, 512, nullptr, log);
        fprintf(stderr, "Vertex shader error: %s\n", log);
        return false;
    }

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fragSrc, nullptr);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(fs, 512, nullptr, log);
        fprintf(stderr, "Fragment shader error: %s\n", log);
        return false;
    }

    id = glCreateProgram();
    glAttachShader(id, vs);
    glAttachShader(id, fs);
    glLinkProgram(id);
    glGetProgramiv(id, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(id, 512, nullptr, log);
        fprintf(stderr, "Shader link error: %s\n", log);
        return false;
    }

    glDeleteShader(vs);
    glDeleteShader(fs);
    return true;
}

void Shader::use() const { glUseProgram(id); }

void Shader::set_mat4(const char* name, const glm::mat4& m) const {
    glUniformMatrix4fv(glGetUniformLocation(id, name), 1, GL_FALSE, glm::value_ptr(m));
}
void Shader::set_vec3(const char* name, const glm::vec3& v) const {
    glUniform3fv(glGetUniformLocation(id, name), 1, glm::value_ptr(v));
}
void Shader::set_float(const char* name, float f) const {
    glUniform1f(glGetUniformLocation(id, name), f);
}
void Shader::set_int(const char* name, int i) const {
    glUniform1i(glGetUniformLocation(id, name), i);
}

// ---------------------------------------------------------------------------
// Mesh shader — flat shaded with simple directional light
// ---------------------------------------------------------------------------
Shader create_mesh_shader()
{
    const char* vert = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUV;

uniform mat4 uMVP;
uniform mat4 uModel;

out vec3 vNormal;
out vec2 vUV;

void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vNormal = mat3(uModel) * aNormal;
    vUV = aUV;
}
)";

    const char* frag = R"(
#version 330 core
in vec3 vNormal;
in vec2 vUV;

uniform vec3 uColor;
uniform vec3 uLightDir;
uniform float uAmbient;

out vec4 FragColor;

void main() {
    vec3 n = normalize(vNormal);
    float diff = max(dot(n, normalize(uLightDir)), 0.0);
    vec3 col = uColor * (uAmbient + (1.0 - uAmbient) * diff);
    FragColor = vec4(col, 1.0);
}
)";

    Shader s;
    s.compile(vert, frag);
    return s;
}

// ---------------------------------------------------------------------------
// Grid shader — infinite ground grid
// ---------------------------------------------------------------------------
Shader create_grid_shader()
{
    const char* vert = R"(
#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uMVP;
out vec3 vWorldPos;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vWorldPos = aPos;
}
)";

    const char* frag = R"(
#version 330 core
in vec3 vWorldPos;
out vec4 FragColor;
void main() {
    // Fade grid with distance from origin
    float dist = length(vWorldPos.xz);
    float fade = 1.0 - smoothstep(4.0, 10.0, dist);

    // Axis highlights: X axis = red line along Z=0, Z axis = blue line along X=0
    if (abs(vWorldPos.z) < 0.01 && abs(vWorldPos.x) > 0.01) {
        FragColor = vec4(0.85, 0.25, 0.25, 0.8 * fade);
        return;
    }
    if (abs(vWorldPos.x) < 0.01 && abs(vWorldPos.z) > 0.01) {
        FragColor = vec4(0.25, 0.4, 0.85, 0.8 * fade);
        return;
    }

    FragColor = vec4(0.35, 0.35, 0.38, 0.25 * fade);
}
)";

    Shader s;
    s.compile(vert, frag);
    return s;
}

// ---------------------------------------------------------------------------
// Wireframe overlay shader
// ---------------------------------------------------------------------------
Shader create_wireframe_shader()
{
    const char* vert = R"(
#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uMVP;
void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

    const char* frag = R"(
#version 330 core
uniform vec3 uColor;
out vec4 FragColor;
void main() {
    FragColor = vec4(uColor, 1.0);
}
)";

    Shader s;
    s.compile(vert, frag);
    return s;
}

} // namespace rf
