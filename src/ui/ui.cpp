#include "ui/ui.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <cstdio>

namespace rf {

// Layout constants
static constexpr float kTopBarHeight    = 48.0f;
static constexpr float kToolbarWidth    = 160.0f;
static constexpr float kRightPanelWidth = 260.0f;
static constexpr float kStatusBarHeight = 36.0f;

void ui_init(GLFWwindow* win)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // don't save layout

    // Scale up font for readability
    io.FontGlobalScale = 1.4f;

    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    ui_apply_theme();
}

void ui_shutdown()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void ui_new_frame()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ui_render()
{
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void ui_apply_theme()
{
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding    = 0.0f;
    s.FrameRounding     = 4.0f;
    s.GrabRounding      = 3.0f;
    s.TabRounding       = 0.0f;
    s.ScrollbarRounding = 3.0f;
    s.WindowBorderSize  = 0.0f;
    s.FrameBorderSize   = 0.0f;
    s.PopupBorderSize   = 1.0f;
    s.WindowPadding     = {12, 12};
    s.FramePadding      = {8, 6};
    s.ItemSpacing       = {8, 6};
    s.ItemInnerSpacing  = {6, 4};
    s.ScrollbarSize     = 10.0f;

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg]             = Colors::panel();
    c[ImGuiCol_PopupBg]              = Colors::panelDark();
    c[ImGuiCol_Border]               = Colors::border();
    c[ImGuiCol_FrameBg]              = Colors::input();
    c[ImGuiCol_FrameBgHovered]       = {0.20f, 0.20f, 0.24f, 1.0f};
    c[ImGuiCol_FrameBgActive]        = {0.24f, 0.24f, 0.28f, 1.0f};
    c[ImGuiCol_TitleBg]              = Colors::panelDark();
    c[ImGuiCol_TitleBgActive]        = Colors::panelDark();
    c[ImGuiCol_MenuBarBg]            = Colors::panelDark();
    c[ImGuiCol_Header]               = Colors::accent();
    c[ImGuiCol_HeaderHovered]        = Colors::accentHi();
    c[ImGuiCol_HeaderActive]         = Colors::accent();
    c[ImGuiCol_Button]               = {0.18f, 0.18f, 0.22f, 1.0f};
    c[ImGuiCol_ButtonHovered]        = {0.25f, 0.25f, 0.30f, 1.0f};
    c[ImGuiCol_ButtonActive]         = Colors::accent();
    c[ImGuiCol_Tab]                  = Colors::panelDark();
    c[ImGuiCol_TabHovered]           = Colors::accentHi();
    c[ImGuiCol_TabSelected]          = Colors::accent();
    c[ImGuiCol_Text]                 = Colors::text();
    c[ImGuiCol_TextDisabled]         = Colors::textDim();
    c[ImGuiCol_Separator]            = Colors::border();
    c[ImGuiCol_SeparatorHovered]     = Colors::accent();
    c[ImGuiCol_SliderGrab]           = Colors::accent();
    c[ImGuiCol_SliderGrabActive]     = Colors::accentHi();
    c[ImGuiCol_CheckMark]            = Colors::accent();
    c[ImGuiCol_ResizeGrip]           = {0, 0, 0, 0};
    c[ImGuiCol_ScrollbarBg]          = {0, 0, 0, 0};
    c[ImGuiCol_ScrollbarGrab]        = {0.3f, 0.3f, 0.35f, 0.5f};
    c[ImGuiCol_ScrollbarGrabHovered] = {0.4f, 0.4f, 0.45f, 0.7f};
    c[ImGuiCol_ScrollbarGrabActive]  = Colors::accent();
}

