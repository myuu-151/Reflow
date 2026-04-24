#include "core/reflow.h"
#include "mesh/mesh.h"
#include "renderer/camera.h"
#include "renderer/shader.h"
#include "renderer/grid.h"
#include "ui/ui.h"

#include "imgui.h"
#include <stb_image.h>

#include <cstdio>
#include <vector>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

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

static rf::UIState g_uiState;

// Input state
static bool g_mmb_down = false;
static bool g_rmb_down = false;
static double g_lastX = 0, g_lastY = 0;

// Window
static int g_winW = rf::kDefaultWidth;
static int g_winH = rf::kDefaultHeight;

// Colors
static const glm::vec3 kBgColor   = {0.12f, 0.12f, 0.14f};
static const glm::vec3 kMeshColor = {0.72f, 0.72f, 0.74f};
static const glm::vec3 kWireColor = {0.2f, 0.2f, 0.22f};
static const glm::vec3 kLightDir  = glm::normalize(glm::vec3(0.4f, 0.8f, 0.3f));

// Layout from ui.cpp (dynamic, scale-aware)

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
    // Don't handle if ImGui wants the mouse
    if (ImGui::GetIO().WantCaptureMouse) return;

    if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        g_mmb_down = (action == GLFW_PRESS);
        glfwGetCursorPos(win, &g_lastX, &g_lastY);
    }
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        g_rmb_down = (action == GLFW_PRESS);
        glfwGetCursorPos(win, &g_lastX, &g_lastY);
    }
}

static void cb_cursor(GLFWwindow* win, double x, double y)
{
    if (ImGui::GetIO().WantCaptureMouse) return;

    float dx = (float)(x - g_lastX);
    float dy = (float)(y - g_lastY);
    g_lastX = x;
    g_lastY = y;

    bool shift = (glfwGetKey(win, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS);
    bool alt   = (glfwGetKey(win, GLFW_KEY_LEFT_ALT) == GLFW_PRESS);

    // Alt+Left or MMB: orbit, Alt+Right or Shift+MMB: pan
    if (g_mmb_down) {
        if (shift)
            g_camera.pan(dx, dy);
        else
            g_camera.orbit(dx, dy);
    }
    if (g_rmb_down && alt) {
        g_camera.pan(dx, dy);
    }
}

static void cb_scroll(GLFWwindow*, double, double dy)
{
    if (ImGui::GetIO().WantCaptureMouse) return;
    g_camera.zoom((float)dy);
}

static void cb_key(GLFWwindow* win, int key, int, int action, int)
{
    if (ImGui::GetIO().WantCaptureKeyboard) return;
    if (action != GLFW_PRESS) return;

    // Numpad views
    switch (key) {
        case GLFW_KEY_KP_1: g_camera.yaw = 0;   g_camera.pitch = 0;  break;
        case GLFW_KEY_KP_3: g_camera.yaw = 90;  g_camera.pitch = 0;  break;
        case GLFW_KEY_KP_7: g_camera.yaw = 0;   g_camera.pitch = 89; break;

        // Tool shortcuts
        case GLFW_KEY_Q: g_uiState.currentTool = rf::Tool::Select; break;
        case GLFW_KEY_W: g_uiState.currentTool = rf::Tool::Move;   break;
        case GLFW_KEY_E: g_uiState.currentTool = rf::Tool::Rotate; break;
        case GLFW_KEY_R: g_uiState.currentTool = rf::Tool::Scale;  break;

        // Selection mode
        case GLFW_KEY_1: g_uiState.selectMode = rf::SelectMode::Vertex; break;
        case GLFW_KEY_2: g_uiState.selectMode = rf::SelectMode::Edge;   break;
        case GLFW_KEY_3: g_uiState.selectMode = rf::SelectMode::Face;   break;
        case GLFW_KEY_4: g_uiState.selectMode = rf::SelectMode::Object; break;
    }
}

// ---------------------------------------------------------------------------
// 3D Render (viewport area only)
// ---------------------------------------------------------------------------
static void render_viewport()
{
    // Calculate viewport region (between UI panels)
    int vpX = (int)rf::ui_toolbar_width();
    int vpY = (int)rf::ui_status_bar_height();
    int vpW = g_winW - (int)rf::ui_toolbar_width() - (int)rf::ui_right_panel_width();
    int vpH = g_winH - (int)rf::ui_top_bar_height() - (int)rf::ui_status_bar_height();

    if (vpW < 1 || vpH < 1) return;

    glViewport(vpX, vpY, vpW, vpH);
    glScissor(vpX, vpY, vpW, vpH);
    glEnable(GL_SCISSOR_TEST);

    glClearColor(kBgColor.x, kBgColor.y, kBgColor.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float aspect = (float)vpW / (float)vpH;
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

    // Wireframe overlay
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
    glDisable(GL_SCISSOR_TEST);

    // Restore full viewport for UI
    glViewport(0, 0, g_winW, g_winH);
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

    // Set window icon (title bar + taskbar)
    {
#ifdef _WIN32
        char exePath[MAX_PATH];
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        std::string dir(exePath);
        dir = dir.substr(0, dir.find_last_of("\\/") + 1);
        std::string iconPath = dir + "res/reflow_icon.png";
#else
        std::string iconPath = "res/reflow_icon.png";
#endif
        int w, h, ch;
        unsigned char* px = stbi_load(iconPath.c_str(), &w, &h, &ch, 4);
        if (px) {
            GLFWimage img = { w, h, px };
            glfwSetWindowIcon(win, 1, &img);
            stbi_image_free(px);
        }
    }

    glfwSetFramebufferSizeCallback(win, cb_resize);
    glfwSetMouseButtonCallback(win, cb_mouse_button);
    glfwSetCursorPosCallback(win, cb_cursor);
    glfwSetScrollCallback(win, cb_scroll);
    glfwSetKeyCallback(win, cb_key);

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

    // Init UI
    rf::ui_init(win);

    // Default scene
    g_meshes.push_back(rf::Mesh::create_cube());

    // Main loop
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();

        // Clear full window
        glViewport(0, 0, g_winW, g_winH);
        glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // 3D viewport
        render_viewport();

        // UI
        rf::ui_new_frame();
        rf::ui_top_bar(g_uiState);
        rf::ui_toolbar(g_uiState);
        rf::ui_objects_panel(g_uiState);
        rf::ui_properties_panel(g_uiState);
        rf::ui_status_bar(g_uiState);
        rf::ui_viewport_overlay(g_uiState);
        rf::ui_render();

        glfwSwapBuffers(win);
    }

    rf::ui_shutdown();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
