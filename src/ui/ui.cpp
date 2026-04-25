#include "ui/ui.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <glad/glad.h>
#include "mesh/mesh.h"
#include <stb_image.h>
#include <cstdio>
#include <string>
#include <algorithm>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace rf {

// Base layout constants (at scale 1.0)
static constexpr float kBaseTopBarHeight    = 34.0f;
static constexpr float kBaseToolbarWidth    = 114.0f;
static constexpr float kBaseRightPanelWidth = 186.0f;
static constexpr float kBaseStatusBarHeight = 26.0f;

// Scale factor and helpers
static float S() { return ImGui::GetIO().FontGlobalScale; }
static float s(float v) { return v * S(); } // scale any pixel value
static float topBarHeight()    { return kBaseTopBarHeight    * S(); }
static float toolbarWidth()    { return kBaseToolbarWidth    * S(); }
static float rightPanelWidth() { return kBaseRightPanelWidth * S(); }
static float statusBarHeight() { return kBaseStatusBarHeight * S(); }

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

// Selection mode icons
static constexpr const char* ICON_SEL_OBJ   = "\xEE\xA0\xB5"; // U+E835 checkbox_outline (cube wireframe)
static constexpr const char* ICON_SEL_VTX   = "\xEE\x97\x83"; // U+E5C3 apps (grid dots)
static constexpr const char* ICON_SEL_EDGE  = "\xEE\xA3\xB2"; // U+E8F2 view_column (parallel lines)
static constexpr const char* ICON_SEL_FACE  = "\xEE\x81\x87"; // U+E047 stop (solid square)
static constexpr const char* ICON_BOX       = "\xEE\xA0\x8E"; // U+E80E 3d_rotation (cube)

static ImFont* g_iconFont = nullptr;
static ImFont* g_arrowFont = nullptr;
static float g_lastAppliedScale = 1.0f; // tracks style scale factor (1.0 = theme defaults)
static GLuint g_logoTexture = 0;
static int g_logoW = 0, g_logoH = 0;

// Selection mode icon textures
static GLuint g_selTex[4] = {}; // Object, Vertex, Edge, Face

// Viewport shading icon textures
static GLuint g_viewModeTex[3] = {}; // Wireframe, Solid, Textured

