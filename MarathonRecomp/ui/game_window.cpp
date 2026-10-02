#include "game_window.h"
#include <gpu/video.h>
#include <os/logger.h>
#include <os/user.h>
#include <os/version.h>
#include <app.h>
#include <sdl_listener.h>

#if defined(__SWITCH__)
#include <switch.h>
#include <sys/stat.h>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#else
#include <SDL_syswm.h>
#endif

#if _WIN32
#include <dwmapi.h>
#include <shellscalingapi.h>
#endif

#include <res/images/game_icon.bmp.h>

bool m_isFullscreenKeyReleased = true;
bool m_isResizing = false;

#if defined(__SWITCH__)
namespace
{
    // [Switch] The window's size, per console mode, from sdmc:/switch/MarathonRecomp/resolution.txt (written with the
    // defaults when it is missing). The game renders at the window's size (app.cpp gives it the viewport when the game
    // starts), so this is its resolution. The game makes its render targets once, at that size, so the size is taken
    // at start, from the mode the console starts in; docking or undocking later keeps it (the console scales the
    // picture to the screen), and the other mode's size applies from the next start. UnleashedRecomp-NX, whose game
    // remakes its render targets, makes its swap chain again at the new mode's size instead.
    constexpr const char* RESOLUTION_FILE = "sdmc:/switch/MarathonRecomp/resolution.txt";
    constexpr int DEFAULT_DOCKED_HEIGHT = 900;
    constexpr int DEFAULT_HANDHELD_HEIGHT = 720;
    constexpr int MIN_WINDOW_HEIGHT = 480;
    constexpr int MAX_WINDOW_HEIGHT = 1080; // the largest NWindow the console shows

    constexpr const char* RESOLUTION_FILE_TEXT =
        "# MarathonRecomp-NX window size. The game renders at the window's size, so this is its resolution.\n"
        "# One line per console mode: the height in pixels, from 480 to 1080. The width follows for 16:9:\n"
        "#   480 = 854x480, 540 = 960x540, 576 = 1024x576, 648 = 1152x648, 720 = 1280x720,\n"
        "#   810 = 1440x810, 864 = 1536x864, 900 = 1600x900, 1080 = 1920x1080 (any height in between works).\n"
        "# A width and height also work (docked=1600x900); a size outside 480-1080 lines is brought into range.\n"
        "# The size is taken when the game starts, from the mode the console starts in. Docking or undocking while\n"
        "# playing keeps the size the game started with (the console scales the picture to the screen); restart the\n"
        "# game to use the other mode's size. A larger window costs GPU time (1080p draws 2.25 times the pixels of 720p).\n"
        "docked=900\n"
        "handheld=720\n";

    // Docked: the console's operation mode when the game starts (no applet message loop runs here, so it is read
    // once); the default display resolution if that fails (1080 lines when docked, 720 in handheld).
    bool QueryDocked()
    {
        const AppletOperationMode mode = appletGetOperationMode();
        if (mode == AppletOperationMode_Console)
            return true;

        if (mode == AppletOperationMode_Handheld)
            return false;

        s32 width = 0;
        s32 height = 0;
        return R_SUCCEEDED(appletGetDefaultDisplayResolution(&width, &height)) && height >= 1080;
    }

    // The 16:9 width of a height, even.
    int WidthForHeight(int height)
    {
        const int width = (height * 16 + 4) / 9;
        return width + (width & 1);
    }

    // "1080", "1080p" or "1920x1080" (the height is what counts for a 16:9 window; a width is kept when given). 0 when
    // the value is not a size.
    void ParseSize(const char* value, int& width, int& height)
    {
        width = 0;
        height = 0;
        char* end = nullptr;
        const long first = strtol(value, &end, 10);
        if (end == value || first <= 0)
            return;

        if (*end == 'x' || *end == 'X' || *end == '*')
        {
            const char* rest = end + 1;
            const long second = strtol(rest, &end, 10);
            if (end == rest || second <= 0)
                return;

            width = int(first);
            height = int(second);
            return;
        }

        height = int(first);
    }

