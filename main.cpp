
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL3_ttf/SDL_ttf.h>

#include <JFLX/SDL3/AudioHandler.hpp>
#include <JFLX/SDL3/TextRenderer.hpp>
#define JFLX_TEXTURE_HANDLER_DEBUG
#include <JFLX/SDL3/textureHandler.hpp>

#include <ImGui/imgui.h>
#include <ImGui/imgui_impl_sdl3.h>
#include <ImGui/imgui_impl_sdlrenderer3.h>

#define JFLX_LOGGING_NO_DEBUG
#include <JFLX/Logging.hpp>

#include <cstdio>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

#define WM_TRAY_ICON  (WM_USER + 1)
#define TRAY_ICON_ID  1

// Queried at runtime from the primary display; defaults act as fallback only.
static int WINDOW_W = 1920;
static int WINDOW_H = 1080;

static constexpr float WIDGET_W = 375.0f;
static constexpr float WIDGET_H = 475.0f;

static bool                     gRunning       = true;
static int                      gWindowOriginX = 0;
static int                      gWindowOriginY = 0;
static bool                     gShowBuddy     = true;
static bool                     gWindowVisible = true;
static bool                     gShowSettings  = false;
static SDL_Window*              gSdlWindow     = nullptr;
static std::string              gBuddyName     = "cat";
static std::vector<std::string> gFolderNames = {};
static SDL_Rect                 gCurrentDisplayBounds = { 0, 0, 1920, 1080 };

JFLX::SDL3::AudioHandler   audioHandler;
JFLX::SDL3::TextRenderer   textRenderer;
JFLX::SDL3::TextureHandler textureHandler;

#define BuddyDir installationDir
#include <include/buddy.hpp>
ByteBuddy::Buddy buddy;

#include <filesystem>
std::string installationDir = std::filesystem::current_path().string();

// Tray globals — hidden message-only HWND so the tray icon never shows a taskbar button.
static HWND            g_hwnd  = nullptr;
static HINSTANCE       g_hInst = nullptr;
static NOTIFYICONDATAW g_nid   = {};