// Load a PNG as an OpenGL texture, returns texture ID (0 on failure)
static GLuint load_texture(const char* path)
{
    int w, h, ch;
    unsigned char* px = stbi_load(path, &w, &h, &ch, 4);
    if (!px) return 0;
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    stbi_image_free(px);
    return tex;
}

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

    io.FontGlobalScale = 2.5f;

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
    std::string logoPath = exe_relative("res/reflow_icon_48.png");
    unsigned char* pixels = stbi_load(logoPath.c_str(), &w, &h, &channels, 4);
    if (pixels) {
        glGenTextures(1, &g_logoTexture);
        glBindTexture(GL_TEXTURE_2D, g_logoTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glGenerateMipmap(GL_TEXTURE_2D);
        g_logoW = w;
        g_logoH = h;
        stbi_image_free(pixels);
    }

    // Load selection mode icons
    const char* selIconFiles[] = {
        "res/icon_object.png",
        "res/icon_vertex.png",
        "res/icon_edge.png",
        "res/icon_face.png",
    };
    for (int i = 0; i < 4; i++) {
        std::string path = exe_relative(selIconFiles[i]);
        g_selTex[i] = load_texture(path.c_str());
    }

    // Load viewport shading icons
    const char* viewModeFiles[] = {
        "assets/b/sph_wf3.png",
        "assets/b/sph_sld.png",
        "assets/b/sph_txr.png",
    };
    for (int i = 0; i < 3; i++) {
        std::string path = exe_relative(viewModeFiles[i]);
        g_viewModeTex[i] = load_texture(path.c_str());
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
    ImGui::SetNextWindowSize({vp->Size.x, topBarHeight()});

    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.08f, 0.08f, 0.10f, 1.0f});
    ImGui::Begin("##TopBar", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Logo icon + text
    ImGui::SetCursorPos({s(7), s(6)});
    if (g_logoTexture) {
        float iconSz = s(21);
        ImGui::Image((ImTextureID)(intptr_t)g_logoTexture, {iconSz, iconSz});
        ImGui::SameLine(0, s(4));
        ImGui::SetCursorPosY(s(8));
    }
    ImGui::PushStyleColor(ImGuiCol_Text, Colors::accent());
    ImGui::Text("REFLOW");
    ImGui::PopStyleColor();

    // Filename
    ImGui::SameLine(s(93));
    float barMid = s(8);
    ImGui::SetCursorPosY(barMid);
    ImGui::PushStyleColor(ImGuiCol_Text, Colors::textDim());
    ImGui::Text("|");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::SetCursorPosY(barMid);
    {
        std::string display = state.filename;
        auto dot = display.rfind('.');
        if (dot != std::string::npos) display = display.substr(0, dot);
        ImGui::Text("%s", display.c_str());
    }
    ImGui::SameLine();
    if (state.fileModified) {
        ImGui::PushStyleColor(ImGuiCol_Text, Colors::green());
        ImGui::Text("Saved");
        ImGui::PopStyleColor();
    }

    // Mode tabs — centered
    const char* modeNames[] = {"MODEL", "SCULPT", "PAINT", "RIG", "ANIMATE", "UV"};
    EditorMode modes[] = {EditorMode::Model, EditorMode::Sculpt, EditorMode::Paint, EditorMode::Rig, EditorMode::Animate, EditorMode::UV};
    const int tabCount = 6;
    float padding = ImGui::GetStyle().FramePadding.x * 2;
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float totalTabWidth = 0;
    float tabWidths[6];
    for (int i = 0; i < tabCount; i++) {
        tabWidths[i] = ImGui::CalcTextSize(modeNames[i]).x + padding;
        totalTabWidth += tabWidths[i];
    }
    totalTabWidth += spacing * (tabCount - 1);
    float tabStartX = (vp->Size.x - totalTabWidth) * 0.5f;
    ImGui::SetCursorPos({tabStartX, s(6)});

    for (int i = 0; i < tabCount; i++) {
        bool active = (state.editorMode == modes[i]);

        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, {0, 0, 0, 0});
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::accent());
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, {0, 0, 0, 0});
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::textDim());
        }

        if (ImGui::Button(modeNames[i], {tabWidths[i], s(21)}))
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
        if (i < tabCount - 1) ImGui::SameLine();
    }

    // Right side icons using Material Icons font
    float iconBtnSize = s(23);
    float iconGap = s(3);
    float rightX = vp->Size.x - (iconBtnSize * 4 + iconGap * 3 + s(12));
    ImGui::SetCursorPos({rightX, s(6)});

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
    if (ImGui::Button(ICON_MENU, {iconBtnSize, iconBtnSize}))
        ImGui::OpenPopup("##FileMenu");
    ImGui::SameLine(0, iconGap);
    if (ImGui::Button(ICON_MORE_VERT, {iconBtnSize, iconBtnSize}))
        ImGui::OpenPopup("##MoreMenu");
    if (g_iconFont) ImGui::PopFont();

    ImGui::PopStyleColor(3);

    // File menu popup
    ImGui::PushStyleColor(ImGuiCol_PopupBg, Colors::panelDark());
    if (ImGui::BeginPopup("##FileMenu")) {
        if (ImGui::MenuItem("New"))          { state.pendingAction = UIAction::New; }
        if (ImGui::MenuItem("Open..."))      { state.pendingAction = UIAction::Open; }
        ImGui::Separator();
        if (ImGui::MenuItem("Save"))         { state.pendingAction = UIAction::Save; }
        if (ImGui::MenuItem("Save As..."))   { state.pendingAction = UIAction::SaveAs; }
        ImGui::Separator();
        if (ImGui::MenuItem("Import"))   { state.pendingAction = UIAction::Import; }
        if (ImGui::MenuItem("Export"))   { state.pendingAction = UIAction::Export; }
        ImGui::Separator();
        if (ImGui::MenuItem("Quit"))         { glfwSetWindowShouldClose(glfwGetCurrentContext(), GLFW_TRUE); }
        ImGui::EndPopup();
    }
    ImGui::PopStyleColor();

    // More menu popup
    ImGui::PushStyleColor(ImGuiCol_PopupBg, Colors::panelDark());
    if (ImGui::BeginPopup("##MoreMenu")) {
        ImGui::Text("UI Scale");
        ImGui::SetNextItemWidth(s(107));
        if (ImGui::SliderFloat("##uiscale", &state.uiScale, 0.8f, 2.5f, "%.1fx")) {
            ImGui::GetIO().FontGlobalScale = state.uiScale;
        }
        ImGui::Spacing();
        if (ImGui::Button("Save Settings", {s(107), s(22)})) {
            ui_save_settings(state);
        }
        ImGui::EndPopup();
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
    float y = vp->Pos.y + topBarHeight();
    float h = vp->Size.y - topBarHeight() - statusBarHeight();

    ImGui::SetNextWindowPos({vp->Pos.x, y});
    ImGui::SetNextWindowSize({toolbarWidth(), h});

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

    ImGui::SetCursorPosY(s(9));
    for (auto& t : tools) {
        bool active = (state.currentTool == t.tool);
        float btnW = toolbarWidth() - s(17);
        float btnH = s(26);

        if (active)
            ImGui::PushStyleColor(ImGuiCol_Button, Colors::accent());
        else
            ImGui::PushStyleColor(ImGuiCol_Button, {0, 0, 0, 0});

        ImGui::SetCursorPosX(s(9));
        ImVec2 btnPos = ImGui::GetCursorScreenPos();

        char btnId[32];
        snprintf(btnId, sizeof(btnId), "##tool_%s", t.name);
        if (ImGui::Button(btnId, {btnW, btnH}))
            state.currentTool = t.tool;

        // Draw icon + label over the button
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float iconSz = s(14);
        float iconX = btnPos.x + s(7);
        float textY = btnPos.y + (btnH - iconSz) * 0.5f;

        ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);

        // Icon
        if (g_iconFont) {
            dl->AddText(g_iconFont, iconSz, ImVec2(iconX, textY), textCol, t.icon);
        }

        // Label
        float labelX = btnPos.x + s(27);
        dl->AddText(ImVec2(labelX, textY + s(1)), textCol, t.name);

        ImGui::PopStyleColor();
        ImGui::Spacing();
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
    float y = vp->Pos.y + topBarHeight();
    float h = (vp->Size.y - topBarHeight() - statusBarHeight()) * 0.4f;

    ImGui::SetNextWindowPos({vp->Pos.x + vp->Size.x - rightPanelWidth(), y});
    ImGui::SetNextWindowSize({rightPanelWidth(), h});

    ImGui::Begin("##Objects", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);

    // Header
    ImGui::Text("OBJECTS");
    ImGui::SameLine(rightPanelWidth() - s(35));
    ImGui::PushStyleColor(ImGuiCol_Button, {0, 0, 0, 0});
    if (g_iconFont) ImGui::PushFont(g_iconFont);
    if (ImGui::Button("\xEE\x85\x85##addobj", {s(24), s(24)}))
        ImGui::OpenPopup("##AddPrimitive");
    if (g_iconFont) ImGui::PopFont();
    ImGui::PopStyleColor();

    // Add primitive dropdown
    static int s_sphereRings = 8;
    static int s_sphereSegments = 16;
    static bool s_showSphereParams = false;
    static int s_sphereIdx = -1;
    if (ImGui::BeginPopup("##AddPrimitive")) {
        if (state.meshes && state.selectedMesh && state.objectSelected) {
            if (ImGui::MenuItem("Cube")) {
                auto cube = Mesh::create_cube();
                state.meshes->push_back(std::move(cube));
                *state.selectedMesh = (int)state.meshes->size() - 1;
                *state.objectSelected = true;
            }
            if (ImGui::MenuItem("Sphere")) {
                s_sphereRings = 8;
                s_sphereSegments = 16;
                auto sphere = Mesh::create_sphere(s_sphereRings, s_sphereSegments);
                state.meshes->push_back(std::move(sphere));
                s_sphereIdx = (int)state.meshes->size() - 1;
                *state.selectedMesh = s_sphereIdx;
                *state.objectSelected = true;
                s_showSphereParams = true;
            }
        }
        ImGui::EndPopup();
    }
    if (s_showSphereParams)
        ImGui::OpenPopup("##SphereParams");
    if (ImGui::BeginPopup("##SphereParams")) {
        s_showSphereParams = false;
        ImGui::Text("Sphere");
        ImGui::Separator();
        ImGui::SetNextItemWidth(s(50));
        bool changed = false;
        changed |= ImGui::InputInt("Rings", &s_sphereRings, 0);
        if (s_sphereRings < 2) s_sphereRings = 2;
        if (s_sphereRings > 64) s_sphereRings = 64;
        ImGui::SetNextItemWidth(s(50));
        changed |= ImGui::InputInt("Segments", &s_sphereSegments, 0);
        if (s_sphereSegments < 3) s_sphereSegments = 3;
        if (s_sphereSegments > 128) s_sphereSegments = 128;
        // Regenerate sphere live
        if (changed && state.meshes && s_sphereIdx >= 0 && s_sphereIdx < (int)state.meshes->size()) {
            auto sphere = Mesh::create_sphere(s_sphereRings, s_sphereSegments);
            sphere.position = (*state.meshes)[s_sphereIdx].position;
            (*state.meshes)[s_sphereIdx] = std::move(sphere);
        }
        ImGui::EndPopup();
    } else {
        s_sphereIdx = -1; // popup closed, stop tracking
    }

    ImGui::Separator();
    ImGui::Spacing();

    // Object list (driven by mesh vector)
    if (state.meshes && state.selectedMesh) {
        auto& meshes = *state.meshes;
        int& sel = *state.selectedMesh;
        for (int i = 0; i < (int)meshes.size(); i++) {
            bool isSelected = (state.objectSelected && *state.objectSelected && i == sel);
            ImVec4 btnCol = isSelected ? ImVec4(0.25f, 0.25f, 0.32f, 1.0f) : ImVec4(0.18f, 0.18f, 0.22f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, btnCol);
            char label[64];
            snprintf(label, sizeof(label), "%s##obj%d", meshes[i].name.c_str(), i);
            if (ImGui::Button(label, {rightPanelWidth() - s(43), s(23)})) {
                sel = i;
                if (state.objectSelected) *state.objectSelected = true;
                state.pendingFrameSelected = true;
            }
            ImGui::PopStyleColor();

            ImGui::SameLine();
            ImGui::PushStyleColor(ImGuiCol_Text, Colors::textDim());
            ImGui::PushStyleColor(ImGuiCol_Button, {0, 0, 0, 0});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.25f, 0.25f, 0.28f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, {0, 0, 0, 0});
            if (g_iconFont) ImGui::PushFont(g_iconFont);
            char eyeId[16]; snprintf(eyeId, sizeof(eyeId), "##eye%d", i);
            const char* eyeIcon = "\xEE\xA3\xB4";
            ImGui::Button(eyeIcon, {s(24), s(24)});
            if (g_iconFont) ImGui::PopFont();
            ImGui::PopStyleColor(4);
        }
    }

    ImGui::End();
}

