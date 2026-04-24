#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <string>
#include <vector>
#include <cstdint>

namespace rf {

// Forward declarations
struct Mesh;
struct Camera;
struct Editor;

// App-wide constants
constexpr int kDefaultWidth  = 1280;
constexpr int kDefaultHeight = 800;
constexpr const char* kAppName = "Reflow";
constexpr const char* kVersion = "0.1.0";

} // namespace rf