// ---------------------------------------------------------------------------
// Top bar: logo | filename | MODEL PAINT UV tabs | undo/redo
// ---------------------------------------------------------------------------
void ui_top_bar(UIState& state)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize({vp->Size.x, kTopBarHeight});

    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.08f, 0.08f, 0.10f, 1.0f});
    ImGui::Begin("##TopBar", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Logo text
    ImGui::SetCursorPos({12, 12});
    ImGui::PushStyleColor(ImGuiCol_Text, Colors::accent());
    ImGui::Text("REFLOW");
    ImGui::PopStyleColor();

    // Filename
    ImGui::SameLine(110);
    ImGui::SetCursorPosY(14);
    ImGui::PushStyleColor(ImGuiCol_Text, Colors::textDim());
    ImGui::Text("|");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::Text("%s", state.filename.c_str());
    ImGui::SameLine();
    if (state.fileModified) {
        ImGui::PushStyleColor(ImGuiCol_Text, Colors::green());
        ImGui::Text("Saved");
        ImGui::PopStyleColor();
    }

    // Mode tabs — centered
    float tabWidth = 80.0f;
    float totalTabWidth = tabWidth * 3 + 16;
    float tabStartX = (vp->Size.x - totalTabWidth) * 0.5f;
    ImGui::SetCursorPos({tabStartX, 8});

    const char* modeNames[] = {"MODEL", "PAINT", "UV"};
    EditorMode modes[] = {EditorMode::Model, EditorMode::Paint, EditorMode::UV};

    for (int i = 0; i < 3; i++) {
        bool active = (state.editorMode == modes[i]);

        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, {0, 0, 0, 0});
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::accent());
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, {0, 0, 0, 0});
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::textDim());
        }

        if (ImGui::Button(modeNames[i], {tabWidth, 30}))
            state.editorMode = modes[i];

        // Underline for active tab
        if (active) {
            ImVec2 mn = ImGui::GetItemRectMin();
            ImVec2 mx = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddLine(
                {mn.x + 10, mx.y}, {mx.x - 10, mx.y},
                ImGui::GetColorU32(Colors::accent()), 2.0f);
        }

        ImGui::PopStyleColor(2);
        if (i < 2) ImGui::SameLine();
    }

    // Right side icons: undo, redo, menu, more
    float iconSize = 28.0f;
    float iconGap = 8.0f;
    float rightX = vp->Pos.x + vp->Size.x - (iconSize * 4 + iconGap * 3 + 16);
    ImGui::SetCursorPos({rightX - vp->Pos.x, 10});

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 iconCol = ImGui::GetColorU32({0.55f, 0.55f, 0.58f, 1.0f});
    ImU32 iconHov = ImGui::GetColorU32({0.80f, 0.80f, 0.83f, 1.0f});

    // Undo arrow — rounded open curve sweeping left with arrowhead at tip
    ImGui::PushStyleColor(ImGuiCol_Button, {0, 0, 0, 0});
    if (ImGui::Button("##undo", {iconSize, iconSize})) { /* TODO */ }
    {
        ImVec2 mn = ImGui::GetItemRectMin();
        bool hov = ImGui::IsItemHovered();
        ImU32 c = hov ? iconHov : iconCol;
        float cx = mn.x + iconSize * 0.5f, cy = mn.y + iconSize * 0.5f;
        float r = 8.0f;
        // Arc: starts at bottom-right, sweeps over top to bottom-left
        // Goes from ~-30deg (bottom-right) over the top to ~210deg (bottom-left)
        float startAngle = -0.5f;   // just below right side
        float endAngle   = 3.14f + 0.5f; // past the left side going down
        dl->PathArcTo({cx, cy + 1}, r, startAngle, endAngle, 20);
        dl->PathStroke(c, 0, 2.2f);
        // Arrowhead at the end of the arc (bottom-left, pointing down-left)
        float ax = cx + r * cosf(endAngle);
        float ay = cy + 1 + r * sinf(endAngle);
        dl->AddTriangleFilled(
            {ax - 1, ay + 4},   // tip (pointing down)
            {ax - 5, ay - 2},   // left wing
            {ax + 3, ay - 1},   // right wing
            c);
    }

    ImGui::SameLine(0, iconGap);

    // Redo arrow — mirror of undo, sweeps right
    if (ImGui::Button("##redo", {iconSize, iconSize})) { /* TODO */ }
    {
        ImVec2 mn = ImGui::GetItemRectMin();
        bool hov = ImGui::IsItemHovered();
        ImU32 c = hov ? iconHov : iconCol;
        float cx = mn.x + iconSize * 0.5f, cy = mn.y + iconSize * 0.5f;
        float r = 8.0f;
        // Arc: from bottom-left, sweeps over top to bottom-right
        float startAngle = 3.14f + 0.5f; // bottom-left
        float endAngle   = -0.5f;        // bottom-right
        // Draw reversed: from right going over top to left
        dl->PathArcTo({cx, cy + 1}, r, 3.14f - (-0.5f), 3.14f - (3.14f + 0.5f), 20);
        dl->PathStroke(c, 0, 2.2f);
        // Arrowhead at bottom-right
        float endA = 3.14f - (3.14f + 0.5f); // = -0.5
        float ax = cx + r * cosf(endA);
        float ay = cy + 1 + r * sinf(endA);
        dl->AddTriangleFilled(
            {ax + 1, ay + 4},   // tip
            {ax + 5, ay - 2},   // right wing
            {ax - 3, ay - 1},   // left wing
            c);
    }

    ImGui::SameLine(0, iconGap);

    // Hamburger menu (3 horizontal lines)
    if (ImGui::Button("##menu", {iconSize, iconSize})) { /* TODO */ }
    {
        ImVec2 mn = ImGui::GetItemRectMin();
        bool hov = ImGui::IsItemHovered();
        ImU32 c = hov ? iconHov : iconCol;
        float lx = mn.x + 6, rx = mn.x + iconSize - 6;
        for (int i = 0; i < 3; i++) {
            float ly = mn.y + 8 + i * 6;
            dl->AddLine({lx, ly}, {rx, ly}, c, 2.0f);
        }
    }

    ImGui::SameLine(0, iconGap);

    // Three dots (vertical ellipsis)
    if (ImGui::Button("##more", {iconSize, iconSize})) { /* TODO */ }
    {
        ImVec2 mn = ImGui::GetItemRectMin();
        bool hov = ImGui::IsItemHovered();
        ImU32 c = hov ? iconHov : iconCol;
        float cx = mn.x + iconSize * 0.5f;
        for (int i = 0; i < 3; i++) {
            float cy = mn.y + 7 + i * 7;
            dl->AddCircleFilled({cx, cy}, 2.0f, c);
        }
    }

    ImGui::PopStyleColor();

    ImGui::End();
    ImGui::PopStyleColor(); // WindowBg
}