    void WriteDefaultResolutionFile()
    {
        mkdir("sdmc:/switch", 0777);
        mkdir("sdmc:/switch/MarathonRecomp", 0777);
        if (FILE* file = fopen(RESOLUTION_FILE, "w"))
        {
            fputs(RESOLUTION_FILE_TEXT, file);
            fclose(file);
        }
    }

    struct WindowSize
    {
        int width;
        int height;
    };

    WindowSize ClampWindowSize(int width, int height, int defaultHeight)
    {
        if (height <= 0)
            height = defaultHeight;

        height = std::clamp(height, MIN_WINDOW_HEIGHT, MAX_WINDOW_HEIGHT);
        if (width <= 0)
            width = WidthForHeight(height);

        // The NWindow holds at most 1920x1080; a width given with the height keeps its own aspect ratio (the
        // Aspect Ratio option boxes the picture as for any window shape).
        width = std::clamp(width + (width & 1), 640, 1920);
        return { width, height };
    }

    // The docked and handheld sizes of resolution.txt (written with the defaults when missing).
    void ReadResolutionFile(WindowSize& docked, WindowSize& handheld)
    {
        int dockedWidth = 0;
        int dockedHeight = 0;
        int handheldWidth = 0;
        int handheldHeight = 0;

        FILE* file = fopen(RESOLUTION_FILE, "r");
        if (file == nullptr)
        {
            WriteDefaultResolutionFile();
        }
        else
        {
            char line[256];
            while (fgets(line, sizeof(line), file) != nullptr)
            {
                // "key = value", case and spaces ignored; '#', ';' and "//" start a comment.
                char text[256];
                size_t length = 0;
                for (const char* c = line; *c != '\0' && length + 1 < sizeof(text); c++)
                {
                    if (*c == '#' || *c == ';' || (c[0] == '/' && c[1] == '/'))
                        break;

                    if (!isspace(static_cast<unsigned char>(*c)))
                        text[length++] = char(tolower(static_cast<unsigned char>(*c)));
                }

                text[length] = '\0';
                char* equals = strchr(text, '=');
                if (equals == nullptr)
                    continue;

                *equals = '\0';
                int width = 0;
                int height = 0;
                ParseSize(equals + 1, width, height);
                if (height <= 0)
                    continue;

                if (strcmp(text, "docked") == 0 || strcmp(text, "dock") == 0 || strcmp(text, "tv") == 0)
                {
                    dockedWidth = width;
                    dockedHeight = height;
                }
                else if (strcmp(text, "handheld") == 0 || strcmp(text, "portable") == 0)
                {
                    handheldWidth = width;
                    handheldHeight = height;
                }
            }

            fclose(file);
        }

        docked = ClampWindowSize(dockedWidth, dockedHeight, DEFAULT_DOCKED_HEIGHT);
        handheld = ClampWindowSize(handheldWidth, handheldHeight, DEFAULT_HANDHELD_HEIGHT);
    }

    bool g_startedDocked = false;
    Event g_displayResolutionEvent{};
    bool g_displayResolutionEventValid = false;
    s32 g_lastDisplayHeight = 0; // the console's display height last seen (the change event need not clear itself)
}
#endif

