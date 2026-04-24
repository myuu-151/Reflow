#include "ui/ui.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <glad/glad.h>
#include <stb_image.h>
#include <cstdio>
#include <string>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace rf {

// Layout constants
static constexpr float kTopBarHeight    = 48.0f;
static constexpr float kToolbarWidth    = 160.0f;
static constexpr float kRightPanelWidth = 260.0f;
static constexpr float kStatusBarHeight = 36.0f;

// Material Icons codepoints
static constexpr const char* ICON_UNDO      = "\xE2\x86\xA9"; // U+21A9 ↩
static constexpr const char* ICON_REDO      = "\xE2\x86\xAA"; // U+21AA ↪
static constexpr const char* ICON_MENU      = "\xEE\x97\x92"; // U+E5D2
static constexpr const char* ICON_MORE_VERT = "\xEE\x97\x94"; // U+E5D4

// Toolbar icons
static constexpr const char* ICON_SELECT    = "\xEE\x97\x88"; // U+E5C8 near_me (cursor)
static constexpr const char* ICON_MOVE      = "\xEE\xA2\x9F"; // U+E89F open_with (4-way arrows)
static constexpr const char* ICON_ROTATE    = "\xEE\xA1\xA3"; // U+E863 autorenew (rotate)
static constexpr const char* ICON_SCALE     = "\xEE\x8F\x82"; // U+E3C2 crop_free (square with corners)
static constexpr const char* ICON_BOX       = "\xEE\xA0\x8E"; // U+E80E 3d_rotation (cube)

static ImFont* g_iconFont = nullptr;
static ImFont* g_arrowFont = nullptr;
static GLuint g_logoTexture = 0;
static int g_logoW = 0, g_logoH = 0;

// Resolve path relative to exe directory
static std::string exe_relative(const char* relPath)
{
#ifdef _WIN32
    char buf[MAX_PATH];
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string dir(buf);
    auto pos = dir.find_last_of("\\/");
    if (pos != std::string::npos) dir = dir.substr(0, pos + 1);
    return dir + relPath;
#else
    return relPath;
#endif
}

void ui_init(GLFWwindow* win)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr; // don't save layout

    io.FontGlobalScale = 1.4f;

    // Default font
    static const ImWchar mainRanges[] = {
        0x0020, 0x00FF, // Basic Latin + Latin-1
        0
    };
    io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf", 14.0f, nullptr, mainRanges);

    // Arrow font (separate, use PushFont/PopFont for undo/redo)
    static const ImWchar arrowRanges[] = { 0x2190, 0x21FF, 0 };
    ImFontConfig arrowCfg;
    arrowCfg.PixelSnapH = true;
    arrowCfg.GlyphMinAdvanceX = 20.0f;
    g_arrowFont = io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/seguisym.ttf", 20.0f, &arrowCfg, arrowRanges);

    // Icon font (separate, use PushFont/PopFont)
    static const ImWchar iconRanges[] = { 0xE000, 0xF000, 0 };
    ImFontConfig iconCfg;
    iconCfg.PixelSnapH = true;
    iconCfg.GlyphMinAdvanceX = 20.0f;
    std::string iconPath = exe_relative("res/MaterialIcons-Regular.ttf");
    g_iconFont = io.Fonts->AddFontFromFileTTF(iconPath.c_str(), 20.0f, &iconCfg, iconRanges);

    ImGui_ImplGlfw_InitForOpenGL(win, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    ui_apply_theme();

    // Load logo icon as OpenGL texture
    int w, h, channels;
    std::string logoPath = exe_relative("res/reflow_icon.png");
    unsigned char* pixels = stbi_load(logoPath.c_str(), &w, &h, &channels, 4);
    if (pixels) {
        glGenTextures(1, &g_logoTexture);
        glBindTexture(GL_TEXTURE_2D, g_logoTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        g_logoW = w;
        g_logoH = h;
        stbi_image_free(pixels);
    }
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

    // Logo icon + text
    ImGui::SetCursorPos({10, 8});
    if (g_logoTexture) {
        float iconSz = 30.0f;
        ImGui::Image((ImTextureID)(intptr_t)g_logoTexture, {iconSz, iconSz});
        ImGui::SameLine(0, 6);
        ImGui::SetCursorPosY(12);
    }
    ImGui::PushStyleColor(ImGuiCol_Text, Colors::accent());
    ImGui::Text("REFLOW");
    ImGui::PopStyleColor();

    // Filename
    ImGui::SameLine(130);
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

    // Right side icons using Material Icons font
    float iconBtnSize = 32.0f;
    float iconGap = 4.0f;
    float rightX = vp->Size.x - (iconBtnSize * 4 + iconGap * 3 + 16);
    ImGui::SetCursorPos({rightX, 8});

    ImGui::PushStyleColor(ImGuiCol_Button, {0, 0, 0, 0});
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.2f, 0.2f, 0.24f, 1.0f});
    ImGui::PushStyleColor(ImGuiCol_Text, {0.55f, 0.55f, 0.58f, 1.0f});

    // Undo/Redo use arrow font (Segoe UI Symbol)
    if (g_arrowFont) ImGui::PushFont(g_arrowFont);
    if (ImGui::Button(ICON_UNDO, {iconBtnSize, iconBtnSize})) { /* TODO */ }
    ImGui::SameLine(0, iconGap);
    if (ImGui::Button(ICON_REDO, {iconBtnSize, iconBtnSize})) { /* TODO */ }
    if (g_arrowFont) ImGui::PopFont();
    ImGui::SameLine(0, iconGap);
    // Menu icons use Material Icons font
    if (g_iconFont) ImGui::PushFont(g_iconFont);
    if (ImGui::Button(ICON_MENU, {iconBtnSize, iconBtnSize})) { /* TODO */ }
    ImGui::SameLine(0, iconGap);
    if (ImGui::Button(ICON_MORE_VERT, {iconBtnSize, iconBtnSize})) { /* TODO */ }
    if (g_iconFont) ImGui::PopFont();

    ImGui::PopStyleColor(3);

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
        {Tool::Select, ICON_SELECT, "Select"},
        {Tool::Move,   ICON_MOVE,   "Move"},
        {Tool::Rotate, ICON_ROTATE, "Rotate"},
        {Tool::Scale,  ICON_SCALE,  "Scale"},
    };

    ImGui::SetCursorPosY(12);
    for (auto& t : tools) {
        bool active = (state.currentTool == t.tool);
        float btnW = kToolbarWidth - 24;
        float btnH = 36.0f;

        if (active)
            ImGui::PushStyleColor(ImGuiCol_Button, Colors::accent());
        else
            ImGui::PushStyleColor(ImGuiCol_Button, {0, 0, 0, 0});

        ImGui::SetCursorPosX(12);
        ImVec2 btnPos = ImGui::GetCursorScreenPos();

        char btnId[32];
        snprintf(btnId, sizeof(btnId), "##tool_%s", t.name);
        if (ImGui::Button(btnId, {btnW, btnH}))
            state.currentTool = t.tool;

        // Draw icon + label over the button
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float iconX = btnPos.x + 10;
        float textY = btnPos.y + (btnH - 20) * 0.5f;

        ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);

        // Icon
        if (g_iconFont) {
            dl->AddText(g_iconFont, 20.0f, ImVec2(iconX, textY), textCol, t.icon);
        }

        // Label
        float labelX = btnPos.x + 38;
        dl->AddText(ImVec2(labelX, textY + 2), textCol, t.name);

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
