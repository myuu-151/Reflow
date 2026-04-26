#include "core/reflow.h"
#include "mesh/mesh.h"
#include "renderer/camera.h"
#include "renderer/shader.h"
#include "renderer/grid.h"
#include "ui/ui.h"

#include "imgui.h"
#include <stb_image.h>

#include <cstdio>
#include <cmath>
#include <vector>
#include <string>
#include <set>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#endif

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
static rf::Camera g_camera;
static rf::Shader g_meshShader;
static rf::Shader g_gridShader;
static rf::Shader g_wireShader;
static rf::Shader g_vcShader;  // per-vertex color shader
static rf::Grid   g_grid;

static std::vector<rf::Mesh> g_meshes;
static int g_selectedMesh = 0;
static bool g_objectSelected = false;

// Camera animation
static bool g_camAnimating = false;
static glm::vec3 g_camAnimStartTarget, g_camAnimEndTarget;
static float g_camAnimStartDist, g_camAnimEndDist;
static float g_camAnimStartYaw, g_camAnimEndYaw;
static float g_camAnimStartPitch, g_camAnimEndPitch;
static float g_camAnimT = 0.0f;

static rf::UIState g_uiState;
static bool g_openNormalsMenu = false;
static bool g_openDeleteConfirm = false;

// Input state
static bool g_mmb_down = false;
static bool g_rmb_down = false;
static double g_lastX = 0, g_lastY = 0;

// Window
static int g_winW = rf::kDefaultWidth;
static int g_winH = rf::kDefaultHeight;

// Colors
static const glm::vec3 kBgColor    = {0.12f, 0.12f, 0.14f};
static const glm::vec3 kMeshColor  = {0.72f, 0.72f, 0.74f};
static const glm::vec3 kWireColor  = {0.2f, 0.2f, 0.22f};
static const glm::vec3 kLightDir   = glm::normalize(glm::vec3(0.4f, 0.8f, 0.3f));
static const glm::vec3 kSelectColor = {1.0f, 0.55f, 0.1f};  // orange selection
static const glm::vec3 kAxisColors[3] = {{1,0.2f,0.2f}, {0.2f,1,0.2f}, {0.3f,0.3f,1}};

// Viewport cache (updated each frame in render_viewport)
static int g_vpX, g_vpY, g_vpW, g_vpH;
static glm::mat4 g_viewMat, g_projMat;

// Selection overlay GPU objects
static GLuint g_selVAO = 0, g_selVBO = 0;

// Box select
static bool g_boxSelecting = false;
static double g_boxStartX, g_boxStartY, g_boxEndX, g_boxEndY;

// Transform mode: 0=none, 1=grab, 2=scale, 3=rotate
static int g_transformMode = 0;
static int g_grab_axis = -1;           // -1=free, 0=X, 1=Y, 2=Z
static glm::vec3 g_grab_center;       // world-space center at start
static double g_grab_startMX, g_grab_startMY;
static std::vector<glm::vec3> g_grab_origPos;
static std::vector<int> g_grab_vertIdx;
static float g_scale_start_dist = 1.0f; // for scale mode
static float g_rotate_start_angle = 0.0f; // for rotate mode

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

// Compute world-space ray from screen coordinates
static void screen_to_ray(double mx, double my, glm::vec3& rayO, glm::vec3& rayD)
{
    float topBar = rf::ui_top_bar_height();
    float relX = (float)(mx - g_vpX);
    float relY = (float)(my - topBar);

    float ndcX = (relX / g_vpW) * 2.0f - 1.0f;
    float ndcY = 1.0f - (relY / g_vpH) * 2.0f;

    glm::mat4 invVP = glm::inverse(g_projMat * g_viewMat);
    glm::vec4 near4 = invVP * glm::vec4(ndcX, ndcY, -1, 1);
    glm::vec4 far4  = invVP * glm::vec4(ndcX, ndcY,  1, 1);
    near4 /= near4.w;
    far4  /= far4.w;

    rayO = glm::vec3(near4);
    rayD = glm::normalize(glm::vec3(far4) - glm::vec3(near4));
}

static bool mouse_in_viewport(double mx, double my)
{
    float topBar = rf::ui_top_bar_height();
    float relX = (float)(mx - g_vpX);
    float relY = (float)(my - topBar);
    return relX >= 0 && relX < g_vpW && relY >= 0 && relY < g_vpH;
}

static glm::vec2 world_to_screen(const glm::vec3& pos, const glm::mat4& mvp)
{
    glm::vec4 clip = mvp * glm::vec4(pos, 1.0f);
    if (clip.w <= 0) return {-1, -1};
    float ndcX = clip.x / clip.w;
    float ndcY = clip.y / clip.w;
    float topBar = rf::ui_top_bar_height();
    float sx = (ndcX * 0.5f + 0.5f) * g_vpW + g_vpX;
    float sy = (1.0f - (ndcY * 0.5f + 0.5f)) * g_vpH + topBar;
    return {sx, sy};
}