static LRESULT CALLBACK TrayWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    // Ctrl+Alt+P registered as hot key ID 1 — toggles the settings panel.
    if (msg == WM_HOTKEY && wp == 1)
    {
        gShowSettings = !gShowSettings;
        if (gShowSettings) {
            SDL_RaiseWindow(gSdlWindow);
        }
        return 0;
    }

    if (msg == WM_TRAY_ICON)
    {
        // Single or double left-click → show the overlay window.
        if (lp == WM_LBUTTONDBLCLK || lp == WM_LBUTTONUP)
            gWindowVisible = true;

        if (lp == WM_RBUTTONUP)
        {
            POINT pt;
            GetCursorPos(&pt);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, 1001, L"Show");
            AppendMenuW(menu, MF_STRING, 1002, L"Settings");
            AppendMenuW(menu, MF_STRING, 1003, L"Quit");
            // SetForegroundWindow is required so the menu dismisses on click-away.
            SetForegroundWindow(hwnd);
            int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY,
                                     pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(menu);
            if      (cmd == 1001) {
                gWindowVisible = true;
            }
            else if (cmd == 1002) {
                gShowSettings = true;
                SDL_RaiseWindow(gSdlWindow);
            }
            else if (cmd == 1003) {
                gRunning = false;
            }
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void TrayIcon_Create(SDL_Window*)
{
    g_hInst = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc   = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = TrayWndProc;
    wc.hInstance     = g_hInst;
    wc.lpszClassName = L"DesktopPetTrayClass";
    RegisterClassExW(&wc);

    // HWND_MESSAGE → message-only window, never visible on screen.
    g_hwnd = CreateWindowExW(0, L"DesktopPetTrayClass", L"",
                              0, 0, 0, 0, 0,
                              HWND_MESSAGE, nullptr, g_hInst, nullptr);

    g_nid                  = {};
    g_nid.cbSize           = sizeof(g_nid);
    g_nid.hWnd             = g_hwnd;
    g_nid.uID              = TRAY_ICON_ID;
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY_ICON;
    g_nid.hIcon            = LoadIconW(nullptr, (LPCWSTR)IDI_APPLICATION);
    wcsncpy_s(g_nid.szTip, 128, L"DesktopPet", _TRUNCATE);

    Shell_NotifyIconW(NIM_ADD, &g_nid);
    RegisterHotKey(g_hwnd, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, 'P');
}

// Must be called every frame so hotkey and tray messages are processed.
static void TrayIcon_PumpMessages()
{
    MSG msg;
    while (PeekMessageW(&msg, g_hwnd, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static void TrayIcon_Destroy()
{
    UnregisterHotKey(g_hwnd, 1);
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    DestroyWindow(g_hwnd);
}

bool initSDL() {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    // Get bounding rect across ALL displays
    int displayCount = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&displayCount);

    int minX = INT_MAX, minY = INT_MAX;
    int maxX = INT_MIN, maxY = INT_MIN;

    for (int i = 0; i < displayCount; ++i)
    {
        SDL_Rect bounds{};
        if (SDL_GetDisplayBounds(displays[i], &bounds)) {
            minX = min(minX, bounds.x);
            minY = min(minY, bounds.y);
            maxX = max(maxX, bounds.x + bounds.w);
            maxY = max(maxY, bounds.y + bounds.h);
        }
    }
    SDL_free(displays);

    WINDOW_W = maxX - minX;
    WINDOW_H = maxY - minY;

    // Store the top-left origin so we can position the window correctly
    // (monitors may not start at 0,0 — e.g. a secondary monitor left of primary)
    gWindowOriginX = minX;
    gWindowOriginY = minY;

    return true;
}

SDL_Window* initWindow(HWND& outHwnd) {
    SDL_WindowFlags flags = SDL_WINDOW_TRANSPARENT | SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP;

    SDL_Window* window = SDL_CreateWindow("Byte Buddy", WINDOW_W, WINDOW_H, flags);
    if (!window)
    {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return nullptr;
    }

    SDL_SetWindowPosition(window, gWindowOriginX, gWindowOriginY);

    gSdlWindow = window;
    SDL_SetWindowMouseGrab(window, false);

    outHwnd = (HWND)SDL_GetPointerProperty(
        SDL_GetWindowProperties(window),
        SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);

    if (outHwnd)
    {
        // WS_EX_LAYERED   → required for per-pixel alpha (transparent overlay).
        // WS_EX_TRANSPARENT → passes mouse clicks through to windows below.
        // WS_EX_TOOLWINDOW  → hides the window from Alt+Tab and the taskbar.
        LONG_PTR ex = GetWindowLongPtrW(outHwnd, GWL_EXSTYLE);
        ex &= ~WS_EX_APPWINDOW;
        ex |=  WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW;
        SetWindowLongPtrW(outHwnd, GWL_EXSTYLE, ex);
    }

    SDL_Surface* icon = SDL_LoadBMP((installationDir + "/resources/icon/icon.png").c_str());
    if (icon) {
        SDL_SetWindowIcon(window, icon);
        SDL_DestroySurface(icon);
    }

    return window;
}

SDL_Renderer* initRenderer(SDL_Window* window)
{
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer)
    {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return nullptr;
    }

    // BLEND is required so transparent pixels actually clear the framebuffer.
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    return renderer;
}

ImGuiIO& initImGui(SDL_Window* window, SDL_Renderer* renderer)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_None;

    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 8.0f;
    ImGui::GetStyle().Alpha          = 0.92f;

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    return io;
}

std::vector<std::string> GetAllFolderNames(const fs::path& rootPath) {
    std::vector<std::string> folders;

    if (!fs::exists(rootPath) || !fs::is_directory(rootPath)) {
        return folders;
    }

    for (const auto& entry : fs::directory_iterator( rootPath, fs::directory_options::skip_permission_denied)) {
        if (entry.is_directory()) {
            folders.push_back(entry.path().filename().string());
        }
    }

    return folders;
}

void initJFLXClasses(SDL_Renderer* r) {
    textRenderer.setDefaultRenderer(r);
    textRenderer.loadFont(installationDir + "/resources/font/font.ttf");

    textureHandler.setRenderer(r);
    textureHandler.loadTextureFolder(installationDir + "/resources/textures/");

    audioHandler.loadSounds(installationDir + "/resources/sfx");

    gFolderNames = GetAllFolderNames(installationDir + "/resources/textures/");
}

void initBuddy(std::string& name) {
    buddy.setTypeName(name);
    buddy.setTextureHandler(&textureHandler);
    buddy.setTextRenderer(&textRenderer);
    ByteBuddy::windowBounds wBounds{ WINDOW_H, WINDOW_W, WINDOW_H};
    buddy.setWindowBounds(wBounds);
    buddy.setPosition(WINDOW_W * 0.5f, WINDOW_H);
}

SDL_Rect GetDisplayBoundsForPoint(float globalX, float globalY) {
    int displayCount = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&displayCount);

    SDL_Rect result{};
    // Fallback to primary
    SDL_GetDisplayBounds(SDL_GetPrimaryDisplay(), &result);

    for (int i = 0; i < displayCount; ++i)
    {
        SDL_Rect bounds{};
        if (SDL_GetDisplayBounds(displays[i], &bounds))
        {
            if (globalX >= bounds.x && globalX < bounds.x + bounds.w &&
                globalY >= bounds.y && globalY < bounds.y + bounds.h)
            {
                result = bounds;
                break;
            }
        }
    }

    SDL_free(displays);
    return result;
}