int Window_OnSDLEvent(void*, SDL_Event* event)
{
    if (ImGui::GetIO().BackendPlatformUserData != nullptr)
        ImGui_ImplSDL2_ProcessEvent(event);

    for (auto listener : GetEventListeners())
    {
        if (listener->OnSDLEvent(event))
        {
            return 0;
        }
    }

    switch (event->type)
    {
        case SDL_QUIT:
        {
            if (App::s_isSaving)
                break;

            App::Exit();

            break;
        }

        case SDL_KEYDOWN:
        {
            switch (event->key.keysym.sym)
            {
                // Toggle fullscreen on ALT+ENTER.
                case SDLK_RETURN:
                {
                    if (!(event->key.keysym.mod & KMOD_ALT) || !m_isFullscreenKeyReleased)
                        break;

                    Config::Fullscreen = GameWindow::SetFullscreen(!GameWindow::IsFullscreen());

                    if (Config::Fullscreen)
                    {
                        Config::Monitor = GameWindow::GetDisplay();
                    }
                    else
                    {
                        Config::WindowState = GameWindow::SetMaximised(Config::WindowState == EWindowState::Maximised);
                    }

                    // Block holding ALT+ENTER spamming window changes.
                    m_isFullscreenKeyReleased = false;

                    break;
                }

                // Restore original window dimensions on F2.
                case SDLK_F2:
                    Config::Fullscreen = GameWindow::SetFullscreen(false);
                    GameWindow::ResetDimensions();
                    break;

                // Recentre window on F3.
                case SDLK_F3:
                {
                    if (GameWindow::IsFullscreen())
                        break;

                    GameWindow::SetDimensions(GameWindow::s_width, GameWindow::s_height);

                    break;
                }
            }

            break;
        }

        case SDL_KEYUP:
        {
            switch (event->key.keysym.sym)
            {
                // Allow user to input ALT+ENTER again.
                case SDLK_RETURN:
                    m_isFullscreenKeyReleased = true;
                    break;
            }
        }

        case SDL_WINDOWEVENT:
        {
            switch (event->window.event)
            {
                case SDL_WINDOWEVENT_FOCUS_LOST:
                    GameWindow::s_isFocused = false;
                    SDL_ShowCursor(SDL_ENABLE);
                    break;

                case SDL_WINDOWEVENT_FOCUS_GAINED:
                {
                    GameWindow::s_isFocused = true;

                    if (GameWindow::IsFullscreen())
                        SDL_ShowCursor(GameWindow::s_isFullscreenCursorVisible ? SDL_ENABLE : SDL_DISABLE);

                    break;
                }

                case SDL_WINDOWEVENT_RESTORED:
                    Config::WindowState = EWindowState::Normal;
                    break;

                case SDL_WINDOWEVENT_MAXIMIZED:
                    Config::WindowState = EWindowState::Maximised;
                    break;

                case SDL_WINDOWEVENT_RESIZED:
                    m_isResizing = true;
                    Config::WindowSize = -1;
                    GameWindow::s_width = event->window.data1;
                    GameWindow::s_height = event->window.data2;
                    GameWindow::SetTitle(fmt::format("{} - [{}x{}]", GameWindow::GetTitle(), GameWindow::s_width, GameWindow::s_height).c_str());
                    break;

                case SDL_WINDOWEVENT_MOVED:
                    GameWindow::s_x = event->window.data1;
                    GameWindow::s_y = event->window.data2;
                    break;
            }

            break;
        }

        case SDL_USER_PLAYER_CHAR:
            GameWindow::s_playerCharacter = static_cast<EPlayerCharacter>(event->user.code);
            GameWindow::SetIcon(GameWindow::s_playerCharacter);
            break;
    }

    return 0;
}