static void finish_box_select()
{
    if (g_meshes.empty() || g_uiState.editorMode != rf::EditorMode::Model) return;
    auto& mesh = g_meshes[g_selectedMesh];
    glm::mat4 model = glm::translate(glm::mat4(1.0f), mesh.position);
    glm::mat4 mvp = g_projMat * g_viewMat * model;

    float minX = (float)std::min(g_boxStartX, g_boxEndX);
    float maxX = (float)std::max(g_boxStartX, g_boxEndX);
    float minY = (float)std::min(g_boxStartY, g_boxEndY);
    float maxY = (float)std::max(g_boxStartY, g_boxEndY);

    // Camera direction in model space for back-face culling
    glm::vec3 camPos = g_camera.get_position() - mesh.position;

    // Precompute which faces are front-facing
    std::vector<bool> faceFront(mesh.faces.size(), false);
    for (int i = 0; i < (int)mesh.faces.size(); i++) {
        // Compute face normal from first 3 verts
        std::vector<int> fv;
        int start = mesh.faces[i].edge;
        int cur = start;
        do {
            fv.push_back(mesh.hedges[cur].vertex);
            cur = mesh.hedges[cur].next;
        } while (cur != start && (int)fv.size() < 64);
        if (fv.size() < 3) continue;
        glm::vec3 a = mesh.verts[fv[0]].pos;
        glm::vec3 b = mesh.verts[fv[1]].pos;
        glm::vec3 c = mesh.verts[fv[2]].pos;
        glm::vec3 fn = glm::cross(b - a, c - a);
        glm::vec3 faceCenter = a;
        for (int j = 1; j < (int)fv.size(); j++) faceCenter += mesh.verts[fv[j]].pos;
        faceCenter /= (float)fv.size();
        glm::vec3 toCamera = camPos - faceCenter;
        faceFront[i] = glm::dot(fn, toCamera) > 0.0f;
    }

    // For verts/edges: visible if at least one adjacent face is front-facing
    // Build vert→face adjacency
    std::vector<bool> vertVisible(mesh.verts.size(), false);
    for (int i = 0; i < (int)mesh.faces.size(); i++) {
        if (!faceFront[i]) continue;
        int start = mesh.faces[i].edge;
        int cur = start;
        do {
            vertVisible[mesh.hedges[cur].vertex] = true;
            cur = mesh.hedges[cur].next;
        } while (cur != start);
    }

    bool shift = (glfwGetKey(glfwGetCurrentContext(), GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS);
    if (!shift) mesh.deselect_all();

    if (g_uiState.selectMode == rf::SelectMode::Vertex) {
        for (int i = 0; i < (int)mesh.verts.size(); i++) {
            if (!vertVisible[i]) continue;
            glm::vec2 s = world_to_screen(mesh.verts[i].pos, mvp);
            if (s.x >= minX && s.x <= maxX && s.y >= minY && s.y <= maxY)
                mesh.verts[i].selected = true;
        }
    } else if (g_uiState.selectMode == rf::SelectMode::Edge) {
        for (int i = 0; i < (int)mesh.edges.size(); i++) {
            int he = mesh.edges[i].he;
            int va = mesh.hedges[mesh.hedges[he].prev].vertex;
            int vb = mesh.hedges[he].vertex;
            // Edge visible if at least one adjacent face is front-facing
            // Check via half-edge face and twin's face
            bool edgeVis = false;
            int f1 = mesh.hedges[he].face;
            if (f1 >= 0 && faceFront[f1]) edgeVis = true;
            int twin = mesh.hedges[he].twin;
            if (!edgeVis && twin >= 0) {
                int f2 = mesh.hedges[twin].face;
                if (f2 >= 0 && faceFront[f2]) edgeVis = true;
            }
            if (!edgeVis) continue;
            glm::vec2 sa = world_to_screen(mesh.verts[va].pos, mvp);
            glm::vec2 sb = world_to_screen(mesh.verts[vb].pos, mvp);
            glm::vec2 mid = (sa + sb) * 0.5f;
            if (mid.x >= minX && mid.x <= maxX && mid.y >= minY && mid.y <= maxY)
                mesh.edges[i].selected = true;
        }
    } else if (g_uiState.selectMode == rf::SelectMode::Face) {
        for (int i = 0; i < (int)mesh.faces.size(); i++) {
            if (!faceFront[i]) continue;
            glm::vec3 center(0);
            int count = 0;
            int start = mesh.faces[i].edge;
            int cur = start;
            do {
                center += mesh.verts[mesh.hedges[cur].vertex].pos;
                count++;
                cur = mesh.hedges[cur].next;
            } while (cur != start && count < 64);
            center /= (float)count;
            glm::vec2 s = world_to_screen(center, mvp);
            if (s.x >= minX && s.x <= maxX && s.y >= minY && s.y <= maxY)
                mesh.faces[i].selected = true;
        }
    }
}

static void cancel_transform()
{
    if (g_transformMode != 0 && !g_meshes.empty()) {
        auto& mesh = g_meshes[g_selectedMesh];
        for (int i = 0; i < (int)g_grab_vertIdx.size(); i++)
            mesh.verts[g_grab_vertIdx[i]].pos = g_grab_origPos[i];
        mesh.recalc_normals();
        mesh.rebuild_gpu();
    }
    g_transformMode = 0;
    g_grab_axis = -1;
}

static void confirm_transform()
{
    g_transformMode = 0;
    g_grab_axis = -1;
}

static void start_transform(int mode)
{
    if (g_meshes.empty()) return;
    auto& mesh = g_meshes[g_selectedMesh];

    g_grab_vertIdx.clear();
    g_grab_origPos.clear();

    // Collect selected verts (or verts of selected edges/faces/object)
    std::set<int> selVerts;
    if (g_uiState.selectMode == rf::SelectMode::Object) {
        if (g_objectSelected) {
            for (int i = 0; i < (int)mesh.verts.size(); i++)
                selVerts.insert(i);
        }
    } else if (g_uiState.selectMode == rf::SelectMode::Vertex) {
        for (int i = 0; i < (int)mesh.verts.size(); i++)
            if (mesh.verts[i].selected) selVerts.insert(i);
    } else if (g_uiState.selectMode == rf::SelectMode::Edge) {
        for (auto& e : mesh.edges) {
            if (!e.selected) continue;
            int he = e.he;
            selVerts.insert(mesh.hedges[mesh.hedges[he].prev].vertex);
            selVerts.insert(mesh.hedges[he].vertex);
        }
    } else if (g_uiState.selectMode == rf::SelectMode::Face) {
        for (auto& f : mesh.faces) {
            if (!f.selected) continue;
            int start = f.edge;
            int cur = start;
            do {
                selVerts.insert(mesh.hedges[cur].vertex);
                cur = mesh.hedges[cur].next;
            } while (cur != start);
        }
    }

    if (selVerts.empty()) return;

    glm::vec3 sum(0);
    for (int vi : selVerts) {
        g_grab_vertIdx.push_back(vi);
        g_grab_origPos.push_back(mesh.verts[vi].pos);
        sum += mesh.verts[vi].pos;
    }

    g_grab_center = sum / (float)selVerts.size() + mesh.position;
    g_transformMode = mode;
    g_grab_axis = -1;

    double mx, my;
    glfwGetCursorPos(glfwGetCurrentContext(), &mx, &my);
    g_grab_startMX = mx;
    g_grab_startMY = my;

    if (mode == 2) {
        // Scale: measure initial distance from center to mouse
        g_scale_start_dist = (float)std::sqrt(
            (mx - g_grab_startMX) * (mx - g_grab_startMX) +
            (my - g_grab_startMY) * (my - g_grab_startMY));
        if (g_scale_start_dist < 1.0f) g_scale_start_dist = 1.0f;
    }
    if (mode == 3) {
        // Rotate: measure initial angle
        g_rotate_start_angle = (float)std::atan2(my - g_grab_startMY, mx - g_grab_startMX);
    }
}

static void cb_mouse_button(GLFWwindow* win, int button, int action, int mods)
{
    if (ImGui::GetIO().WantCaptureMouse) return;

    if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        g_mmb_down = (action == GLFW_PRESS);
        glfwGetCursorPos(win, &g_lastX, &g_lastY);
    }
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        g_rmb_down = (action == GLFW_PRESS);
        glfwGetCursorPos(win, &g_lastX, &g_lastY);

        // RMB: confirm transform and/or pick elements
        if (action == GLFW_PRESS) {
            if (g_transformMode != 0) {
                confirm_transform();
            }

            double mx, my;
            glfwGetCursorPos(win, &mx, &my);
            if (!mouse_in_viewport(mx, my)) return;
            if (g_meshes.empty()) return;

            glm::vec3 rayO, rayD;
            screen_to_ray(mx, my, rayO, rayD);

            auto& mesh = g_meshes[g_selectedMesh];
            bool shift = (mods & GLFW_MOD_SHIFT) != 0;
            if (!shift) mesh.deselect_all();

            switch (g_uiState.selectMode) {
                case rf::SelectMode::Vertex: {
                    int vi = mesh.pick_vertex(rayO, rayD);
                    if (vi >= 0) mesh.verts[vi].selected = !mesh.verts[vi].selected || !shift;
                    break;
                }
                case rf::SelectMode::Edge: {
                    int ei = mesh.pick_edge(rayO, rayD);
                    if (ei >= 0) mesh.edges[ei].selected = !mesh.edges[ei].selected || !shift;
                    break;
                }
                case rf::SelectMode::Face: {
                    int fi = mesh.pick_face(rayO, rayD);
                    if (fi >= 0) mesh.faces[fi].selected = !mesh.faces[fi].selected || !shift;
                    break;
                }
                case rf::SelectMode::Object: {
                    int hit = mesh.pick_face(rayO, rayD);
                    g_objectSelected = (hit >= 0);
                    break;
                }
                default: break;
            }
        }
    }

    // LMB: confirm transform, deselect object, or start box select
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
        if (g_transformMode != 0) {
            confirm_transform();
        } else {
            double mx, my;
            glfwGetCursorPos(win, &mx, &my);
            if (mouse_in_viewport(mx, my)) {
                if (g_uiState.selectMode == rf::SelectMode::Object) {
                    g_objectSelected = false;
                } else if (g_uiState.editorMode == rf::EditorMode::Model) {
                    g_boxSelecting = true;
                    g_boxStartX = mx; g_boxStartY = my;
                    g_boxEndX = mx; g_boxEndY = my;
                }
            }
        }
    }

    // LMB release: finish box select
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) {
        if (g_boxSelecting) {
            g_boxSelecting = false;
            double mx, my;
            glfwGetCursorPos(win, &mx, &my);
            g_boxEndX = mx; g_boxEndY = my;
            // Only do box select if dragged more than a few pixels
            if (std::abs(g_boxEndX - g_boxStartX) > 3 || std::abs(g_boxEndY - g_boxStartY) > 3)
                finish_box_select();
            else if (!g_meshes.empty())
                g_meshes[g_selectedMesh].deselect_all();
        }
    }
}