std::string ShowBuddyTypePicker()
{
    static int selectedIndex = -1;

    const char* previewLabel = (selectedIndex >= 0) ? gFolderNames[selectedIndex].c_str() : "-- Buddy Type --";

    ImGui::TextUnformatted("Buddy Type ");
    ImGui::SameLine(200.0f);

    if (ImGui::BeginCombo("##buddyType", previewLabel))
    {
        for (int i = 0; i < static_cast<int>(gFolderNames.size()); ++i)
        {
            bool isSelected = (selectedIndex == i);
            if (ImGui::Selectable(gFolderNames[i].c_str(), isSelected))
                selectedIndex = i;
            if (isSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    return (selectedIndex >= 0) ? gFolderNames[selectedIndex] : "";
}

void renderImGuiSettings(ImVec2& widgetPos, bool& firstFrame, bool& gWindowVisible, float& mouseX, float& mouseY) {
    if (ImGui::Begin("ByteBuddy_Settings", nullptr, ImGuiWindowFlags_None)) {
        widgetPos = ImGui::GetWindowPos();

        ImGui::TextUnformatted("ByteBuddy##Settings");
        ImGui::Separator();
        ImGui::Spacing();

        // Toggle hotkey (read-only display)
        ImGui::TextUnformatted("Toggle Settings: ");
        ImGui::SameLine(200.0f);
        ImGui::SetNextItemWidth(120.0f);
        ImGui::LabelText("##toggleSettings", "Ctrl+Alt+P");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextUnformatted("Buddy: ");

        // --- Buddy Type (folders) ---
        std::string selectedType = ShowBuddyTypePicker();
        if (!selectedType.empty() && selectedType != buddy.getTypeName()) {
            buddy.setTypeName(selectedType);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // --- Buddy Name ---
        ImGui::TextUnformatted("Buddy Name");
        ImGui::SameLine(200.0f);
        static char nameBuffer[128] = "cat";
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::InputText("##buddyname", nameBuffer, sizeof(nameBuffer),ImGuiInputTextFlags_EnterReturnsTrue)) {
            gBuddyName = nameBuffer;
            buddy.setName(gBuddyName);   // setName instead of setTypeName
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextUnformatted("Show Buddy");
        ImGui::SameLine(200.0f);
        if (ImGui::Button(gShowBuddy ? "Shown##buddy" : "Hidden##buddy", { 60.0f, 0 }))
            gShowBuddy = !gShowBuddy;

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextUnformatted("Window");
        ImGui::SameLine(200.0f);
        if (ImGui::Button(gWindowVisible ? "Shown##win" : "Hidden##win", { 60.0f, 0 }))
            gWindowVisible = !gWindowVisible;

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── Position ─────────────────────────────────────────────────────────
        ImGui::TextUnformatted("Position:");
        ImGui::Spacing();
        ImGui::Spacing();

        {
            float x = buddy.getX();
            ImGui::TextUnformatted("X");
            ImGui::SameLine(200.0f);
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::SliderFloat("##pos_x", &x, 0.0f, static_cast<float>(WINDOW_W), "%.0f px"))
                buddy.setPosition(x, buddy.getY());
        }
        {
            float y = buddy.getY();
            ImGui::TextUnformatted("Y");
            ImGui::SameLine(200.0f);
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::SliderFloat("##pos_y", &y, 0.0f, static_cast<float>(WINDOW_H), "%.0f px"))
                buddy.setPosition(buddy.getX(), y);
        }
        {
            float y = buddy.getGroundY();
            ImGui::TextUnformatted("Ground Level");
            ImGui::SameLine(200.0f);
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::SliderFloat("##ground_pos_y", &y, 0.0f, static_cast<float>(WINDOW_H), "%.0f px"))
                buddy.setGroundY(y);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── Behaviour ────────────────────────────────────────────────────────
        ImGui::TextUnformatted("Behaviour:");
        ImGui::Spacing();
        ImGui::Spacing();

        {
            float speed = buddy.getSpeed();
            ImGui::TextUnformatted("Speed");
            ImGui::SameLine(200.0f);
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::SliderFloat("##speed", &speed, 0.1f, 10.0f, "%.2f"))
                buddy.setSpeed(speed);
        }
        {
            float scale = buddy.getScale();
            ImGui::TextUnformatted("Scale");
            ImGui::SameLine(200.0f);
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::SliderFloat("##scale", &scale, 1.0f, 10.0f, "%.1f x"))
                buddy.setScale(scale);
        }
        {
            // How many ticks the Buddy holds a state before picking a new random one.
            float stateMax = buddy.getStateTimeMax();
            ImGui::TextUnformatted("State time");
            ImGui::SameLine(200.0f);
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::SliderFloat("##statetime", &stateMax, 60.0f, 3600.0f, "%.0f ticks"))
                buddy.setStateTimeMax(stateMax);
        }
        {
            float stateMin = buddy.getStateTimerMin();
            ImGui::TextUnformatted("Min Animation Length");
            ImGui::SameLine(200.0f);
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::SliderFloat("##MinAniLen", &stateMin, 100.0f, 600.0f, "%.0f ticks"))
                buddy.setStateTimerMin(stateMin);
        }
        {
            float stateMax = buddy.getStateTimerMax();
            ImGui::TextUnformatted("Max Animation Length");
            ImGui::SameLine(200.0f);
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::SliderFloat("##MaxAniLen", &stateMax, 200.0f, 1200.0f, "%.0f ticks"))
                buddy.setStateTimerMax(stateMax);
        }
        {
            float fi = buddy.getFrameInterval();
            ImGui::TextUnformatted("Animation speed");
            ImGui::SameLine(200.0f);
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::SliderFloat("##Animation Speed", &fi, 1.0f, 10.0f, "%.0f"))
                buddy.setFrameInterval(fi);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── Direction ────────────────────────────────────────────────────────
        ImGui::TextUnformatted("Direction:");
        ImGui::Spacing();
        ImGui::Spacing();

        {
            int dir = buddy.getDirection();
            ImGui::TextUnformatted("Facing");
            ImGui::SameLine(200.0f);
            // Label reflects current state so the button reads as a toggle, not an action.
            const char* dirLabel = (dir == -1) ? "Right##dir" : "Left##dir";
            if (ImGui::Button(dirLabel, { 90.0f, 0 }))
                buddy.setDirection(dir * -1);
        }
        {
            int flip = buddy.getHorizontallyFlipped();
            ImGui::TextUnformatted("Render Flipped");
            ImGui::SameLine(200.0f);
            // Label reflects current state so the button reads as a toggle, not an action.
            const char* flipLabel = (flip == 1) ? "Normal##flip" : "Flipped##flip";
            if (ImGui::Button(flipLabel, { 90.0f, 0 }))
                buddy.setHorizontallyFlipped(!flip);
        }
        {
            int updateDirection = buddy.getUpdateDirectionWhenIdle();
            ImGui::TextUnformatted("Update Direction When Idle");
            ImGui::SameLine(200.0f);
            // Label reflects current state so the button reads as a toggle, not an action.
            const char* updateDirectioLabel = (updateDirection == 1) ? "Update##updateDirection" : "No Update##updateDirection";
            if (ImGui::Button(updateDirectioLabel, { 90.0f, 0 }))
                buddy.setUpdateDirectionWhenIdle(!updateDirection);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── State (read-only display + manual override) ───────────────────────
        ImGui::TextUnformatted("State:");
        ImGui::Spacing();
        ImGui::Spacing();

        ImGui::TextUnformatted("Change State");
        ImGui::SameLine(200.0f);
        if (ImGui::Button("change", { 60.0f, 0 }))
        {
            buddy.nextState();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        {
            ImGui::TextUnformatted("State");
            ImGui::SameLine(200.0f);
            ImGui::TextUnformatted(buddy.getState().c_str());
        }
        {
            ImGui::TextUnformatted("Frame");
            ImGui::SameLine(200.0f);
            ImGui::Text("%d", buddy.getFrame());
        }
        {
            ImGui::TextUnformatted("TargetX");
            ImGui::SameLine(200.0f);
            ImGui::Text("%d", buddy.getTarget()[0]);
        }
        {
            ImGui::TextUnformatted("TargetY");
            ImGui::SameLine(200.0f);
            ImGui::Text("%d", buddy.getTarget()[1]);
        }


        // Jump phase is only meaningful while the Buddy is jumping.
        if (buddy.getState() == "jump")
        {
            static constexpr const char* kPhaseNames[] = {
                "PreJump", "MidJump", "MidAir",
                "MidFall_0", "MidFall_1", "Landing_0", "Landing_1"
            };
            ByteBuddy::JumpPhase phase = buddy.getJumpPhase();
            const char* phaseName = (static_cast<int>(phase) < 7)
                                    ? kPhaseNames[static_cast<int>(phase)]
                                    : "?";
            ImGui::TextUnformatted("Jump phase");
            ImGui::SameLine(200.0f);
            ImGui::TextUnformatted(phaseName);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Spacing();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextUnformatted("Close App: ");
        ImGui::SameLine(200.0f);
        if (ImGui::Button("Quit", { 60.0f, 0 }))
        {
            gRunning = false;
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }
    ImGui::End();
}

int main(int /*argc*/, char* /*argv*/[]) {
    if (!initSDL())  return 1;
    if (!TTF_Init()) return 1;

    HWND nativeHwnd = nullptr;
    SDL_Window* window = initWindow(nativeHwnd);
    if (!window) return 1;

    SDL_Renderer* renderer = initRenderer(window);
    if (!renderer) return 1;

    ImGuiIO& io = initImGui(window, renderer);

    TrayIcon_Create(window);

    ImVec2 widgetPos = { (WINDOW_W - WIDGET_W) * 0.5f, WINDOW_H - WIDGET_H - 80.0f };
    bool firstFrame  = true;

    // Toggles WS_EX_TRANSPARENT on/off so mouse events pass through the overlay
    // when the cursor is not over interactive ImGui content.
    auto setClickThrough = [&](bool through)
    {
        if (!nativeHwnd) return;
        LONG_PTR ex = GetWindowLongPtrW(nativeHwnd, GWL_EXSTYLE);
        if (through) ex |=  WS_EX_TRANSPARENT;
        else         ex &= ~WS_EX_TRANSPARENT;
        SetWindowLongPtrW(nativeHwnd, GWL_EXSTYLE, ex);
    };

    initJFLXClasses(renderer);
    initBuddy(gBuddyName);

    SDL_GetDisplayBounds(SDL_GetPrimaryDisplay(), &gCurrentDisplayBounds);
    float initGroundY = static_cast<float>((gCurrentDisplayBounds.y + gCurrentDisplayBounds.h) - gWindowOriginY);
    buddy.setGroundY(initGroundY);
    buddy.setPosition(static_cast<float>(gCurrentDisplayBounds.x - gWindowOriginX + gCurrentDisplayBounds.w / 2), initGroundY);

    ImGui::SetNextWindowPos(widgetPos, firstFrame ? ImGuiCond_Always : ImGuiCond_Once);
    ImGui::SetNextWindowSize({ WIDGET_W, WIDGET_H });
    ImGui::SetNextWindowBgAlpha(0.75f);

    while (gRunning) {
        Uint64 start = SDL_GetPerformanceCounter();

        TrayIcon_PumpMessages();

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) {
                gRunning = false;
            }
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
                gWindowVisible = false;
            }
            if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST) {
                gShowSettings = false;
            }
            if (event.type == SDL_EVENT_WINDOW_FOCUS_GAINED) {
                gShowSettings = true;
                SDL_RaiseWindow(gSdlWindow);
            }
        }

        if (gWindowVisible) SDL_ShowWindow(window);
        else                SDL_HideWindow(window);

        if (!gWindowVisible)
        {
            // Skip rendering entirely when the window is hidden.
            SDL_Delay(50);
            continue;
        }

        float mouseX, mouseY;
        SDL_GetGlobalMouseState(&mouseX, &mouseY);
        float localMouseX = mouseX - gWindowOriginX;
        float localMouseY = mouseY - gWindowOriginY;

        // --- Display change detection ---
        SDL_Rect displayBounds = GetDisplayBoundsForPoint(mouseX, mouseY);
        if (displayBounds.x != gCurrentDisplayBounds.x || displayBounds.y != gCurrentDisplayBounds.y || displayBounds.w != gCurrentDisplayBounds.w || displayBounds.h != gCurrentDisplayBounds.h) {
            gCurrentDisplayBounds = displayBounds;

            // Bottom edge of THIS display in window-local render space.
            // displayBounds.y = global top of this monitor
            // displayBounds.h = this monitor's own pixel height (may differ from others)
            // Subtract gWindowOriginY to convert global → window-local
            int newGroundY = (displayBounds.y + displayBounds.h) - gWindowOriginY;

            // Clamp X so the buddy stays within this display's horizontal span (window-local)
            float newX = std::clamp(
                buddy.getX(),
                static_cast<float>(displayBounds.x - gWindowOriginX),
                static_cast<float>((displayBounds.x + displayBounds.w) - gWindowOriginX - 1)
            );
            buddy.setPosition(newX, newGroundY);

            // Keep window bounds reflecting the full virtual canvas
            ByteBuddy::windowBounds wBounds{ WINDOW_H, WINDOW_W, newGroundY };
            buddy.setWindowBounds(wBounds);
        }

        // Disable click-through while the cursor is over the settings widget.
        bool mouseOverWidget = gShowSettings && (localMouseX >= widgetPos.x) && (localMouseX <= widgetPos.x + WIDGET_W) && (localMouseY >= widgetPos.y) && (localMouseY <= widgetPos.y + WIDGET_H);

        bool imguiWantsInput = io.WantCaptureMouse || io.WantCaptureKeyboard || ImGui::IsAnyItemActive() || mouseOverWidget;
        setClickThrough(!imguiWantsInput);

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        if (gShowSettings)
            renderImGuiSettings(widgetPos, firstFrame, gWindowVisible, mouseX, mouseY);
        ImGui::Render();

        // Clear to fully transparent so the desktop shows through.
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);

        if (gShowBuddy)
        {
            buddy.updateBuddy(localMouseX, localMouseY);
            buddy.renderBuddy();
        }

        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        SDL_RenderPresent(renderer);

        // Cap to ~16 fps minimum (60 ms budget) to avoid burning the CPU.
        Uint64 end = SDL_GetPerformanceCounter();
        double elapsedMS = (end - start) * 1000.0 / SDL_GetPerformanceFrequency();
        if (elapsedMS < 60)
            SDL_Delay((Uint32)(60 - elapsedMS));
    }

    TrayIcon_Destroy();

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    textRenderer.cleanUp();

    TTF_Quit();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}