void GameWindow::Init(const char* sdlVideoDriver)
{
#if defined(__SWITCH__)
    (void)sdlVideoDriver;

    s_x = 0;
    s_y = 0;
    s_isFocused = true;
    s_isFullscreenCursorVisible = false;

    // The size of resolution.txt for the mode the console starts in (see above).
    WindowSize dockedSize{};
    WindowSize handheldSize{};
    ReadResolutionFile(dockedSize, handheldSize);
    g_startedDocked = QueryDocked();
    const WindowSize size = g_startedDocked ? dockedSize : handheldSize;
    s_width = size.width;
    s_height = size.height;
    fprintf(stderr, "Window: %dx%d (%s; resolution.txt: docked %dx%d, handheld %dx%d).\n", s_width, s_height,
        g_startedDocked ? "docked" : "handheld", dockedSize.width, dockedSize.height, handheldSize.width, handheldSize.height);
    g_displayResolutionEventValid = R_SUCCEEDED(appletGetDefaultDisplayResolutionChangeEvent(&g_displayResolutionEvent));
    {
        s32 displayWidth = 0;
        if (R_FAILED(appletGetDefaultDisplayResolution(&displayWidth, &g_lastDisplayHeight)))
            g_lastDisplayHeight = 0;
    }

    s_renderWindow = nwindowGetDefault();
    if (s_renderWindow != nullptr)
    {
        nwindowSetDimensions(s_renderWindow, s_width, s_height);
        nwindowSetCrop(s_renderWindow, 0, 0, s_width, s_height);
        nwindowSetSwapInterval(s_renderWindow, 1);

        uint32_t nativeWidth = 0;
        uint32_t nativeHeight = 0;
        if (nwindowGetDimensions(s_renderWindow, &nativeWidth, &nativeHeight) == 0)
        {
            s_width = static_cast<int>(nativeWidth);
            s_height = static_cast<int>(nativeHeight);
        }
    }

    return;
#else
#ifdef __linux__
    SDL_SetHint("SDL_APP_ID", "io.github.sonicnext_dev.marathonrecomp");
#endif

    if (SDL_VideoInit(sdlVideoDriver) != 0 && sdlVideoDriver)
    {
        LOGFN_ERROR("Failed to initialise the SDL video driver: \"{}\". Falling back to default.", sdlVideoDriver);
        SDL_VideoInit(nullptr);
    }

    auto videoDriverName = SDL_GetCurrentVideoDriver();

    if (videoDriverName)
        LOGFN("SDL video driver: \"{}\"", videoDriverName);

    SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
    SDL_AddEventWatch(Window_OnSDLEvent, s_pWindow);

#ifdef _WIN32
    SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
#endif

    s_x = Config::WindowX;
    s_y = Config::WindowY;
    s_width = Config::WindowWidth;
    s_height = Config::WindowHeight;

    if (s_x == -1 && s_y == -1)
        s_x = s_y = SDL_WINDOWPOS_CENTERED;

    if (!IsPositionValid())
        GameWindow::ResetDimensions();

    s_pWindow = SDL_CreateWindow("Marathon Recompiled", s_x, s_y, s_width, s_height, GetWindowFlags());

    if (IsFullscreen())
        SDL_ShowCursor(SDL_DISABLE);

    SetDisplay(Config::Monitor);
    SetIcon();
    SetTitle();

    SDL_SetWindowMinimumSize(s_pWindow, MIN_WIDTH, MIN_HEIGHT);

    SDL_SysWMinfo info;
    SDL_VERSION(&info.version);
    SDL_GetWindowWMInfo(s_pWindow, &info);

#if defined(_WIN32)
    s_renderWindow = info.info.win.window;

    if (Config::DisableDWMRoundedCorners)
    {
        DWM_WINDOW_CORNER_PREFERENCE wcp = DWMWCP_DONOTROUND;
        DwmSetWindowAttribute(s_renderWindow, DWMWA_WINDOW_CORNER_PREFERENCE, &wcp, sizeof(wcp));
    }
#elif defined(PLUME_SDL_VULKAN_ENABLED)
    s_renderWindow = s_pWindow;
#elif defined(__linux__)
    s_renderWindow = { info.info.x11.display, info.info.x11.window };
#elif defined(__APPLE__)
    s_renderWindow.window = info.info.cocoa.window;
    s_renderWindow.view = SDL_Metal_GetLayer(SDL_Metal_CreateView(s_pWindow));
#else
    static_assert(false, "Unknown platform.");
#endif

    SetTitleBarColour();

    SDL_ShowWindow(s_pWindow);
#endif
}