// ---------------------------------------------------------------------------
// Left toolbar: tool buttons
// ---------------------------------------------------------------------------
void ui_toolbar(UIState& state)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float y = vp->Pos.y + kTopBarHeight;
    float h = vp->Size.y - kTopBarHeight - kStatusBarHeight;

    ImGui::SetNextWindowPos({vp->Pos.x, y});
    ImGui::SetNextWindowSize({kToolbarWidth, h});

    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.10f, 0.10f, 0.12f, 0.95f});
    ImGui::Begin("##Toolbar", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    struct ToolDef { Tool tool; const char* icon; const char* name; };
    ToolDef tools[] = {
        {Tool::Select, "->", "Select"},
        {Tool::Move,   "+",  "Move"},
        {Tool::Rotate, "O",  "Rotate"},
        {Tool::Scale,  "[]", "Scale"},
        {Tool::Box,    "<>", "Box"},
    };

    ImGui::SetCursorPosY(12);
    for (auto& t : tools) {
        bool active = (state.currentTool == t.tool);
        float btnW = kToolbarWidth - 24;

        if (active)
            ImGui::PushStyleColor(ImGuiCol_Button, Colors::accent());
        else
            ImGui::PushStyleColor(ImGuiCol_Button, {0, 0, 0, 0});

        ImGui::SetCursorPosX(12);
        if (ImGui::Button(t.name, {btnW, 36}))
            state.currentTool = t.tool;

        ImGui::PopStyleColor();
        ImGui::Spacing();
    }

    // Selection mode buttons at bottom
    ImGui::SetCursorPosY(h - 56);
    ImGui::Separator();
    ImGui::Spacing();

    const char* selNames[] = {"Obj", "Vtx", "Edge", "Face"};
    SelectMode selModes[] = {SelectMode::Object, SelectMode::Vertex, SelectMode::Edge, SelectMode::Face};

    ImGui::SetCursorPosX(12);
    for (int i = 0; i < 4; i++) {
        bool active = (state.selectMode == selModes[i]);
        if (active)
            ImGui::PushStyleColor(ImGuiCol_Button, Colors::accent());
        else
            ImGui::PushStyleColor(ImGuiCol_Button, Colors::input());

        if (ImGui::Button(selNames[i], {30, 30}))
            state.selectMode = selModes[i];

        ImGui::PopStyleColor();
        if (i < 3) ImGui::SameLine();
    }

    ImGui::End();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// Right panel: Objects list + Properties
// ---------------------------------------------------------------------------
void ui_objects_panel(UIState& state)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float y = vp->Pos.y + kTopBarHeight;
    float h = (vp->Size.y - kTopBarHeight - kStatusBarHeight) * 0.4f;

    ImGui::SetNextWindowPos({vp->Pos.x + vp->Size.x - kRightPanelWidth, y});
    ImGui::SetNextWindowSize({kRightPanelWidth, h});

    ImGui::Begin("##Objects", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Header
    ImGui::Text("OBJECTS");
    ImGui::SameLine(kRightPanelWidth - 44);
    ImGui::PushStyleColor(ImGuiCol_Button, {0, 0, 0, 0});
    if (ImGui::Button("+", {24, 24})) { /* TODO: add primitive */ }
    ImGui::PopStyleColor();

    ImGui::Separator();
    ImGui::Spacing();

    // Object list (placeholder — will be driven by actual scene)
    ImGui::PushStyleColor(ImGuiCol_Button, {0.18f, 0.18f, 0.22f, 1.0f});
    ImGui::Button("Cube", {kRightPanelWidth - 60, 32});
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, Colors::textDim());
    ImGui::Text("eye");
    ImGui::PopStyleColor();
    ImGui::PopStyleColor();

    ImGui::End();
}