void ui_properties_panel(UIState& state)
{
    ImGuiViewport* vp = ImGui::GetMainViewport();
    float objH = (vp->Size.y - topBarHeight() - statusBarHeight()) * 0.4f;
    float y = vp->Pos.y + topBarHeight() + objH;
    float h = vp->Size.y - topBarHeight() - statusBarHeight() - objH;

    ImGui::SetNextWindowPos({vp->Pos.x + vp->Size.x - rightPanelWidth(), y});
    ImGui::SetNextWindowSize({rightPanelWidth(), h});

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
    float y = vp->Pos.y + vp->Size.y - statusBarHeight();

    ImGui::SetNextWindowPos({vp->Pos.x, y});
    ImGui::SetNextWindowSize({vp->Size.x, statusBarHeight()});

    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.08f, 0.08f, 0.10f, 1.0f});
    ImGui::Begin("##StatusBar", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoBringToFrontOnFocus);

    float centerX = vp->Size.x * 0.5f - s(143);
    ImGui::SetCursorPos({centerX, s(6)});
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
    float vpX = vp->Pos.x + toolbarWidth();
    float vpY = vp->Pos.y + topBarHeight();

    // Perspective dropdown
    ImGui::SetNextWindowPos({vpX + s(6), vpY + s(6)});
    ImGui::SetNextWindowSize({0, 0});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.15f, 0.15f, 0.18f, 0.85f});
    ImGui::Begin("##PerspDrop", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing);

    static int projMode = 0; // 0=Persp, 1=Ortho
    const char* projNames[] = {"Persp", "Ortho"};
    ImGui::PushStyleColor(ImGuiCol_Button, Colors::input());
    if (ImGui::Button(projNames[projMode], {s(50), s(19)}))
        projMode = 1 - projMode;
    ImGui::PopStyleColor();

    ImGui::End();
    ImGui::PopStyleColor();

    // Viewport shading mode buttons — top right of viewport
    {
        float vpW = vp->Size.x - toolbarWidth() - rightPanelWidth();
        float btnSz = s(18.5f);
        float gap = s(3);
        float pad = ImGui::GetStyle().FramePadding.x;
        float totalW = (btnSz + pad * 2) * 3 + gap * 2;
        float shadX = vpX + vpW - totalW - s(16);
        float shadY = vpY + s(6);

        ImGui::SetNextWindowPos({shadX - s(4), shadY - s(4)});
        ImGui::SetNextWindowSize({0, 0});
        ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.12f, 0.12f, 0.14f, 0.85f});
        ImGui::Begin("##ViewMode", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing);

        ViewMode modes[] = {ViewMode::Wireframe, ViewMode::Solid, ViewMode::Textured};
        for (int i = 0; i < 3; i++) {
            bool active = (state.viewMode == modes[i]);
            ImVec4 tint = active ? ImVec4(1, 1, 1, 1) : ImVec4(0.5f, 0.5f, 0.5f, 0.7f);

            ImGui::PushStyleColor(ImGuiCol_Button, active ? ImVec4(0.25f, 0.25f, 0.3f, 1.0f) : ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.3f, 0.3f, 0.35f, 1.0f});
            if (g_viewModeTex[i]) {
                char id[16]; snprintf(id, sizeof(id), "vm%d", i);
                if (ImGui::ImageButton(id, (ImTextureID)(intptr_t)g_viewModeTex[i], {btnSz, btnSz}, {0,0}, {1,1}, {0,0,0,0}, tint))
                    state.viewMode = modes[i];
                // RMB on textured button opens settings popup
                if (i == 2 && ImGui::IsItemClicked(ImGuiMouseButton_Right))
                    ImGui::OpenPopup("##TexSettings");
            }
            ImGui::PopStyleColor(2);
            if (i < 2) ImGui::SameLine(0, gap);
        }

        // Textured mode settings popup
        ImGui::PushStyleColor(ImGuiCol_PopupBg, {0.15f, 0.15f, 0.18f, 0.95f});
        if (ImGui::BeginPopup("##TexSettings")) {
            ImGui::Checkbox("Follow Camera", &state.lightFollowCam);
            ImGui::Text("Light Direction");

            // Circle drag widget
            float radius = s(40);
            float dotR = s(4);
            ImVec2 cPos = ImGui::GetCursorScreenPos();
            ImVec2 center = {cPos.x + radius, cPos.y + radius};
            ImDrawList* dl = ImGui::GetWindowDrawList();

            // Invisible button for interaction
            ImGui::InvisibleButton("##LightCircle", {radius * 2, radius * 2});
            bool dragging = ImGui::IsItemActive();
            if (dragging) {
                ImVec2 mouse = ImGui::GetIO().MousePos;
                float dx = mouse.x - center.x;
                float dy = mouse.y - center.y;
                // Map to angles: X = horizontal angle (0-360), Y = distance from center mapped to vertical
                state.lightAngleX = fmodf(atan2f(dx, -dy) * 57.2957795f + 360.0f, 360.0f);
                float dist = sqrtf(dx * dx + dy * dy) / radius;
                if (dist > 1.0f) dist = 1.0f;
                state.lightAngleY = 90.0f - dist * 90.0f; // center = 90 (top), edge = 0 (horizon)
            }

            // Draw circle background
            dl->AddCircleFilled(center, radius, IM_COL32(30, 30, 35, 255), 32);
            dl->AddCircle(center, radius, IM_COL32(80, 80, 90, 255), 32, 1.5f);
            // Crosshair
            dl->AddLine({center.x - radius, center.y}, {center.x + radius, center.y}, IM_COL32(50, 50, 60, 255));
            dl->AddLine({center.x, center.y - radius}, {center.x, center.y + radius}, IM_COL32(50, 50, 60, 255));

            // Dot position from angles
            float rx = glm::radians(state.lightAngleX);
            float dist = (90.0f - state.lightAngleY) / 90.0f;
            if (dist < 0.0f) dist = 0.0f;
            if (dist > 1.0f) dist = 1.0f;
            float dotX = center.x + sinf(rx) * dist * radius;
            float dotY = center.y - cosf(rx) * dist * radius;
            dl->AddCircleFilled({dotX, dotY}, dotR, IM_COL32(255, 200, 60, 255));
            dl->AddCircle({dotX, dotY}, dotR, IM_COL32(255, 255, 255, 200), 12, 1.0f);

            if (ImGui::Checkbox("Unlit", &state.unlit) && state.unlit)
                { state.toon = false; state.fresnel = false; }
            if (ImGui::Checkbox("Toon", &state.toon) && state.toon)
                { state.unlit = false; state.fresnel = false; }
            if (ImGui::Checkbox("Fresnel", &state.fresnel) && state.fresnel)
                { state.unlit = false; state.toon = false; }
            ImGui::Checkbox("Specular", &state.specular);
            if (state.specular) {
                ImGui::PushItemWidth(120 * state.uiScale);
                ImGui::SliderFloat("Roughness", &state.specRoughness, 0.0f, 1.0f, "%.2f");
                ImGui::PopItemWidth();
            }

            ImGui::Spacing();

            // Gradient ramp widget
            {
                static int selectedStop = 0;
                auto& stops = state.rampStops;
                float rampW = s(100);
                float rampH = s(12);
                float handleH = s(8);

                // Sort stops by position, track selected — only when not dragging
                static bool wasDragging = false;
                bool anyDragging = false;

                // + / - buttons
                float btnH = ImGui::GetFrameHeight();
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {s(4), s(0)});
                ImGui::PushStyleColor(ImGuiCol_Button, {0.22f, 0.22f, 0.26f, 1.0f});
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.30f, 0.30f, 0.35f, 1.0f});
                if (ImGui::Button("+##ramp", {btnH, btnH})) {
                    // Add stop at midpoint of selected stop and its neighbor
                    float newT = 0.5f;
                    if (selectedStop < (int)stops.size() - 1)
                        newT = (stops[selectedStop].first + stops[selectedStop + 1].first) * 0.5f;
                    else if (selectedStop > 0)
                        newT = (stops[selectedStop - 1].first + stops[selectedStop].first) * 0.5f;
                    // Interpolate brightness
                    float bri = 0.5f;
                    for (int si = 0; si < (int)stops.size() - 1; si++) {
                        if (newT >= stops[si].first && newT <= stops[si + 1].first) {
                            float f = (newT - stops[si].first) / (stops[si + 1].first - stops[si].first);
                            bri = stops[si].second + f * (stops[si + 1].second - stops[si].second);
                            break;
                        }
                    }
                    stops.push_back({newT, bri});
                    selectedStop = (int)stops.size() - 1;
                }
                ImGui::SameLine(0, s(1));
                if (ImGui::Button("-##ramp", {btnH, btnH}) && (int)stops.size() > 2 && selectedStop > 0 && selectedStop < (int)stops.size() - 1) {
                    stops.erase(stops.begin() + selectedStop);
                    if (selectedStop >= (int)stops.size()) selectedStop = (int)stops.size() - 1;
                }
                ImGui::PopStyleColor(2);
                ImGui::SameLine(0, s(4));
                const char* interpNames[] = {"Ease", "Cardinal", "Linear", "B-Spline", "Constant"};
                int interpIdx = (int)state.rampInterp;
                ImGui::SetNextItemWidth(s(45));
                if (ImGui::BeginCombo("##RampInterp", interpNames[interpIdx], ImGuiComboFlags_NoArrowButton)) {
                    for (int i = 0; i < 5; i++) {
                        bool sel = (i == interpIdx);
                        if (ImGui::Selectable(interpNames[i], sel))
                            state.rampInterp = (UIState::RampInterp)i;
                        if (sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopStyleVar();

                // Gradient bar
                ImVec2 rPos = ImGui::GetCursorScreenPos();
                ImDrawList* rdl = ImGui::GetWindowDrawList();

                // Sort a copy for gradient drawing, keep original order for handle drag
                auto sortedStops = stops;
                std::sort(sortedStops.begin(), sortedStops.end(),
                    [](auto& a, auto& b){ return a.first < b.first; });

                // Helper: sample ramp at position t using current interpolation
                auto sampleRampUI = [&](float t) -> float {
                    if (sortedStops.empty()) return 0.0f;
                    if (t <= sortedStops[0].first) return sortedStops[0].second;
                    if (t >= sortedStops.back().first) return sortedStops.back().second;
                    for (int i = 0; i < (int)sortedStops.size() - 1; i++) {
                        if (t >= sortedStops[i].first && t <= sortedStops[i + 1].first) {
                            float f = (t - sortedStops[i].first) / (sortedStops[i + 1].first - sortedStops[i].first);
                            float v0 = sortedStops[i].second, v1 = sortedStops[i + 1].second;
                            switch (state.rampInterp) {
                            case UIState::RampInterp::Constant:
                                return v0;
                            case UIState::RampInterp::Ease:
                                f = f * f * (3.0f - 2.0f * f); // smoothstep
                                return v0 + f * (v1 - v0);
                            case UIState::RampInterp::BSpline:
                            case UIState::RampInterp::Cardinal:
                                f = f * f * (3.0f - 2.0f * f); // approximate
                                return v0 + f * (v1 - v0);
                            default: // Linear
                                return v0 + f * (v1 - v0);
                            }
                        }
                    }
                    return stops.back().second;
                };

                // Draw gradient bar pixel by pixel for accurate preview
                int pixW = (int)rampW;
                for (int px = 0; px < pixW; px++) {
                    float t = (float)px / (float)pixW;
                    float val = sampleRampUI(t);
                    int b = (int)(val * 255);
                    if (b < 0) b = 0; if (b > 255) b = 255;
                    rdl->AddRectFilled(
                        {rPos.x + px, rPos.y},
                        {rPos.x + px + 1, rPos.y + rampH},
                        IM_COL32(b, b, b, 255));
                }
                // Fill after last stop
                if (!stops.empty() && stops.back().first < 1.0f) {
                    int b = (int)(stops.back().second * 255);
                    rdl->AddRectFilled({rPos.x + stops.back().first * rampW, rPos.y},
                        {rPos.x + rampW, rPos.y + rampH}, IM_COL32(b, b, b, 255));
                }
                rdl->AddRect(rPos, {rPos.x + rampW, rPos.y + rampH}, IM_COL32(80, 80, 90, 255));
                ImGui::InvisibleButton("##RampBar", {rampW, rampH});

                // Handles below the bar
                float handleY = rPos.y + rampH;
                for (int si = 0; si < (int)stops.size(); si++) {
                    float hx = rPos.x + stops[si].first * rampW;
                    float hy = handleY;
                    int bv = (int)(stops[si].second * 255);
                    bool sel = (si == selectedStop);

                    // Handle shape: pentagon/house
                    ImVec2 pts[5] = {
                        {hx, hy},
                        {hx - s(4), hy + s(3)},
                        {hx - s(4), hy + handleH},
                        {hx + s(4), hy + handleH},
                        {hx + s(4), hy + s(3)},
                    };
                    ImU32 handleCol = sel ? IM_COL32(220, 220, 220, 255) : IM_COL32(140, 140, 140, 255);
                    rdl->AddConvexPolyFilled(pts, 5, handleCol);
                    rdl->AddRectFilled({hx - s(2.5f), hy + s(3)}, {hx + s(2.5f), hy + handleH - s(0.5f)},
                        IM_COL32(bv, bv, bv, 255));
                    if (sel) rdl->AddPolyline(pts, 5, IM_COL32(255, 255, 255, 255), ImDrawFlags_Closed, 1.0f);

                    // Interaction
                    ImGui::SetCursorScreenPos({hx - s(5), hy});
                    char hid[16]; snprintf(hid, sizeof(hid), "##rh%d", si);
                    ImGui::InvisibleButton(hid, {s(10), handleH});

                    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
                        selectedStop = si;

                    if (ImGui::IsItemActive()) {
                        anyDragging = true;
                        selectedStop = si;
                        float mx = ImGui::GetIO().MousePos.x - rPos.x;
                        float newT = mx / rampW;
                        if (newT < 0.0f) newT = 0.0f;
                        if (newT > 1.0f) newT = 1.0f;
                        stops[si].first = newT;

                        // Scroll to change brightness
                        float scroll = ImGui::GetIO().MouseWheel;
                        if (scroll != 0.0f) {
                            stops[si].second += scroll * 0.05f;
                            if (stops[si].second < 0.0f) stops[si].second = 0.0f;
                            if (stops[si].second > 1.0f) stops[si].second = 1.0f;
                        }
                    }
                }

                // Sort when drag ends
                if (wasDragging && !anyDragging) {
                    float selVal = stops[selectedStop].second;
                    float selP = stops[selectedStop].first;
                    std::sort(stops.begin(), stops.end(),
                        [](auto& a, auto& b){ return a.first < b.first; });
                    for (int i = 0; i < (int)stops.size(); i++)
                        if (stops[i].first == selP && stops[i].second == selVal) { selectedStop = i; break; }
                }
                wasDragging = anyDragging;

                ImGui::SetCursorScreenPos({rPos.x, handleY + handleH + s(2)});
                ImGui::Dummy({0, 0});

                // Grayscale picker for selected stop
                if (selectedStop >= 0 && selectedStop < (int)stops.size()) {
                    float pickW = rampW;
                    float pickH = s(10);
                    ImVec2 pPos = ImGui::GetCursorScreenPos();

                    // Draw black-to-white gradient
                    rdl->AddRectFilledMultiColor(pPos, {pPos.x + pickW, pPos.y + pickH},
                        IM_COL32(0, 0, 0, 255), IM_COL32(255, 255, 255, 255),
                        IM_COL32(255, 255, 255, 255), IM_COL32(0, 0, 0, 255));
                    rdl->AddRect(pPos, {pPos.x + pickW, pPos.y + pickH}, IM_COL32(80, 80, 90, 255));

                    // Marker for current brightness
                    float markerX = pPos.x + stops[selectedStop].second * pickW;
                    rdl->AddTriangleFilled(
                        {markerX, pPos.y - s(2)},
                        {markerX - s(2), pPos.y - s(5)},
                        {markerX + s(2), pPos.y - s(5)},
                        IM_COL32(255, 255, 255, 255));

                    ImGui::InvisibleButton("##GrayPick", {pickW, pickH});
                    if (ImGui::IsItemActive()) {
                        float mx = ImGui::GetIO().MousePos.x - pPos.x;
                        float val = mx / pickW;
                        if (val < 0.0f) val = 0.0f;
                        if (val > 1.0f) val = 1.0f;
                        stops[selectedStop].second = val;
                    }
                }
            }
            // Specular ramp widget
            if (state.specular) {
                ImGui::Spacing();
                ImGui::Text("Specular Ramp");

                static int specSelStop = 0;
                auto& sStops = state.specRampStops;
                float sRampW = s(100);
                float sRampH = s(12);
                float sHandleH = s(8);

                static bool specWasDrag = false;
                bool specAnyDrag = false;

                // +/- buttons
                float sBtnH = ImGui::GetFrameHeight();
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {s(4), s(0)});
                ImGui::PushStyleColor(ImGuiCol_Button, {0.22f, 0.22f, 0.26f, 1.0f});
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.30f, 0.30f, 0.35f, 1.0f});
                if (ImGui::Button("+##spec", {sBtnH, sBtnH})) {
                    float newT = 0.5f;
                    if (specSelStop < (int)sStops.size() - 1)
                        newT = (sStops[specSelStop].first + sStops[specSelStop + 1].first) * 0.5f;
                    sStops.push_back({newT, 0.5f});
                    specSelStop = (int)sStops.size() - 1;
                }
                ImGui::SameLine(0, s(1));
                if (ImGui::Button("-##spec", {sBtnH, sBtnH}) && (int)sStops.size() > 2 && specSelStop > 0 && specSelStop < (int)sStops.size() - 1) {
                    sStops.erase(sStops.begin() + specSelStop);
                    if (specSelStop >= (int)sStops.size()) specSelStop = (int)sStops.size() - 1;
                }
                ImGui::PopStyleColor(2);
                ImGui::SameLine(0, s(4));
                const char* sInterpNames[] = {"Ease", "Cardinal", "Linear", "B-Spline", "Constant"};
                int sInterpIdx = (int)state.specRampInterp;
                ImGui::SetNextItemWidth(s(45));
                if (ImGui::BeginCombo("##SpecInterp", sInterpNames[sInterpIdx], ImGuiComboFlags_NoArrowButton)) {
                    for (int i = 0; i < 5; i++) {
                        bool sel = (i == sInterpIdx);
                        if (ImGui::Selectable(sInterpNames[i], sel))
                            state.specRampInterp = (UIState::RampInterp)i;
                        if (sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::PopStyleVar();

                // Gradient bar
                ImVec2 sPos = ImGui::GetCursorScreenPos();
                ImDrawList* sdl = ImGui::GetWindowDrawList();
                auto sortedSpec = sStops;
                std::sort(sortedSpec.begin(), sortedSpec.end(),
                    [](auto& a, auto& b){ return a.first < b.first; });

                auto sampleSpec = [&](float t) -> float {
                    if (sortedSpec.empty()) return 0.0f;
                    if (t <= sortedSpec[0].first) return sortedSpec[0].second;
                    if (t >= sortedSpec.back().first) return sortedSpec.back().second;
                    for (int i = 0; i < (int)sortedSpec.size() - 1; i++) {
                        if (t >= sortedSpec[i].first && t <= sortedSpec[i + 1].first) {
                            float f = (t - sortedSpec[i].first) / (sortedSpec[i + 1].first - sortedSpec[i].first);
                            float v0 = sortedSpec[i].second, v1 = sortedSpec[i + 1].second;
                            if (state.specRampInterp == UIState::RampInterp::Constant) return v0;
                            if (state.specRampInterp != UIState::RampInterp::Linear)
                                f = f * f * (3.0f - 2.0f * f);
                            return v0 + f * (v1 - v0);
                        }
                    }
                    return sortedSpec.back().second;
                };

                int sPxW = (int)sRampW;
                for (int px = 0; px < sPxW; px++) {
                    float t = (float)px / (float)sPxW;
                    float val = sampleSpec(t);
                    int b = std::min(255, std::max(0, (int)(val * 255)));
                    sdl->AddRectFilled({sPos.x + px, sPos.y}, {sPos.x + px + 1, sPos.y + sRampH}, IM_COL32(b, b, b, 255));
                }
                sdl->AddRect(sPos, {sPos.x + sRampW, sPos.y + sRampH}, IM_COL32(80, 80, 90, 255));
                ImGui::InvisibleButton("##SpecBar", {sRampW, sRampH});

                // Handles
                float sHandleY = sPos.y + sRampH;
                for (int si = 0; si < (int)sStops.size(); si++) {
                    float hx = sPos.x + sStops[si].first * sRampW;
                    float hy = sHandleY;
                    int bv = (int)(sStops[si].second * 255);
                    bool sel = (si == specSelStop);
                    ImVec2 pts[5] = {
                        {hx, hy}, {hx - s(4), hy + s(3)}, {hx - s(4), hy + sHandleH},
                        {hx + s(4), hy + sHandleH}, {hx + s(4), hy + s(3)},
                    };
                    sdl->AddConvexPolyFilled(pts, 5, sel ? IM_COL32(220, 220, 220, 255) : IM_COL32(140, 140, 140, 255));
                    sdl->AddRectFilled({hx - s(2.5f), hy + s(3)}, {hx + s(2.5f), hy + sHandleH - s(0.5f)}, IM_COL32(bv, bv, bv, 255));
                    if (sel) sdl->AddPolyline(pts, 5, IM_COL32(255, 255, 255, 255), ImDrawFlags_Closed, 1.0f);

                    ImGui::SetCursorScreenPos({hx - s(5), hy});
                    char hid[16]; snprintf(hid, sizeof(hid), "##sh%d", si);
                    ImGui::InvisibleButton(hid, {s(10), sHandleH});
                    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) specSelStop = si;
                    if (ImGui::IsItemActive()) {
                        specAnyDrag = true;
                        specSelStop = si;
                        float mx = ImGui::GetIO().MousePos.x - sPos.x;
                        float newT = mx / sRampW;
                        if (newT < 0.0f) newT = 0.0f;
                        if (newT > 1.0f) newT = 1.0f;
                        sStops[si].first = newT;
                        float scroll = ImGui::GetIO().MouseWheel;
                        if (scroll != 0.0f) {
                            sStops[si].second += scroll * 0.05f;
                            if (sStops[si].second < 0.0f) sStops[si].second = 0.0f;
                            if (sStops[si].second > 1.0f) sStops[si].second = 1.0f;
                        }
                    }
                }
                if (specWasDrag && !specAnyDrag) {
                    float sp = sStops[specSelStop].first, sv = sStops[specSelStop].second;
                    std::sort(sStops.begin(), sStops.end(), [](auto& a, auto& b){ return a.first < b.first; });
                    for (int i = 0; i < (int)sStops.size(); i++)
                        if (sStops[i].first == sp && sStops[i].second == sv) { specSelStop = i; break; }
                }
                specWasDrag = specAnyDrag;
                ImGui::SetCursorScreenPos({sPos.x, sHandleY + sHandleH + s(2)});
                ImGui::Dummy({0, 0});

                // Grayscale picker
                if (specSelStop >= 0 && specSelStop < (int)sStops.size()) {
                    float pickW = sRampW, pickH = s(10);
                    ImVec2 pPos = ImGui::GetCursorScreenPos();
                    sdl->AddRectFilledMultiColor(pPos, {pPos.x + pickW, pPos.y + pickH},
                        IM_COL32(0, 0, 0, 255), IM_COL32(255, 255, 255, 255),
                        IM_COL32(255, 255, 255, 255), IM_COL32(0, 0, 0, 255));
                    sdl->AddRect(pPos, {pPos.x + pickW, pPos.y + pickH}, IM_COL32(80, 80, 90, 255));
                    float markerX = pPos.x + sStops[specSelStop].second * pickW;
                    sdl->AddTriangleFilled({markerX, pPos.y - s(2)}, {markerX - s(2), pPos.y - s(5)}, {markerX + s(2), pPos.y - s(5)}, IM_COL32(255, 255, 255, 255));
                    ImGui::InvisibleButton("##SpecPick", {pickW, pickH});
                    if (ImGui::IsItemActive()) {
                        float mx = ImGui::GetIO().MousePos.x - pPos.x;
                        float val = mx / pickW;
                        if (val < 0.0f) val = 0.0f;
                        if (val > 1.0f) val = 1.0f;
                        sStops[specSelStop].second = val;
                    }
                }
            }

            ImGui::EndPopup();
        }
        ImGui::PopStyleColor();
        ImGui::End();
        ImGui::PopStyleColor();
    }

    // Orientation gizmo (simple XYZ text for now)
    float gizX = vp->Pos.x + vp->Size.x - rightPanelWidth() - s(57);
    float gizY = vpY + s(11);
    ImGui::SetNextWindowPos({gizX, gizY});
    ImGui::SetNextWindowSize({0, 0});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0, 0, 0, 0});
    ImGui::Begin("##Gizmo", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoInputs);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    float len = s(16);
    ImVec2 center = {gizX + s(21), gizY + s(21)};

    // X axis (red)
    dl->AddLine(center, {center.x + len, center.y}, IM_COL32(230, 80, 80, 255), 2.0f);
    dl->AddText({center.x + len + s(3), center.y - s(5)}, IM_COL32(230, 80, 80, 255), "X");

    // Y axis (green)
    dl->AddLine(center, {center.x, center.y - len}, IM_COL32(80, 200, 80, 255), 2.0f);
    dl->AddText({center.x - s(3), center.y - len - s(13)}, IM_COL32(80, 200, 80, 255), "Y");

    // Z axis (blue)
    dl->AddLine(center, {center.x - len * 0.6f, center.y + len * 0.6f}, IM_COL32(80, 120, 230, 255), 2.0f);
    dl->AddText({center.x - len * 0.6f - s(10), center.y + len * 0.6f - s(1)}, IM_COL32(80, 120, 230, 255), "Z");

    ImGui::End();
    ImGui::PopStyleColor();

    // Selection mode buttons — bottom center of viewport (Model mode only)
    if (state.editorMode != EditorMode::Model) return;
    float vpW = vp->Size.x - toolbarWidth() - rightPanelWidth();
    float selBtnSz = s(24);
    float selGap = s(4);
    float totalSelW = selBtnSz * 4 + selGap * 3;
    float selX = vpX + (vpW - totalSelW) * 0.5f;
    float selY = vp->Pos.y + vp->Size.y - statusBarHeight() - selBtnSz - s(10);

    ImGui::SetNextWindowPos({selX - s(6), selY - s(6)});
    ImGui::SetNextWindowSize({0, 0});
    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0.12f, 0.12f, 0.14f, 0.85f});
    ImGui::Begin("##SelMode", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoFocusOnAppearing);

    struct SelDef { SelectMode mode; const char* icon; const char* id; };
    SelDef selDefs[] = {
        {SelectMode::Object, ICON_SEL_OBJ,  "##selObj"},
        {SelectMode::Vertex, ICON_SEL_VTX,  "##selVtx"},
        {SelectMode::Edge,   ICON_SEL_EDGE, "##selEdge"},
        {SelectMode::Face,   ICON_SEL_FACE, "##selFace"},
    };

    for (int i = 0; i < 4; i++) {
        bool active = (state.selectMode == selDefs[i].mode);
        if (active)
            ImGui::PushStyleColor(ImGuiCol_Button, Colors::accent());
        else
            ImGui::PushStyleColor(ImGuiCol_Button, Colors::input());

        ImVec2 btnPos = ImGui::GetCursorScreenPos();
        if (ImGui::Button(selDefs[i].id, {selBtnSz, selBtnSz}))
            state.selectMode = selDefs[i].mode;

        if (g_selTex[i]) {
            // Draw texture centered on button
            float pad = s(4);
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddImage((ImTextureID)(intptr_t)g_selTex[i],
                ImVec2(btnPos.x + pad, btnPos.y + pad),
                ImVec2(btnPos.x + selBtnSz - pad, btnPos.y + selBtnSz - pad));
        } else if (g_iconFont) {
            float iconSz = s(11);
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            float ix = btnPos.x + (selBtnSz - iconSz) * 0.5f;
            float iy = btnPos.y + (selBtnSz - iconSz) * 0.5f;
            drawList->AddText(g_iconFont, iconSz, ImVec2(ix, iy),
                ImGui::GetColorU32(ImGuiCol_Text), selDefs[i].icon);
        }

        ImGui::PopStyleColor();
        if (i < 3) ImGui::SameLine(0, selGap);
    }

    ImGui::End();
    ImGui::PopStyleColor();
}

float ui_top_bar_height()    { return topBarHeight(); }
float ui_toolbar_width()     { return toolbarWidth(); }
float ui_right_panel_width() { return rightPanelWidth(); }
float ui_status_bar_height() { return statusBarHeight(); }

// ---------------------------------------------------------------------------
// Settings persistence (reflow.ini next to executable)
// ---------------------------------------------------------------------------
static std::string settings_path()
{
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    std::string p(buf);
    auto pos = p.find_last_of("\\/");
    if (pos != std::string::npos) p = p.substr(0, pos + 1);
    return p + "reflow.ini";
}

void ui_save_settings(const UIState& state)
{
    FILE* f = fopen(settings_path().c_str(), "w");
    if (!f) return;
    fprintf(f, "ui_scale=%.2f\n", state.uiScale);
    fclose(f);
}

void ui_load_settings(UIState& state)
{
    FILE* f = fopen(settings_path().c_str(), "r");
    if (!f) return;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        float v;
        if (sscanf(line, "ui_scale=%f", &v) == 1) {
            state.uiScale = v;
        }
    }
    fclose(f);
}

} // namespace rf