void GameWindow::Update()
{
#if defined(__SWITCH__)
    // Docked or undocked while playing: the window keeps its size (see resolution.txt above); said once per change.
    if (g_displayResolutionEventValid && R_SUCCEEDED(eventWait(&g_displayResolutionEvent, 0)))
    {
        s32 displayWidth = 0;
        s32 displayHeight = 0;
        if (R_SUCCEEDED(appletGetDefaultDisplayResolution(&displayWidth, &displayHeight)) && displayHeight != g_lastDisplayHeight)
        {
            g_lastDisplayHeight = displayHeight;
            fprintf(stderr, "Window: the console now shows %dx%d (%s); the game keeps %dx%d until it is started again.\n",
                int(displayWidth), int(displayHeight), displayHeight >= 1080 ? "docked" : "handheld", s_width, s_height);
        }
    }

    uint32_t nativeWidth = 0;
    uint32_t nativeHeight = 0;
    if (s_renderWindow != nullptr && nwindowGetDimensions(s_renderWindow, &nativeWidth, &nativeHeight) == 0)
    {
        s_width = static_cast<int>(nativeWidth);
        s_height = static_cast<int>(nativeHeight);
    }

    if (g_needsResize)
        s_isChangingDisplay = false;

    return;
#endif
    if (!GameWindow::IsFullscreen() && !GameWindow::IsMaximised() && !s_isChangingDisplay)
    {
        Config::WindowX = GameWindow::s_x;
        Config::WindowY = GameWindow::s_y;
        Config::WindowWidth = GameWindow::s_width;
        Config::WindowHeight = GameWindow::s_height;
    }

    if (m_isResizing)
    {
        SetTitle();
        m_isResizing = false;
    }

    if (g_needsResize)
        s_isChangingDisplay = false;
}

SDL_Surface* GameWindow::GetIconSurface(void* pIconBmp, size_t iconSize)
{
#if defined(__SWITCH__)
    (void)pIconBmp;
    (void)iconSize;
    return nullptr;
#else
    auto rw = SDL_RWFromMem(pIconBmp, iconSize);
    auto surface = SDL_LoadBMP_RW(rw, 1);

    if (!surface)
        LOGF_ERROR("Failed to load icon: {}", SDL_GetError());

    return surface;
#endif
}

void GameWindow::SetIcon(void* pIconBmp, size_t iconSize)
{
#if defined(__SWITCH__)
    (void)pIconBmp;
    (void)iconSize;
#else
    if (auto icon = GetIconSurface(pIconBmp, iconSize))
    {
        SDL_SetWindowIcon(s_pWindow, icon);
        SDL_FreeSurface(icon);
    }
#endif
}

void GameWindow::SetIcon(EPlayerCharacter player)
{
    // TODO: Per-character icons
    switch (player) {
        case EPlayerCharacter::Sonic:
            break;
        case EPlayerCharacter::Shadow:
            break;
        case EPlayerCharacter::Silver:
            break;
        case EPlayerCharacter::Blaze:
            break;
        case EPlayerCharacter::Amy:
            break;
        case EPlayerCharacter::Tails:
            break;
        case EPlayerCharacter::Rouge:
            break;
        case EPlayerCharacter::Knuckles:
            break;
    }

    SetIcon(g_game_icon, sizeof(g_game_icon));
}

const char* GameWindow::GetTitle()
{
    if (Config::UseOfficialTitleOnTitleBar)
    {
        return "SONIC THE HEDGEHOG";
    }

    return "Marathon Recompiled";
}

void GameWindow::SetTitle(const char* title)
{
#if defined(__SWITCH__)
    (void)title;
#else
    SDL_SetWindowTitle(s_pWindow, title ? title : GetTitle());
#endif
}

void GameWindow::SetTitleBarColour()
{
#if _WIN32
    if (os::user::IsDarkTheme())
    {
        auto version = os::version::GetOSVersion();

        if (version.Major < 10 || version.Build <= 17763)
            return;

        auto flag = version.Build >= 18985
            ? DWMWA_USE_IMMERSIVE_DARK_MODE
            : 19; // DWMWA_USE_IMMERSIVE_DARK_MODE_BEFORE_20H1

        const DWORD useImmersiveDarkMode = 1;
        DwmSetWindowAttribute(s_renderWindow, flag, &useImmersiveDarkMode, sizeof(useImmersiveDarkMode));
    }
#endif
}

bool GameWindow::IsFullscreen()
{
#if defined(__SWITCH__)
    return true;
#else
    return SDL_GetWindowFlags(s_pWindow) & SDL_WINDOW_FULLSCREEN_DESKTOP;
#endif
}

