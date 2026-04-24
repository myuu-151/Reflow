#pragma once

#include "core/reflow.h"
#include "imgui.h"

namespace rf {

// Tool modes
enum class Tool { Select, Move, Rotate, Scale, Box };

// Editor modes (top tabs)
enum class EditorMode { Model, Paint, UV };

// Selection modes
enum class SelectMode { Object, Vertex, Edge, Face };

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

struct UIState {
    Tool currentTool = Tool::Select;
    EditorMode editorMode = EditorMode::Model;
    SelectMode selectMode = SelectMode::Object;
    bool fileModified = false;
    std::string filename = "untitled.rflw";
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

} // namespace rf