void ui_properties_panel(UIState& state)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float objH = (vp->Size.y - kTopBarHeight - kStatusBarHeight) * 0.4f;
    float y = vp->Pos.y + kTopBarHeight + objH;
    float h = vp->Size.y - kTopBarHeight - kStatusBarHeight - objH;

    ImGui::SetNextWindowPos({vp->Pos.x + vp->Size.x - kRightPanelWidth, y});
    ImGui::SetNextWindowSize({kRightPanelWidth, h});

    ImGui::Begin("##Properties", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    ImGui::Text("PROPERTIES");
    ImGui::Separator();
    ImGui::Spacing();

    // Position
    static float pos[3] = {0, 0, 0};
    ImGui::Text("Position");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputFloat3("##pos", pos, "%.1f");

    ImGui::Spacing();

    // Rotation
    static float rot[3] = {0, 0, 0};
    ImGui::Text("Rotation");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputFloat3("##rot", rot, "%.0f\xC2\xB0"); // degree sign

    ImGui::Spacing();

    // Scale
    static float scl[3] = {1, 1, 1};
    ImGui::Text("Scale");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputFloat3("##scl", scl, "%.1f");

    ImGui::End();
}

// ---------------------------------------------------------------------------
// Status bar: navigation hints
// ---------------------------------------------------------------------------
void ui_status_bar(UIState& state)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float y = vp->Pos.y + vp->Size.y - kStatusBarHeight;

    ImGui::SetNextWindowPos({vp->Pos.x, y});
    ImGui::SetNextWindowSize({vp->Size.x, kStatusBarHeight});

    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.08f, 0.08f, 0.10f, 1.0f});
    ImGui::Begin("##StatusBar", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    float centerX = vp->Size.x * 0.5f - 200;
    ImGui::SetCursorPos({centerX, 8});
    ImGui::PushStyleColor(ImGuiCol_Text, Colors::textDim());
    ImGui::Text("MMB: Orbit    Shift+MMB: Pan    Scroll: Zoom");
    ImGui::PopStyleColor();

    ImGui::End();
    ImGui::PopStyleColor();
}

// ---------------------------------------------------------------------------
// Viewport overlay: Persp dropdown + orientation gizmo
// ---------------------------------------------------------------------------
void ui_viewport_overlay(UIState& state)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float vpX = vp->Pos.x + kToolbarWidth;
    float vpY = vp->Pos.y + kTopBarHeight;

    // Perspective dropdown
    ImGui::SetNextWindowPos({vpX + 8, vpY + 8});
    ImGui::SetNextWindowSize({0, 0});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.15f, 0.15f, 0.18f, 0.85f});
    ImGui::Begin("##PerspDrop", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing);

    static int projMode = 0; // 0=Persp, 1=Ortho
    const char* projNames[] = {"Persp", "Ortho"};
    ImGui::PushStyleColor(ImGuiCol_Button, Colors::input());
    if (ImGui::Button(projNames[projMode], {70, 26}))
        projMode = 1 - projMode;
    ImGui::PopStyleColor();

    ImGui::End();
    ImGui::PopStyleColor();

    // Orientation gizmo (simple XYZ text for now)
    float gizX = vp->Pos.x + vp->Size.x - kRightPanelWidth - 80;
    float gizY = vpY + 16;
    ImGui::SetNextWindowPos({gizX, gizY});
    ImGui::SetNextWindowSize({0, 0});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0, 0, 0, 0});
    ImGui::Begin("##Gizmo", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoInputs);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 center = {gizX + 30, gizY + 30};
    float len = 22.0f;

    // X axis (red)
    dl->AddLine(center, {center.x + len, center.y}, IM_COL32(230, 80, 80, 255), 2.0f);
    dl->AddText({center.x + len + 4, center.y - 7}, IM_COL32(230, 80, 80, 255), "X");

    // Y axis (green)
    dl->AddLine(center, {center.x, center.y - len}, IM_COL32(80, 200, 80, 255), 2.0f);
    dl->AddText({center.x - 4, center.y - len - 18}, IM_COL32(80, 200, 80, 255), "Y");

    // Z axis (blue)
    dl->AddLine(center, {center.x - len * 0.6f, center.y + len * 0.6f}, IM_COL32(80, 120, 230, 255), 2.0f);
    dl->AddText({center.x - len * 0.6f - 14, center.y + len * 0.6f - 2}, IM_COL32(80, 120, 230, 255), "Z");

    ImGui::End();
    ImGui::PopStyleColor();
}

} // namespace rf