bool GameWindow::SetFullscreen(bool isEnabled)
{
#if defined(__SWITCH__)
    return true;
#else
    if (isEnabled)
    {
        SDL_SetWindowFullscreen(s_pWindow, SDL_WINDOW_FULLSCREEN_DESKTOP);
        SDL_ShowCursor(s_isFullscreenCursorVisible ? SDL_ENABLE : SDL_DISABLE);
    }
    else
    {
        SDL_SetWindowFullscreen(s_pWindow, 0);
        SDL_ShowCursor(SDL_ENABLE);

        SetIcon(GameWindow::s_playerCharacter);
        SetDimensions(Config::WindowWidth, Config::WindowHeight, Config::WindowX, Config::WindowY);
    }

    return isEnabled;
#endif
}
    
void GameWindow::SetFullscreenCursorVisibility(bool isVisible)
{
    s_isFullscreenCursorVisible = isVisible;

#if !defined(__SWITCH__)
    if (IsFullscreen())
    {
        SDL_ShowCursor(s_isFullscreenCursorVisible ? SDL_ENABLE : SDL_DISABLE);
    }
    else
    {
        SDL_ShowCursor(SDL_ENABLE);
    }
#endif
}

bool GameWindow::IsMaximised()
{
#if defined(__SWITCH__)
    return false;
#else
    return SDL_GetWindowFlags(s_pWindow) & SDL_WINDOW_MAXIMIZED;
#endif
}

EWindowState GameWindow::SetMaximised(bool isEnabled)
{
#if defined(__SWITCH__)
    (void)isEnabled;
    return EWindowState::Normal;
#else
    if (isEnabled)
    {
        SDL_MaximizeWindow(s_pWindow);
    }
    else
    {
        SDL_RestoreWindow(s_pWindow);
    }

    return isEnabled
        ? EWindowState::Maximised
        : EWindowState::Normal;
#endif
}

SDL_Rect GameWindow::GetDimensions()
{
    SDL_Rect rect{};

#if defined(__SWITCH__)
    rect.x = 0;
    rect.y = 0;
    rect.w = s_width;
    rect.h = s_height;
#else
    SDL_GetWindowPosition(s_pWindow, &rect.x, &rect.y);
    SDL_GetWindowSize(s_pWindow, &rect.w, &rect.h);
#endif

    return rect;
}

void GameWindow::GetSizeInPixels(int *w, int *h)
{
#if defined(__SWITCH__)
    *w = s_width;
    *h = s_height;
#else
    SDL_GetWindowSizeInPixels(s_pWindow, w, h);
#endif
}

void GameWindow::SetDimensions(int w, int h, int x, int y)
{
    s_width = w;
    s_height = h;
    s_x = x;
    s_y = y;

#if defined(__SWITCH__)
    if (s_renderWindow != nullptr)
    {
        nwindowSetDimensions(s_renderWindow, w, h);
        nwindowSetCrop(s_renderWindow, 0, 0, w, h);
    }
#else
    SDL_SetWindowSize(s_pWindow, w, h);
    SDL_ResizeEvent(s_pWindow, w, h);

    SDL_SetWindowPosition(s_pWindow, x, y);
    SDL_MoveEvent(s_pWindow, x, y);
#endif
}

void GameWindow::ResetDimensions()
{
    s_x = SDL_WINDOWPOS_CENTERED;
    s_y = SDL_WINDOWPOS_CENTERED;
    s_width = DEFAULT_WIDTH;
    s_height = DEFAULT_HEIGHT;

    Config::WindowX = s_x;
    Config::WindowY = s_y;
    Config::WindowWidth = s_width;
    Config::WindowHeight = s_height;
}

uint32_t GameWindow::GetWindowFlags()
{
#if defined(__SWITCH__)
    return 0;
#else
    uint32_t flags = SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;

    if (Config::WindowState == EWindowState::Maximised)
        flags |= SDL_WINDOW_MAXIMIZED;

    if (Config::Fullscreen)
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

#ifdef PLUME_SDL_VULKAN_ENABLED
    flags |= SDL_WINDOW_VULKAN;
#endif

    return flags;
#endif
}

int GameWindow::GetDisplayCount()
{
#if defined(__SWITCH__)
    return 1;
#else
    auto result = SDL_GetNumVideoDisplays();

    if (result < 0)
    {
        LOGF_ERROR("Failed to get display count: {}", SDL_GetError());
        return 1;
    }

    return result;
#endif
}