static void cb_cursor(GLFWwindow* win, double x, double y)
{
    if (ImGui::GetIO().WantCaptureMouse) return;

    float dx = (float)(x - g_lastX);
    float dy = (float)(y - g_lastY);
    g_lastX = x;
    g_lastY = y;

    if (g_boxSelecting) {
        g_boxEndX = x;
        g_boxEndY = y;
    }

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

static void cb_key(GLFWwindow* win, int key, int, int action, int mods)
{
    if (ImGui::GetIO().WantTextInput) return;
    if (action != GLFW_PRESS) return;

    // During transform: axis constraints or cancel
    if (g_transformMode != 0) {
        switch (key) {
            case GLFW_KEY_X: g_grab_axis = (g_grab_axis == 0) ? -1 : 0; return;
            case GLFW_KEY_Y: g_grab_axis = (g_grab_axis == 1) ? -1 : 1; return;
            case GLFW_KEY_Z: g_grab_axis = (g_grab_axis == 2) ? -1 : 2; return;
            case GLFW_KEY_ESCAPE: cancel_transform(); return;
            case GLFW_KEY_ENTER: confirm_transform(); return;
            // G during grab = reset to free (double-tap G)
            case GLFW_KEY_G: if (g_transformMode == 1) { g_grab_axis = -1; } return;
        }
        return;
    }

    switch (key) {
        // Tool shortcuts
        case GLFW_KEY_Q: g_uiState.currentTool = rf::Tool::Select; break;
        case GLFW_KEY_W: g_openNormalsMenu = true; break;

        // View directions (Blender-style number keys)

        // Grab (G) / Alt+G snap to origin
        case GLFW_KEY_G:
            if (mods & GLFW_MOD_ALT) {
                if (!g_meshes.empty()) {
                    auto& mesh = g_meshes[g_selectedMesh];
                    glm::vec3 center(0);
                    for (auto& v : mesh.verts) center += v.pos;
                    center /= (float)mesh.verts.size();
                    for (auto& v : mesh.verts) v.pos -= center;
                    mesh.position = {0, 0, 0};
                    mesh.recalc_normals();
                    mesh.rebuild_gpu();
                }
            } else {
                start_transform(1);
            }
            break;

        // Scale (S)
        case GLFW_KEY_S:
            start_transform(2);
            break;

        // Rotate (R)
        case GLFW_KEY_R:
            start_transform(3);
            break;

        // Extrude (E) — face mode only
        case GLFW_KEY_E:
            if (g_uiState.selectMode == rf::SelectMode::Face && !g_meshes.empty()) {
                auto& mesh = g_meshes[g_selectedMesh];
                if (mesh.count_selected_faces() > 0) {
                    mesh.extrude_selected_faces();
                    // Auto-enter grab mode along face normal after extrude
                    start_transform(1);
                }
            }
            break;

        // X = delete mesh confirmation
        case GLFW_KEY_X:
            if (g_uiState.selectMode == rf::SelectMode::Object && !g_meshes.empty() && g_objectSelected)
                g_openDeleteConfirm = true;
            break;

        // Delete
        case GLFW_KEY_DELETE:
        case GLFW_KEY_BACKSPACE:
            if (g_uiState.selectMode != rf::SelectMode::Object && !g_meshes.empty()) {
                g_meshes[g_selectedMesh].delete_selected();
            }
            break;

        // Ctrl+= / Ctrl+- : grow / shrink selection
        case GLFW_KEY_EQUAL: // + key (=/+)
        case GLFW_KEY_KP_ADD:
            if ((mods & GLFW_MOD_CONTROL) && !g_meshes.empty() && g_uiState.selectMode != rf::SelectMode::Object) {
                auto& mesh = g_meshes[g_selectedMesh];
                if (g_uiState.selectMode == rf::SelectMode::Vertex) {
                    std::set<int> toSelect;
                    for (int vi = 0; vi < (int)mesh.verts.size(); vi++) {
                        if (!mesh.verts[vi].selected) continue;
                        // Find all adjacent verts via half-edges
                        for (int he = 0; he < (int)mesh.hedges.size(); he++) {
                            int src = mesh.hedges[mesh.hedges[he].prev].vertex;
                            int dst = mesh.hedges[he].vertex;
                            if (src == vi) toSelect.insert(dst);
                            if (dst == vi) toSelect.insert(src);
                        }
                    }
                    for (int vi : toSelect) mesh.verts[vi].selected = true;
                } else if (g_uiState.selectMode == rf::SelectMode::Face) {
                    std::set<int> toSelect;
                    for (int fi = 0; fi < (int)mesh.faces.size(); fi++) {
                        if (!mesh.faces[fi].selected) continue;
                        // Walk edges, find twin faces
                        int start = mesh.faces[fi].edge;
                        int cur = start;
                        do {
                            int tw = mesh.hedges[cur].twin;
                            if (tw >= 0 && mesh.hedges[tw].face >= 0)
                                toSelect.insert(mesh.hedges[tw].face);
                            cur = mesh.hedges[cur].next;
                        } while (cur != start);
                    }
                    for (int fi : toSelect) mesh.faces[fi].selected = true;
                } else if (g_uiState.selectMode == rf::SelectMode::Edge) {
                    std::set<int> toSelect;
                    for (int ei = 0; ei < (int)mesh.edges.size(); ei++) {
                        if (!mesh.edges[ei].selected) continue;
                        int he = mesh.edges[ei].he;
                        int va = mesh.hedges[mesh.hedges[he].prev].vertex;
                        int vb = mesh.hedges[he].vertex;
                        // Find all edges connected to va or vb
                        for (int ej = 0; ej < (int)mesh.edges.size(); ej++) {
                            int h2 = mesh.edges[ej].he;
                            int a2 = mesh.hedges[mesh.hedges[h2].prev].vertex;
                            int b2 = mesh.hedges[h2].vertex;
                            if (a2 == va || a2 == vb || b2 == va || b2 == vb)
                                toSelect.insert(ej);
                        }
                    }
                    for (int ei : toSelect) mesh.edges[ei].selected = true;
                }
            }
            break;

        case GLFW_KEY_MINUS:
        case GLFW_KEY_KP_SUBTRACT:
            if ((mods & GLFW_MOD_CONTROL) && !g_meshes.empty() && g_uiState.selectMode != rf::SelectMode::Object) {
                auto& mesh = g_meshes[g_selectedMesh];
                if (g_uiState.selectMode == rf::SelectMode::Vertex) {
                    // Deselect verts that have any unselected neighbor
                    std::set<int> toDeselect;
                    for (int vi = 0; vi < (int)mesh.verts.size(); vi++) {
                        if (!mesh.verts[vi].selected) continue;
                        bool boundary = false;
                        for (int he = 0; he < (int)mesh.hedges.size() && !boundary; he++) {
                            int src = mesh.hedges[mesh.hedges[he].prev].vertex;
                            int dst = mesh.hedges[he].vertex;
                            if (src == vi && !mesh.verts[dst].selected) boundary = true;
                            if (dst == vi && !mesh.verts[src].selected) boundary = true;
                        }
                        if (boundary) toDeselect.insert(vi);
                    }
                    for (int vi : toDeselect) mesh.verts[vi].selected = false;
                } else if (g_uiState.selectMode == rf::SelectMode::Face) {
                    std::set<int> toDeselect;
                    for (int fi = 0; fi < (int)mesh.faces.size(); fi++) {
                        if (!mesh.faces[fi].selected) continue;
                        bool boundary = false;
                        int start = mesh.faces[fi].edge;
                        int cur = start;
                        do {
                            int tw = mesh.hedges[cur].twin;
                            if (tw < 0 || mesh.hedges[tw].face < 0 || !mesh.faces[mesh.hedges[tw].face].selected)
                                boundary = true;
                            cur = mesh.hedges[cur].next;
                        } while (cur != start && !boundary);
                        if (boundary) toDeselect.insert(fi);
                    }
                    for (int fi : toDeselect) mesh.faces[fi].selected = false;
                } else if (g_uiState.selectMode == rf::SelectMode::Edge) {
                    std::set<int> toDeselect;
                    for (int ei = 0; ei < (int)mesh.edges.size(); ei++) {
                        if (!mesh.edges[ei].selected) continue;
                        int he = mesh.edges[ei].he;
                        int va = mesh.hedges[mesh.hedges[he].prev].vertex;
                        int vb = mesh.hedges[he].vertex;
                        bool boundary = false;
                        for (int ej = 0; ej < (int)mesh.edges.size() && !boundary; ej++) {
                            if (ej == ei || !mesh.edges[ej].selected) continue;
                            // skip — only check unselected neighbors
                        }
                        // Check if any connected edge is unselected
                        for (int ej = 0; ej < (int)mesh.edges.size() && !boundary; ej++) {
                            if (mesh.edges[ej].selected) continue;
                            int h2 = mesh.edges[ej].he;
                            int a2 = mesh.hedges[mesh.hedges[h2].prev].vertex;
                            int b2 = mesh.hedges[h2].vertex;
                            if (a2 == va || a2 == vb || b2 == va || b2 == vb)
                                boundary = true;
                        }
                        if (boundary) toDeselect.insert(ei);
                    }
                    for (int ei : toDeselect) mesh.edges[ei].selected = false;
                }
            }
            break;

        // A = select all / deselect all
        case GLFW_KEY_A:
            if (!g_meshes.empty()) {
                auto& mesh = g_meshes[g_selectedMesh];
                // If anything selected, deselect all; else select all
                bool anySel = false;
                for (auto& v : mesh.verts) if (v.selected) { anySel = true; break; }
                if (!anySel) for (auto& e : mesh.edges) if (e.selected) { anySel = true; break; }
                if (!anySel) for (auto& f : mesh.faces) if (f.selected) { anySel = true; break; }

                if (anySel) {
                    mesh.deselect_all();
                } else {
                    if (g_uiState.selectMode == rf::SelectMode::Vertex)
                        for (auto& v : mesh.verts) v.selected = true;
                    else if (g_uiState.selectMode == rf::SelectMode::Edge)
                        for (auto& e : mesh.edges) e.selected = true;
                    else if (g_uiState.selectMode == rf::SelectMode::Face)
                        for (auto& f : mesh.faces) f.selected = true;
                }
            }
            break;

        // Numpad views (Blender-style)
        case GLFW_KEY_1:
        case GLFW_KEY_KP_1: // Front / Back
        {
            bool back = (mods & GLFW_MOD_SHIFT);
            g_camAnimStartTarget = g_camera.target;
            g_camAnimStartDist = g_camera.distance;
            g_camAnimEndTarget = g_camera.target;
            g_camAnimEndDist = g_camera.distance;
            g_camAnimStartYaw = g_camera.yaw;
            g_camAnimStartPitch = g_camera.pitch;
            g_camAnimEndYaw = back ? 180.0f : 0.0f;
            g_camAnimEndPitch = 0.0f;
            g_camAnimT = 0.0f;
            g_camAnimating = true;
            break;
        }
        case GLFW_KEY_3:
        case GLFW_KEY_KP_3: // Right / Left
        {
            bool left = (mods & GLFW_MOD_SHIFT);
            g_camAnimStartTarget = g_camera.target;
            g_camAnimStartDist = g_camera.distance;
            g_camAnimEndTarget = g_camera.target;
            g_camAnimEndDist = g_camera.distance;
            g_camAnimStartYaw = g_camera.yaw;
            g_camAnimStartPitch = g_camera.pitch;
            g_camAnimEndYaw = left ? -90.0f : 90.0f;
            g_camAnimEndPitch = 0.0f;
            g_camAnimT = 0.0f;
            g_camAnimating = true;
            break;
        }
        case GLFW_KEY_7:
        case GLFW_KEY_KP_7: // Top / Bottom
        {
            bool bottom = (mods & GLFW_MOD_SHIFT);
            g_camAnimStartTarget = g_camera.target;
            g_camAnimStartDist = g_camera.distance;
            g_camAnimEndTarget = g_camera.target;
            g_camAnimEndDist = g_camera.distance;
            g_camAnimStartYaw = g_camera.yaw;
            g_camAnimStartPitch = g_camera.pitch;
            g_camAnimEndYaw = g_camera.yaw;
            g_camAnimEndPitch = bottom ? -89.0f : 89.0f;
            g_camAnimT = 0.0f;
            g_camAnimating = true;
            break;
        }
        case GLFW_KEY_5:
        case GLFW_KEY_KP_5: // Toggle ortho/perspective
            g_camera.ortho = !g_camera.ortho;
            break;
    }
}

// ---------------------------------------------------------------------------
// Transform mode update (called each frame when grabbing/scaling/rotating)
// ---------------------------------------------------------------------------
static void update_transform(GLFWwindow* win)
{
    if (g_transformMode == 0 || g_meshes.empty()) return;

    double mx, my;
    glfwGetCursorPos(win, &mx, &my);

    auto& mesh = g_meshes[g_selectedMesh];

    if (g_transformMode == 1) {
        // GRAB: project mouse onto camera plane at grab center depth
        glm::vec3 rayO, rayD;
        screen_to_ray(mx, my, rayO, rayD);

        glm::vec3 rayO0, rayD0;
        screen_to_ray(g_grab_startMX, g_grab_startMY, rayO0, rayD0);

        glm::vec3 camPos = g_camera.get_position();
        glm::vec3 camDir = glm::normalize(g_camera.target - camPos);

        float denom = glm::dot(rayD, camDir);
        float denom0 = glm::dot(rayD0, camDir);
        if (std::abs(denom) < 1e-6f || std::abs(denom0) < 1e-6f) return;

        float t  = glm::dot(g_grab_center - rayO,  camDir) / denom;
        float t0 = glm::dot(g_grab_center - rayO0, camDir) / denom0;

        glm::vec3 hitNow   = rayO  + rayD  * t;
        glm::vec3 hitStart = rayO0 + rayD0 * t0;
        glm::vec3 delta = hitNow - hitStart;

        // Axis constraint
        if (g_grab_axis == 0) delta = glm::vec3(delta.x, 0, 0);
        else if (g_grab_axis == 1) delta = glm::vec3(0, delta.y, 0);
        else if (g_grab_axis == 2) delta = glm::vec3(0, 0, delta.z);

        for (int i = 0; i < (int)g_grab_vertIdx.size(); i++)
            mesh.verts[g_grab_vertIdx[i]].pos = g_grab_origPos[i] + delta;

    } else if (g_transformMode == 2) {
        // SCALE: distance from grab center on screen determines scale factor
        // Project grab center to screen
        glm::mat4 vp = g_projMat * g_viewMat;
        glm::vec4 cp = vp * glm::vec4(g_grab_center, 1.0f);
        float screenCX = (cp.x / cp.w * 0.5f + 0.5f) * g_vpW + g_vpX;
        float screenCY = (1.0f - (cp.y / cp.w * 0.5f + 0.5f)) * g_vpH + rf::ui_top_bar_height();

        float startDist = (float)std::sqrt(
            (g_grab_startMX - screenCX) * (g_grab_startMX - screenCX) +
            (g_grab_startMY - screenCY) * (g_grab_startMY - screenCY));
        float curDist = (float)std::sqrt(
            (mx - screenCX) * (mx - screenCX) +
            (my - screenCY) * (my - screenCY));

        if (startDist < 1.0f) startDist = 1.0f;
        float scaleFactor = curDist / startDist;

        glm::vec3 center = g_grab_center - mesh.position;
        for (int i = 0; i < (int)g_grab_vertIdx.size(); i++) {
            glm::vec3 offset = g_grab_origPos[i] - center;
            if (g_grab_axis == 0) offset = glm::vec3(offset.x * scaleFactor, offset.y, offset.z);
            else if (g_grab_axis == 1) offset = glm::vec3(offset.x, offset.y * scaleFactor, offset.z);
            else if (g_grab_axis == 2) offset = glm::vec3(offset.x, offset.y, offset.z * scaleFactor);
            else offset *= scaleFactor;
            mesh.verts[g_grab_vertIdx[i]].pos = center + offset;
        }

    } else if (g_transformMode == 3) {
        // ROTATE: angle from grab center on screen
        glm::mat4 vp = g_projMat * g_viewMat;
        glm::vec4 cp = vp * glm::vec4(g_grab_center, 1.0f);
        float screenCX = (cp.x / cp.w * 0.5f + 0.5f) * g_vpW + g_vpX;
        float screenCY = (1.0f - (cp.y / cp.w * 0.5f + 0.5f)) * g_vpH + rf::ui_top_bar_height();

        float startAngle = (float)std::atan2(g_grab_startMY - screenCY, g_grab_startMX - screenCX);
        float curAngle = (float)std::atan2(my - screenCY, mx - screenCX);
        float angle = curAngle - startAngle;

        // Determine rotation axis
        glm::vec3 axis;
        if (g_grab_axis == 0) axis = glm::vec3(1, 0, 0);
        else if (g_grab_axis == 1) axis = glm::vec3(0, 1, 0);
        else if (g_grab_axis == 2) axis = glm::vec3(0, 0, 1);
        else {
            // Free rotation: rotate around view axis
            glm::vec3 camPos = g_camera.get_position();
            axis = glm::normalize(camPos - g_camera.target);
        }

        glm::mat4 rot = glm::rotate(glm::mat4(1.0f), angle, axis);
        glm::vec3 center = g_grab_center - mesh.position;

        for (int i = 0; i < (int)g_grab_vertIdx.size(); i++) {
            glm::vec3 offset = g_grab_origPos[i] - center;
            glm::vec3 rotated = glm::vec3(rot * glm::vec4(offset, 1.0f));
            mesh.verts[g_grab_vertIdx[i]].pos = center + rotated;
        }
    }

    mesh.recalc_normals();
    mesh.rebuild_gpu();
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

    // Cache viewport for picking
    g_vpX = vpX; g_vpY = vpY; g_vpW = vpW; g_vpH = vpH;

    glViewport(vpX, vpY, vpW, vpH);
    glScissor(vpX, vpY, vpW, vpH);
    glEnable(GL_SCISSOR_TEST);

    glClearColor(kBgColor.x, kBgColor.y, kBgColor.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    float aspect = (float)vpW / (float)vpH;
    glm::mat4 view = g_camera.get_view();
    glm::mat4 proj = g_camera.get_projection(aspect);
    glm::mat4 vp = proj * view;

    // Cache matrices for picking
    g_viewMat = view;
    g_projMat = proj;

    // Grid
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    g_gridShader.use();
    g_gridShader.set_mat4("uMVP", vp);
    g_grid.draw();
    glDisable(GL_BLEND);

    // Meshes — solid pass
    glEnable(GL_DEPTH_TEST);

    // Push mesh slightly back so edge/vert lines render on top
    bool needOffset = (g_uiState.editorMode == rf::EditorMode::Model &&
        (g_uiState.selectMode == rf::SelectMode::Vertex ||
         g_uiState.selectMode == rf::SelectMode::Edge ||
         g_uiState.selectMode == rf::SelectMode::Face));
    if (needOffset) {
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.0f, 1.0f);
    }

    g_meshShader.use();
    // Lighting: unlit = fullbright, textured = user-controlled angle, solid = camera-following
    if (g_uiState.unlit) {
        g_meshShader.set_vec3("uLightDir", glm::vec3(0, 1, 0));
        g_meshShader.set_float("uAmbient", 1.0f);
    } else if (g_uiState.viewMode == rf::ViewMode::Textured) {
        float rx = glm::radians(g_uiState.lightAngleX);
        float ry = glm::radians(g_uiState.lightAngleY);
        glm::vec3 lightDir = glm::normalize(glm::vec3(sinf(rx) * cosf(ry), sinf(ry), cosf(rx) * cosf(ry)));
        if (g_uiState.lightFollowCam) {
            // Rotate the light offset relative to camera orientation
            glm::vec3 camFwd = glm::normalize(g_camera.get_position() - g_camera.target);
            glm::vec3 camRight = glm::normalize(glm::cross(glm::vec3(0, 1, 0), camFwd));
            glm::vec3 camUp = glm::cross(camFwd, camRight);
            glm::mat3 camBasis(camRight, camUp, camFwd);
            lightDir = glm::normalize(camBasis * lightDir);
        }
        g_meshShader.set_vec3("uLightDir", lightDir);
        g_meshShader.set_float("uAmbient", 0.25f);
    } else {
        g_meshShader.set_vec3("uLightDir", glm::normalize(g_camera.get_position() - g_camera.target));
        g_meshShader.set_float("uAmbient", 0.25f);
    }

    // Toon & Fresnel — both use the ramp
    g_meshShader.set_int("uToon", g_uiState.toon ? 1 : 0);
    g_meshShader.set_int("uFresnel", g_uiState.fresnel ? 1 : 0);
    if (g_uiState.toon || g_uiState.fresnel) {
        g_meshShader.set_vec3("uViewPos", g_camera.get_position());
        auto& stops = g_uiState.rampStops;
        int count = std::min((int)stops.size(), 16);
        g_meshShader.set_int("uRampCount", count);
        g_meshShader.set_int("uRampInterp", (int)g_uiState.rampInterp);
        float pos[16] = {}, val[16] = {};
        for (int i = 0; i < count; i++) {
            pos[i] = stops[i].first;
            val[i] = stops[i].second;
        }
        glUniform1fv(glGetUniformLocation(g_meshShader.id, "uRampPos"), count, pos);
        glUniform1fv(glGetUniformLocation(g_meshShader.id, "uRampVal"), count, val);
    }

    // Specular — independent ramp
    g_meshShader.set_int("uSpecular", g_uiState.specular ? 1 : 0);
    if (g_uiState.specular) {
        g_meshShader.set_vec3("uViewPos", g_camera.get_position());
        g_meshShader.set_float("uSpecRoughness", g_uiState.specRoughness);
        auto& stops = g_uiState.specRampStops;
        int count = std::min((int)stops.size(), 16);
        g_meshShader.set_int("uSpecRampCount", count);
        g_meshShader.set_int("uSpecRampInterp", (int)g_uiState.specRampInterp);
        float pos[16] = {}, val[16] = {};
        for (int i = 0; i < count; i++) {
            pos[i] = stops[i].first;
            val[i] = stops[i].second;
        }
        glUniform1fv(glGetUniformLocation(g_meshShader.id, "uSpecRampPos"), count, pos);
        glUniform1fv(glGetUniformLocation(g_meshShader.id, "uSpecRampVal"), count, val);
    }

    for (auto& mesh : g_meshes) {
        if (!mesh.visible) continue;
        glm::mat4 model = glm::translate(glm::mat4(1.0f), mesh.position);
        glm::mat4 mvp = vp * model;
        g_meshShader.set_mat4("uMVP", mvp);
        g_meshShader.set_mat4("uModel", model);
        g_meshShader.set_vec3("uColor", kMeshColor);

        glBindVertexArray(mesh.vao);
        glDrawArrays(GL_TRIANGLES, 0, mesh.triCount * 3);
    }

    if (needOffset) {
        glDisable(GL_POLYGON_OFFSET_FILL);
    }

    // --- Object mode silhouette outline (back-face method) ---
    if (!g_meshes.empty() && g_objectSelected && g_uiState.selectMode == rf::SelectMode::Object && g_meshes[g_selectedMesh].visible) {
        auto& mesh = g_meshes[g_selectedMesh];

        float outlineScale = 1.008f;
        // Compute actual center of vertices
        glm::vec3 center(0);
        for (auto& v : mesh.verts) center += v.pos;
        center /= (float)mesh.verts.size();
        center += mesh.position;
        // Scale around the vertex center, not mesh.position
        glm::mat4 scaledModel = glm::translate(glm::mat4(1.0f), center)
            * glm::scale(glm::mat4(1.0f), glm::vec3(outlineScale))
            * glm::translate(glm::mat4(1.0f), -center)
            * glm::translate(glm::mat4(1.0f), mesh.position);

        g_wireShader.use();
        g_wireShader.set_mat4("uMVP", vp * scaledModel);
        g_wireShader.set_vec3("uColor", {1.0f, 1.0f, 1.0f});

        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);  // only draw back faces of the larger mesh
        glBindVertexArray(mesh.vao);
        glDrawArrays(GL_TRIANGLES, 0, mesh.triCount * 3);
        glCullFace(GL_BACK);
        glDisable(GL_CULL_FACE);
    }

    // --- Selection overlay (Model mode edit sub-modes only) ---
    if (!g_meshes.empty() && g_uiState.editorMode == rf::EditorMode::Model && g_uiState.selectMode != rf::SelectMode::Object && g_meshes[g_selectedMesh].visible) {
        auto& mesh = g_meshes[g_selectedMesh];
        glm::mat4 model = glm::translate(glm::mat4(1.0f), mesh.position);
        glm::mat4 mvp = vp * model;

        if (!g_selVAO) {
            glGenVertexArrays(1, &g_selVAO);
            glGenBuffers(1, &g_selVBO);
        }

        // Precompute front-facing faces for back-face culling of overlays
        glm::vec3 camPos = g_camera.get_position() - mesh.position;
        std::vector<bool> faceFront(mesh.faces.size(), false);
        for (int fi = 0; fi < (int)mesh.faces.size(); fi++) {
            std::vector<int> fv;
            int start = mesh.faces[fi].edge;
            int cur = start;
            do { fv.push_back(mesh.hedges[cur].vertex); cur = mesh.hedges[cur].next; }
            while (cur != start && (int)fv.size() < 64);
            if (fv.size() < 3) continue;
            glm::vec3 fn = glm::cross(mesh.verts[fv[1]].pos - mesh.verts[fv[0]].pos,
                                       mesh.verts[fv[2]].pos - mesh.verts[fv[0]].pos);
            glm::vec3 fc(0);
            for (int j : fv) fc += mesh.verts[j].pos;
            fc /= (float)fv.size();
            faceFront[fi] = glm::dot(fn, camPos - fc) > 0.0f;
        }
        // Vertex visible if on at least one front-facing face
        std::vector<bool> vertVis(mesh.verts.size(), false);
        for (int fi = 0; fi < (int)mesh.faces.size(); fi++) {
            if (!faceFront[fi]) continue;
            int start = mesh.faces[fi].edge; int cur = start;
            do { vertVis[mesh.hedges[cur].vertex] = true; cur = mesh.hedges[cur].next; } while (cur != start);
        }
        // Edge visible if at least one adjacent face is front-facing
        auto isEdgeVis = [&](int ei) {
            int he = mesh.edges[ei].he;
            int f1 = mesh.hedges[he].face;
            if (f1 >= 0 && faceFront[f1]) return true;
            int twin = mesh.hedges[he].twin;
            if (twin >= 0) { int f2 = mesh.hedges[twin].face; if (f2 >= 0 && faceFront[f2]) return true; }
            return false;
        };

        glBindVertexArray(g_selVAO);
        g_wireShader.use();
        g_wireShader.set_mat4("uMVP", mvp);

        std::vector<float> buf;

        if (g_uiState.selectMode == rf::SelectMode::Vertex) {
            // Draw front-facing edges in dark color
            buf.clear();
            for (int ei = 0; ei < (int)mesh.edges.size(); ei++) {
                if (!isEdgeVis(ei)) continue;
                int he = mesh.edges[ei].he;
                int va = mesh.hedges[mesh.hedges[he].prev].vertex;
                int vb = mesh.hedges[he].vertex;
                auto& pa = mesh.verts[va].pos;
                auto& pb = mesh.verts[vb].pos;
                buf.push_back(pa.x); buf.push_back(pa.y); buf.push_back(pa.z);
                buf.push_back(pb.x); buf.push_back(pb.y); buf.push_back(pb.z);
            }
            if (!buf.empty()) {
                glBindBuffer(GL_ARRAY_BUFFER, g_selVBO);
                glBufferData(GL_ARRAY_BUFFER, buf.size() * sizeof(float), buf.data(), GL_DYNAMIC_DRAW);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
                glEnableVertexAttribArray(0);
                g_wireShader.set_vec3("uColor", kWireColor);
                glLineWidth(1.5f);
                glDrawArrays(GL_LINES, 0, (int)buf.size() / 3);
                glLineWidth(1.0f);
            }

            // Draw front-facing edges connected to selected verts with gradient falloff
            buf.clear();
            for (int ei = 0; ei < (int)mesh.edges.size(); ei++) {
                if (!isEdgeVis(ei)) continue;
                int he = mesh.edges[ei].he;
                int va = mesh.hedges[mesh.hedges[he].prev].vertex;
                int vb = mesh.hedges[he].vertex;
                bool sa = mesh.verts[va].selected;
                bool sb = mesh.verts[vb].selected;
                if (!sa && !sb) continue;
                auto& pa = mesh.verts[va].pos;
                auto& pb = mesh.verts[vb].pos;
                glm::vec3 ca = sa ? glm::vec3(kSelectColor) : glm::vec3(kWireColor);
                glm::vec3 cb = sb ? glm::vec3(kSelectColor) : glm::vec3(kWireColor);
                buf.push_back(pa.x); buf.push_back(pa.y); buf.push_back(pa.z);
                buf.push_back(ca.x); buf.push_back(ca.y); buf.push_back(ca.z);
                buf.push_back(pb.x); buf.push_back(pb.y); buf.push_back(pb.z);
                buf.push_back(cb.x); buf.push_back(cb.y); buf.push_back(cb.z);
            }
            if (!buf.empty()) {
                g_vcShader.use();
                g_vcShader.set_mat4("uMVP", mvp);
                glBindBuffer(GL_ARRAY_BUFFER, g_selVBO);
                glBufferData(GL_ARRAY_BUFFER, buf.size() * sizeof(float), buf.data(), GL_DYNAMIC_DRAW);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
                glEnableVertexAttribArray(1);
                glLineWidth(2.5f);
                glDepthFunc(GL_LEQUAL);
                glDrawArrays(GL_LINES, 0, (int)buf.size() / 6);
                glDepthFunc(GL_LESS);
                glLineWidth(1.0f);
                g_wireShader.use();
                g_wireShader.set_mat4("uMVP", mvp);
            }

            // Selected visible verts as orange dots
            buf.clear();
            for (int i = 0; i < (int)mesh.verts.size(); i++) {
                if (mesh.verts[i].selected && vertVis[i]) {
                    buf.push_back(mesh.verts[i].pos.x);
                    buf.push_back(mesh.verts[i].pos.y);
                    buf.push_back(mesh.verts[i].pos.z);
                }
            }
            if (!buf.empty()) {
                glBindBuffer(GL_ARRAY_BUFFER, g_selVBO);
                glBufferData(GL_ARRAY_BUFFER, buf.size() * sizeof(float), buf.data(), GL_DYNAMIC_DRAW);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
                glEnableVertexAttribArray(0);
                g_wireShader.set_vec3("uColor", kSelectColor);
                glPointSize(6.0f);
                glDepthFunc(GL_LEQUAL);
                glDrawArrays(GL_POINTS, 0, (int)buf.size() / 3);
                glDepthFunc(GL_LESS);
            }

            // Unselected visible verts as small dark dots
            buf.clear();
            for (int i = 0; i < (int)mesh.verts.size(); i++) {
                if (!mesh.verts[i].selected && vertVis[i]) {
                    buf.push_back(mesh.verts[i].pos.x);
                    buf.push_back(mesh.verts[i].pos.y);
                    buf.push_back(mesh.verts[i].pos.z);
                }
            }
            if (!buf.empty()) {
                glBindBuffer(GL_ARRAY_BUFFER, g_selVBO);
                glBufferData(GL_ARRAY_BUFFER, buf.size() * sizeof(float), buf.data(), GL_DYNAMIC_DRAW);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
                glEnableVertexAttribArray(0);
                g_wireShader.set_vec3("uColor", {0.1f, 0.1f, 0.1f});
                glPointSize(5.0f);
                glDrawArrays(GL_POINTS, 0, (int)buf.size() / 3);
            }
        }
        else if (g_uiState.selectMode == rf::SelectMode::Edge) {
            // Draw front-facing edges in dark color
            buf.clear();
            for (int ei = 0; ei < (int)mesh.edges.size(); ei++) {
                if (!isEdgeVis(ei)) continue;
                int he = mesh.edges[ei].he;
                int va = mesh.hedges[mesh.hedges[he].prev].vertex;
                int vb = mesh.hedges[he].vertex;
                auto& pa = mesh.verts[va].pos;
                auto& pb = mesh.verts[vb].pos;
                buf.push_back(pa.x); buf.push_back(pa.y); buf.push_back(pa.z);
                buf.push_back(pb.x); buf.push_back(pb.y); buf.push_back(pb.z);
            }
            if (!buf.empty()) {
                glBindBuffer(GL_ARRAY_BUFFER, g_selVBO);
                glBufferData(GL_ARRAY_BUFFER, buf.size() * sizeof(float), buf.data(), GL_DYNAMIC_DRAW);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
                glEnableVertexAttribArray(0);
                g_wireShader.set_vec3("uColor", kWireColor);
                glLineWidth(1.5f);
                glDrawArrays(GL_LINES, 0, (int)buf.size() / 3);
                glLineWidth(1.0f);
            }

            // Draw selected front-facing edges in orange
            buf.clear();
            for (int ei = 0; ei < (int)mesh.edges.size(); ei++) {
                if (!mesh.edges[ei].selected || !isEdgeVis(ei)) continue;
                int he = mesh.edges[ei].he;
                int va = mesh.hedges[mesh.hedges[he].prev].vertex;
                int vb = mesh.hedges[he].vertex;
                auto& pa = mesh.verts[va].pos;
                auto& pb = mesh.verts[vb].pos;
                buf.push_back(pa.x); buf.push_back(pa.y); buf.push_back(pa.z);
                buf.push_back(pb.x); buf.push_back(pb.y); buf.push_back(pb.z);
            }
            if (!buf.empty()) {
                glBindBuffer(GL_ARRAY_BUFFER, g_selVBO);
                glBufferData(GL_ARRAY_BUFFER, buf.size() * sizeof(float), buf.data(), GL_DYNAMIC_DRAW);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
                glEnableVertexAttribArray(0);
                g_wireShader.set_vec3("uColor", kSelectColor);
                glLineWidth(2.5f);
                glDepthFunc(GL_LEQUAL);
                glDrawArrays(GL_LINES, 0, (int)buf.size() / 3);
                glDepthFunc(GL_LESS);
                glLineWidth(1.0f);
            }
        }
        else if (g_uiState.selectMode == rf::SelectMode::Face) {
            // Determine which edges border a selected face
            std::vector<bool> edgeOnSel(mesh.edges.size(), false);
            for (int ei = 0; ei < (int)mesh.edges.size(); ei++) {
                int he = mesh.edges[ei].he;
                int f1 = mesh.hedges[he].face;
                if (f1 >= 0 && mesh.faces[f1].selected && faceFront[f1]) { edgeOnSel[ei] = true; continue; }
                int twin = mesh.hedges[he].twin;
                if (twin >= 0) { int f2 = mesh.hedges[twin].face; if (f2 >= 0 && mesh.faces[f2].selected && faceFront[f2]) edgeOnSel[ei] = true; }
            }

            // Draw dark edges on unselected front-facing areas
            buf.clear();
            for (int ei = 0; ei < (int)mesh.edges.size(); ei++) {
                if (!isEdgeVis(ei) || edgeOnSel[ei]) continue;
                int he = mesh.edges[ei].he;
                int va = mesh.hedges[mesh.hedges[he].prev].vertex;
                int vb = mesh.hedges[he].vertex;
                auto& pa = mesh.verts[va].pos;
                auto& pb = mesh.verts[vb].pos;
                buf.push_back(pa.x); buf.push_back(pa.y); buf.push_back(pa.z);
                buf.push_back(pb.x); buf.push_back(pb.y); buf.push_back(pb.z);
            }
            if (!buf.empty()) {
                glBindBuffer(GL_ARRAY_BUFFER, g_selVBO);
                glBufferData(GL_ARRAY_BUFFER, buf.size() * sizeof(float), buf.data(), GL_DYNAMIC_DRAW);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
                glEnableVertexAttribArray(0);
                g_wireShader.set_vec3("uColor", kWireColor);
                glLineWidth(1.5f);
                glDrawArrays(GL_LINES, 0, (int)buf.size() / 3);
                glLineWidth(1.0f);
            }

            // Translucent fill on selected front-facing faces
            buf.clear();
            for (int fi = 0; fi < (int)mesh.faces.size(); fi++) {
                if (!mesh.faces[fi].selected || !faceFront[fi]) continue;
                std::vector<int> fv;
                int start = mesh.faces[fi].edge;
                int cur = start;
                do { fv.push_back(mesh.hedges[cur].vertex); cur = mesh.hedges[cur].next; }
                while (cur != start && (int)fv.size() < 64);
                for (int i = 1; i + 1 < (int)fv.size(); i++) {
                    int vi[3] = {fv[0], fv[i], fv[i+1]};
                    for (int k = 0; k < 3; k++) {
                        auto& p = mesh.verts[vi[k]].pos;
                        buf.push_back(p.x); buf.push_back(p.y); buf.push_back(p.z);
                    }
                }
            }
            if (!buf.empty()) {
                glBindBuffer(GL_ARRAY_BUFFER, g_selVBO);
                glBufferData(GL_ARRAY_BUFFER, buf.size() * sizeof(float), buf.data(), GL_DYNAMIC_DRAW);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
                glEnableVertexAttribArray(0);
                g_wireShader.set_vec3("uColor", kSelectColor);
                glEnable(GL_BLEND);
                glBlendColor(0, 0, 0, 0.3f);
                glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA);
                glDepthFunc(GL_LEQUAL);
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(-2.0f, -2.0f);
                glDrawArrays(GL_TRIANGLES, 0, (int)buf.size() / 3);
                glDisable(GL_POLYGON_OFFSET_FILL);
                glDepthFunc(GL_LESS);
                glDisable(GL_BLEND);
            }

            // Orange edges on selected faces (drawn on top)
            buf.clear();
            for (int ei = 0; ei < (int)mesh.edges.size(); ei++) {
                if (!isEdgeVis(ei) || !edgeOnSel[ei]) continue;
                int he = mesh.edges[ei].he;
                int va = mesh.hedges[mesh.hedges[he].prev].vertex;
                int vb = mesh.hedges[he].vertex;
                auto& pa = mesh.verts[va].pos;
                auto& pb = mesh.verts[vb].pos;
                buf.push_back(pa.x); buf.push_back(pa.y); buf.push_back(pa.z);
                buf.push_back(pb.x); buf.push_back(pb.y); buf.push_back(pb.z);
            }
            if (!buf.empty()) {
                glBindBuffer(GL_ARRAY_BUFFER, g_selVBO);
                glBufferData(GL_ARRAY_BUFFER, buf.size() * sizeof(float), buf.data(), GL_DYNAMIC_DRAW);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
                glEnableVertexAttribArray(0);
                g_wireShader.set_vec3("uColor", kSelectColor);
                glLineWidth(1.5f);
                glDisable(GL_DEPTH_TEST);
                glDrawArrays(GL_LINES, 0, (int)buf.size() / 3);
                glEnable(GL_DEPTH_TEST);
                glLineWidth(1.0f);
            }
        }

        glBindVertexArray(0);
    }

    // Draw axis indicator during transform (all modes)
    if (!g_meshes.empty() && g_transformMode != 0 && g_grab_axis >= 0) {
        auto& mesh = g_meshes[g_selectedMesh];
        glm::mat4 model = glm::translate(glm::mat4(1.0f), mesh.position);

        if (!g_selVAO) {
            glGenVertexArrays(1, &g_selVAO);
            glGenBuffers(1, &g_selVBO);
        }

        float len = 2.0f;
        glm::vec3 center = g_grab_center;
        glm::vec3 dir(0);
        dir[g_grab_axis] = len;
        glm::vec3 a = center - dir;
        glm::vec3 b = center + dir;
        std::vector<float> axisBuf = {a.x, a.y, a.z, b.x, b.y, b.z};

        glBindVertexArray(g_selVAO);
        glBindBuffer(GL_ARRAY_BUFFER, g_selVBO);
        glBufferData(GL_ARRAY_BUFFER, axisBuf.size() * sizeof(float), axisBuf.data(), GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        g_wireShader.use();
        g_wireShader.set_mat4("uMVP", vp * glm::mat4(1.0f));
        g_wireShader.set_vec3("uColor", kAxisColors[g_grab_axis]);
        glLineWidth(2.0f);
        glDisable(GL_DEPTH_TEST);
        glDrawArrays(GL_LINES, 0, 2);
        glEnable(GL_DEPTH_TEST);
        glLineWidth(1.0f);
        glBindVertexArray(0);
    }

    glDisable(GL_SCISSOR_TEST);

    // Restore full viewport for UI
    glViewport(0, 0, g_winW, g_winH);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// File dialogs (Windows native)
// ---------------------------------------------------------------------------
#ifdef _WIN32
static std::string file_dialog_save(const char* filter, const char* defExt)
{
    char path[MAX_PATH] = {};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = defExt;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;
    if (GetSaveFileNameA(&ofn)) return std::string(path);
    return {};
}

static std::string file_dialog_open(const char* filter)
{
    char path[MAX_PATH] = {};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn)) return std::string(path);
    return {};
}
#endif

