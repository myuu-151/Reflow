#include "core/reflow.h"
#include "mesh/mesh.h"
#include "renderer/camera.h"
#include "renderer/shader.h"
#include "renderer/grid.h"

#include <cstdio>
#include <vector>

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static rf::Camera g_camera;
static rf::Shader g_meshShader;
static rf::Shader g_gridShader;
static rf::Shader g_wireShader;
static rf::Grid   g_grid;

static std::vector<rf::Mesh> g_meshes;
static int g_selectedMesh = 0;

// Input state
static bool g_mmb_down = false;
static bool g_shift_down = false;
static double g_lastX = 0, g_lastY = 0;

// Window
static int g_winW = rf::kDefaultWidth;
static int g_winH = rf::kDefaultHeight;

// Colors
static const glm::vec3 kBgColor   = {0.12f, 0.12f, 0.14f};
static const glm::vec3 kMeshColor = {0.72f, 0.72f, 0.74f};
static const glm::vec3 kWireColor = {0.2f, 0.2f, 0.22f};
static const glm::vec3 kLightDir  = glm::normalize(glm::vec3(0.4f, 0.8f, 0.3f));

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------
static void cb_resize(GLFWwindow*, int w, int h)
{
    g_winW = w;
    g_winH = h;
    glViewport(0, 0, w, h);
}

static void cb_mouse_button(GLFWwindow* win, int button, int action, int mods)
{
    if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        g_mmb_down = (action == GLFW_PRESS);
        glfwGetCursorPos(win, &g_lastX, &g_lastY);
    }
}

static void cb_cursor(GLFWwindow*, double x, double y)
{
    float dx = (float)(x - g_lastX);
    float dy = (float)(y - g_lastY);
    g_lastX = x;
    g_lastY = y;

    if (!g_mmb_down) return;

    g_shift_down = (glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS);

    if (g_shift_down)
        g_camera.pan(dx, dy);
    else
        g_camera.orbit(dx, dy);
}

static void cb_scroll(GLFWwindow*, double, double dy)
{
    g_camera.zoom((float)dy);
}

static void cb_key(GLFWwindow* win, int key, int, int action, int)
{
    if (action != GLFW_PRESS) return;

    // Numpad views
    switch (key) {
        case GLFW_KEY_KP_1: g_camera.yaw = 0;   g_camera.pitch = 0;  break; // front
        case GLFW_KEY_KP_3: g_camera.yaw = 90;  g_camera.pitch = 0;  break; // right
        case GLFW_KEY_KP_7: g_camera.yaw = 0;   g_camera.pitch = 89; break; // top
        case GLFW_KEY_KP_5: break; // toggle ortho (todo)
    }
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------
static void render()
{
    glClearColor(kBgColor.x, kBgColor.y, kBgColor.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float aspect = (float)g_winW / (float)g_winH;
    glm::mat4 view = g_camera.get_view();
    glm::mat4 proj = g_camera.get_projection(aspect);
    glm::mat4 vp = proj * view;

    // Grid
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    g_gridShader.use();
    g_gridShader.set_mat4("uMVP", vp);
    g_grid.draw();
    glDisable(GL_BLEND);

    // Meshes — solid pass
    glEnable(GL_DEPTH_TEST);
    g_meshShader.use();
    g_meshShader.set_vec3("uLightDir", kLightDir);
    g_meshShader.set_float("uAmbient", 0.25f);

    for (auto& mesh : g_meshes) {
        glm::mat4 model = glm::translate(glm::mat4(1.0f), mesh.position);
        glm::mat4 mvp = vp * model;
        g_meshShader.set_mat4("uMVP", mvp);
        g_meshShader.set_mat4("uModel", model);
        g_meshShader.set_vec3("uColor", kMeshColor);

        glBindVertexArray(mesh.vao);
        glDrawArrays(GL_TRIANGLES, 0, mesh.triCount * 3);
    }

    // Meshes — wireframe overlay
    g_wireShader.use();
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    glEnable(GL_POLYGON_OFFSET_LINE);
    glPolygonOffset(-1.0f, -1.0f);

    for (auto& mesh : g_meshes) {
        glm::mat4 model = glm::translate(glm::mat4(1.0f), mesh.position);
        glm::mat4 mvp = vp * model;
        g_wireShader.set_mat4("uMVP", mvp);
        g_wireShader.set_vec3("uColor", kWireColor);

        glBindVertexArray(mesh.vao);
        glDrawArrays(GL_TRIANGLES, 0, mesh.triCount * 3);
    }

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glDisable(GL_POLYGON_OFFSET_LINE);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main()
{
    if (!glfwInit()) {
        fprintf(stderr, "Failed to init GLFW\n");
        return 1;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    GLFWwindow* win = glfwCreateWindow(g_winW, g_winH, rf::kAppName, nullptr, nullptr);
    if (!win) {
        fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    // Callbacks
    glfwSetFramebufferSizeCallback(win, cb_resize);
    glfwSetMouseButtonCallback(win, cb_mouse_button);
    glfwSetCursorPosCallback(win, cb_cursor);
    glfwSetScrollCallback(win, cb_scroll);
    glfwSetKeyCallback(win, cb_key);

    // Load OpenGL
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        fprintf(stderr, "Failed to init glad\n");
        return 1;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);

    printf("%s v%s\n", rf::kAppName, rf::kVersion);
    printf("OpenGL %s\n", glGetString(GL_VERSION));

    // Init shaders
    g_meshShader = rf::create_mesh_shader();
    g_gridShader = rf::create_grid_shader();
    g_wireShader = rf::create_wireframe_shader();

    // Init grid
    g_grid.init();

    // Default scene: one cube
    g_meshes.push_back(rf::Mesh::create_cube());

    // Main loop
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();
        render();
        glfwSwapBuffers(win);
    }

    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