int GameWindow::GetDisplay()
{
#if defined(__SWITCH__)
    return 0;
#else
    return SDL_GetWindowDisplayIndex(s_pWindow);
#endif
}

void GameWindow::SetDisplay(int displayIndex)
{
#if defined(__SWITCH__)
    (void)displayIndex;
    return;
#else
    if (!IsFullscreen())
        return;

    if (GetDisplay() == displayIndex)
        return;

    s_isChangingDisplay = true;

    SDL_Rect bounds;

    if (SDL_GetDisplayBounds(displayIndex, &bounds) == 0)
    {
        SetFullscreen(false);
        SetDimensions(bounds.w, bounds.h, bounds.x, bounds.y);
        SetFullscreen(true);
    }
    else
    {
        ResetDimensions();
    }
#endif
}

std::vector<SDL_DisplayMode> GameWindow::GetDisplayModes(bool ignoreInvalidModes, bool ignoreRefreshRates)
{
    auto result = std::vector<SDL_DisplayMode>();

#if defined(__SWITCH__)
    (void)ignoreInvalidModes;
    (void)ignoreRefreshRates;
    SDL_DisplayMode mode{};
    mode.w = s_width;
    mode.h = s_height;
    mode.refresh_rate = 60;
    result.push_back(mode);
    return result;
#else
    auto uniqueResolutions = std::set<std::pair<int, int>>();
    auto displayIndex = GetDisplay();
    auto modeCount = SDL_GetNumDisplayModes(displayIndex);

    if (modeCount <= 0)
        return result;

    for (int i = modeCount - 1; i >= 0; i--)
    {
        SDL_DisplayMode mode;

        if (SDL_GetDisplayMode(displayIndex, i, &mode) == 0)
        {
            if (ignoreInvalidModes)
            {
                if (mode.w < MIN_WIDTH || mode.h < MIN_HEIGHT)
                    continue;

                SDL_DisplayMode desktopMode;

                if (SDL_GetDesktopDisplayMode(displayIndex, &desktopMode) == 0)
                {
                    if (mode.w >= desktopMode.w || mode.h >= desktopMode.h)
                        continue;
                }
            }

            if (ignoreRefreshRates)
            {
                auto res = std::make_pair(mode.w, mode.h);

                if (uniqueResolutions.find(res) == uniqueResolutions.end())
                {
                    uniqueResolutions.insert(res);
                    result.push_back(mode);
                }
            }
            else
            {
                result.push_back(mode);
            }
        }
    }

    return result;
#endif
}

int GameWindow::FindNearestDisplayMode()
{
    auto result = -1;
    auto displayModes = GetDisplayModes();
    auto currentDiff = std::numeric_limits<int>::max();

    for (int i = 0; i < displayModes.size(); i++)
    {
        auto& mode = displayModes[i];

        auto widthDiff = abs(mode.w - s_width);
        auto heightDiff = abs(mode.h - s_height);
        auto totalDiff = widthDiff + heightDiff;

        if (totalDiff < currentDiff)
        {
            currentDiff = totalDiff;
            result = i;
        }
    }

    return result;
}

bool GameWindow::IsPositionValid()
{
#if defined(__SWITCH__)
    return true;
#else
    auto displayCount = GetDisplayCount();

    for (int i = 0; i < displayCount; i++)
    {
        SDL_Rect bounds;

        if (SDL_GetDisplayBounds(i, &bounds) == 0)
        {
            auto x = s_x;
            auto y = s_y;

            // Window spans across the entire display in windowed mode, which is invalid.
            if (!Config::Fullscreen && s_width == bounds.w && s_height == bounds.h)
                return false;

            if (x == SDL_WINDOWPOS_CENTERED_DISPLAY(i))
                x = bounds.w / 2 - s_width / 2;

            if (y == SDL_WINDOWPOS_CENTERED_DISPLAY(i))
                y = bounds.h / 2 - s_height / 2;

            if (x >= bounds.x && x < bounds.x + bounds.w &&
                y >= bounds.y && y < bounds.y + bounds.h)
            {
                return true;
            }
        }
    }

    return false;
#endif
}