// ---------------------------------------------------------------------------
// Handle file menu actions
// ---------------------------------------------------------------------------
static void handle_ui_actions(GLFWwindow* win)
{
    if (g_uiState.pendingAction == rf::UIAction::None) return;

    rf::UIAction action = g_uiState.pendingAction;
    g_uiState.pendingAction = rf::UIAction::None;

    switch (action) {
        case rf::UIAction::New:
            g_meshes.clear();
            g_meshes.push_back(rf::Mesh::create_cube());
            g_selectedMesh = 0;
            g_uiState.filename = "untitled.rflw";
            g_uiState.filepath.clear();
            g_uiState.fileModified = false;
            break;

        case rf::UIAction::Save:
            if (g_uiState.filepath.empty()) {
                // No path yet — do Save As
                g_uiState.pendingAction = rf::UIAction::SaveAs;
                return;
            }
            if (rf::save_project(g_uiState.filepath, g_meshes, &g_uiState)) {
                g_uiState.fileModified = false;
            }
            break;

        case rf::UIAction::SaveAs: {
#ifdef _WIN32
            std::string path = file_dialog_save("Reflow Project (*.rflw)\0*.rflw\0All Files\0*.*\0", "rflw");
            if (!path.empty()) {
                if (rf::save_project(path, g_meshes, &g_uiState)) {
                    g_uiState.filepath = path;
                    // Extract filename from path
                    size_t sep = path.find_last_of("\\/");
                    g_uiState.filename = (sep != std::string::npos) ? path.substr(sep + 1) : path;
                    g_uiState.fileModified = false;
                }
            }
#endif
            break;
        }

        case rf::UIAction::Open: {
#ifdef _WIN32
            std::string path = file_dialog_open("Reflow Project (*.rflw)\0*.rflw\0All Files\0*.*\0");
            if (!path.empty()) {
                if (rf::load_project(path, g_meshes, &g_uiState)) {
                    g_selectedMesh = 0;
                    g_uiState.filepath = path;
                    size_t sep = path.find_last_of("\\/");
                    g_uiState.filename = (sep != std::string::npos) ? path.substr(sep + 1) : path;
                    g_uiState.fileModified = false;
                }
            }
#endif
            break;
        }

        case rf::UIAction::Export: {
#ifdef _WIN32
            std::string path = file_dialog_save("OBJ File (*.obj)\0*.obj\0All Files\0*.*\0", "obj");
            if (!path.empty() && !g_meshes.empty()) {
                g_meshes[g_selectedMesh].export_obj(path);
            }
#endif
            break;
        }

        default: break;
    }
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
    glfwWindowHint(GLFW_STENCIL_BITS, 8);

    GLFWwindow* win = glfwCreateWindow(g_winW, g_winH, rf::kAppName, nullptr, nullptr);
    if (!win) {
        fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(win);
    glfwSwapInterval(1);

    // Set window icon (title bar + taskbar) — multiple sizes for crisp scaling
    {
#ifdef _WIN32
        char exePath[MAX_PATH];
        GetModuleFileNameA(nullptr, exePath, MAX_PATH);
        std::string dir(exePath);
        dir = dir.substr(0, dir.find_last_of("\\/") + 1);
#else
        std::string dir = "";
#endif
        const char* iconFiles[] = {
            "res/reflow_icon_16.png",
            "res/reflow_icon_32.png",
            "res/reflow_icon_48.png",
            "res/reflow_icon.png",
        };
        GLFWimage imgs[4];
        unsigned char* pxData[4] = {};
        int imgCount = 0;
        for (int i = 0; i < 4; i++) {
            std::string path = dir + iconFiles[i];
            int w, h, ch;
            pxData[i] = stbi_load(path.c_str(), &w, &h, &ch, 4);
            if (pxData[i]) {
                imgs[imgCount++] = { w, h, pxData[i] };
            }
        }
        if (imgCount > 0)
            glfwSetWindowIcon(win, imgCount, imgs);
        for (int i = 0; i < 4; i++)
            if (pxData[i]) stbi_image_free(pxData[i]);
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
    g_vcShader = rf::create_vertcolor_shader();

    // Init grid
    g_grid.init();

    // Init UI
    rf::ui_init(win);
    rf::ui_load_settings(g_uiState);
    ImGui::GetIO().FontGlobalScale = g_uiState.uiScale;

    // Wire UI to scene data
    g_uiState.meshes = &g_meshes;
    g_uiState.selectedMesh = &g_selectedMesh;
    g_uiState.objectSelected = &g_objectSelected;

    // Default scene
    g_meshes.push_back(rf::Mesh::create_cube());

    // Main loop
    while (!glfwWindowShouldClose(win)) {
        glfwPollEvents();

        // Update transform mode (grab/scale/rotate)
        update_transform(win);
        handle_ui_actions(win);

        // Force Object mode when not in Model tab
        if (g_uiState.editorMode != rf::EditorMode::Model)
            g_uiState.selectMode = rf::SelectMode::Object;

        // Frame selected: recenter camera on mesh
        if (g_uiState.pendingFrameSelected) {
            g_uiState.pendingFrameSelected = false;
            if (!g_meshes.empty()) {
                auto& mesh = g_meshes[g_selectedMesh];
                glm::vec3 center(0);
                for (auto& v : mesh.verts) center += v.pos;
                center /= (float)mesh.verts.size();
                g_camera.target = center + mesh.position;
            }
        }
        // Zoom to fit selected mesh on double-click (start animation)
        if (g_uiState.pendingZoomSelected) {
            g_uiState.pendingZoomSelected = false;
            if (!g_meshes.empty()) {
                auto& mesh = g_meshes[g_selectedMesh];
                glm::vec3 mn(FLT_MAX), mx(-FLT_MAX);
                for (auto& v : mesh.verts) {
                    mn = glm::min(mn, v.pos);
                    mx = glm::max(mx, v.pos);
                }
                g_camAnimStartTarget = g_camera.target;
                g_camAnimStartDist = g_camera.distance;
                g_camAnimStartYaw = g_camera.yaw;
                g_camAnimStartPitch = g_camera.pitch;
                g_camAnimEndTarget = (mn + mx) * 0.5f + mesh.position;
                g_camAnimEndDist = glm::length(mx - mn) * 2.5f * 0.5f;
                g_camAnimEndYaw = g_camera.yaw;
                g_camAnimEndPitch = g_camera.pitch;
                g_camAnimT = 0.0f;
                g_camAnimating = true;
            }
        }
        // Animate camera slide
        if (g_camAnimating) {
            g_camAnimT += 0.04f;
            if (g_camAnimT >= 1.0f) {
                g_camAnimT = 1.0f;
                g_camAnimating = false;
            }
            float t = g_camAnimT * g_camAnimT * (3.0f - 2.0f * g_camAnimT); // smoothstep
            g_camera.target = glm::mix(g_camAnimStartTarget, g_camAnimEndTarget, t);
            g_camera.distance = glm::mix(g_camAnimStartDist, g_camAnimEndDist, t);
            g_camera.yaw = glm::mix(g_camAnimStartYaw, g_camAnimEndYaw, t);
            g_camera.pitch = glm::mix(g_camAnimStartPitch, g_camAnimEndPitch, t);
        }

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

        // Delete mesh confirmation (X key)
        if (g_openDeleteConfirm) {
            ImGui::OpenPopup("##DeleteMesh");
            g_openDeleteConfirm = false;
        }
        if (ImGui::BeginPopup("##DeleteMesh")) {
            ImGui::Text("Delete \"%s\"?", g_meshes[g_selectedMesh].name.c_str());
            if (ImGui::MenuItem("Delete")) {
                g_meshes.erase(g_meshes.begin() + g_selectedMesh);
                g_objectSelected = false;
                if (g_selectedMesh >= (int)g_meshes.size())
                    g_selectedMesh = std::max(0, (int)g_meshes.size() - 1);
            }
            ImGui::EndPopup();
        }

        // Normals menu (W key)
        if (g_openNormalsMenu) {
            ImGui::OpenPopup("##NormalsMenu");
            g_openNormalsMenu = false;
        }
        if (ImGui::BeginPopup("##NormalsMenu")) {
            if (ImGui::MenuItem("Shade Smooth")) {
                if (!g_meshes.empty()) {
                    g_meshes[g_selectedMesh].shadeSmooth = true;
                    g_meshes[g_selectedMesh].rebuild_gpu();
                }
            }
            if (ImGui::MenuItem("Shade Flat")) {
                if (!g_meshes.empty()) {
                    g_meshes[g_selectedMesh].shadeSmooth = false;
                    g_meshes[g_selectedMesh].rebuild_gpu();
                }
            }
            ImGui::EndPopup();
        }

        // Draw box select rectangle
        if (g_boxSelecting) {
            ImDrawList* dl = ImGui::GetForegroundDrawList();
            ImVec2 a((float)g_boxStartX, (float)g_boxStartY);
            ImVec2 b((float)g_boxEndX, (float)g_boxEndY);
            dl->AddRectFilled(a, b, IM_COL32(100, 150, 255, 40));
            dl->AddRect(a, b, IM_COL32(100, 150, 255, 180), 0, 0, 1.5f);
        }

        rf::ui_render();

        glfwSwapBuffers(win);
    }

    rf::ui_shutdown();
    glfwDestroyWindow(win);
    glfwTerminate();
    return 0;
}
