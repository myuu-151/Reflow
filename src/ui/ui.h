#pragma once

#include "core/reflow.h"
#include "imgui.h"

namespace rf {

// Tool modes
enum class Tool { Select, Move, Rotate, Scale, Box };

// Editor modes (top tabs)
enum class EditorMode { Model, Sculpt, Paint, Rig, Animate, UV };

// Selection modes
enum class SelectMode { Object, Vertex, Edge, Face };

// Viewport shading modes
enum class ViewMode { Wireframe, Solid, Textured };

// UI colors matching mockup
namespace Colors {
    inline ImVec4 bg()        { return {0.11f, 0.11f, 0.13f, 1.0f}; }
    inline ImVec4 panel()     { return {0.13f, 0.13f, 0.15f, 1.0f}; }
    inline ImVec4 panelDark() { return {0.10f, 0.10f, 0.12f, 1.0f}; }
    inline ImVec4 accent()    { return {0.40f, 0.30f, 0.65f, 1.0f}; }
    inline ImVec4 accentHi()  { return {0.50f, 0.38f, 0.75f, 1.0f}; }
    inline ImVec4 text()      { return {0.85f, 0.85f, 0.87f, 1.0f}; }
    inline ImVec4 textDim()   { return {0.50f, 0.50f, 0.55f, 1.0f}; }
    inline ImVec4 border()    { return {0.20f, 0.20f, 0.23f, 1.0f}; }
    inline ImVec4 input()     { return {0.16f, 0.16f, 0.19f, 1.0f}; }
    inline ImVec4 green()     { return {0.30f, 0.75f, 0.40f, 1.0f}; }
}

enum class UIAction { None, New, Open, Save, SaveAs, Import, Export };

struct UIState {
    Tool currentTool = Tool::Select;
    EditorMode editorMode = EditorMode::Model;
    SelectMode selectMode = SelectMode::Object;
    ViewMode viewMode = ViewMode::Solid;
    bool fileModified = false;
    std::string filename = "untitled.rflw";
    std::string filepath;  // full path to current project file
    float uiScale = 2.5f;
    UIAction pendingAction = UIAction::None;
    bool pendingFrameSelected = false;
    bool pendingZoomSelected = false;
    int pendingSelectMesh = -1;    // request to select mesh by index
    bool outlinerHovered = false; // outliner panel is hovered (set each frame)

    // Pointers to scene data (set by main)
    std::vector<Mesh>* meshes = nullptr;
    int* selectedMesh = nullptr;
    bool* objectSelected = nullptr;
    float lightAngleX = 0.0f;  // textured mode light horizontal angle
    float lightAngleY = 45.0f; // textured mode light vertical angle
    bool lightFollowCam = false; // textured mode light follows camera
    bool unlit = false;        // fullbright / unlit mode
    bool toon = false;         // toon shading
    bool fresnel = false;      // fresnel effect
    bool specular = false;     // specular highlight
    float specRoughness = 0.5f; // specular roughness (0=sharp, 1=broad)
    // Ramp interpolation
    enum class RampInterp { Ease, Cardinal, Linear, BSpline, Constant };
    RampInterp rampInterp = RampInterp::Linear;
    // Ramp stops: {position (0-1), brightness (0-1)}
    std::vector<std::pair<float,float>> rampStops = {{0.0f, 0.0f}, {1.0f, 1.0f}};
    // Specular ramp
    RampInterp specRampInterp = RampInterp::Linear;
    std::vector<std::pair<float,float>> specRampStops = {{0.0f, 0.0f}, {1.0f, 1.0f}};

    // Outliner context menu state
    int pendingRename = -1;
    int clipboardMesh = -1;
};

void ui_init(GLFWwindow* win);
void ui_shutdown();
void ui_new_frame();
void ui_render();

// Individual panels
void ui_top_bar(UIState& state);
void ui_toolbar(UIState& state);
void ui_objects_panel(UIState& state);
void ui_properties_panel(UIState& state);
void ui_status_bar(UIState& state);
void ui_viewport_overlay(UIState& state);

void ui_apply_theme();
void ui_save_settings(const UIState& state);
void ui_load_settings(UIState& state);

// Scaled layout dimensions
float ui_top_bar_height();
float ui_toolbar_width();
float ui_right_panel_width();
float ui_status_bar_height();

} // namespace rf
