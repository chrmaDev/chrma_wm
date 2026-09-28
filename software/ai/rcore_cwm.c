/**********************************************************************************************
*
*   rcore_cwm - raylib 6 memory backend adapted for chrmaWM
*
*   PLATFORM: MEMORY (chrmaWM client)
*       - Software framebuffer output into a CWM shared window buffer
*
*   LIMITATIONS:
*       - Software renderer (rlsw)
*       - Input and resize depend on the CWM protocol
*
*   POSSIBLE IMPROVEMENTS:
*       - Improvement 01
*       - Improvement 02
*
*   CONFIGURATION:
*       #define RCORE_PLATFORM_CUSTOM_FLAG
*           Custom flag for rcore on target platform -not used-
*
*   DEPENDENCIES:
*       - rlsw: Software renderer
*       - gestures: Gestures system for touch-ready devices (or simulated from mouse inputs)
*
*
*   LICENSE: zlib/libpng
*
*   Copyright (c) 2025-2026 Ramon Santamaria (@raysan5) and contributors
*
*   This software is provided "as-is", without any express or implied warranty. In no event
*   will the authors be held liable for any damages arising from the use of this software.
*
*   Permission is granted to anyone to use this software for any purpose, including commercial
*   applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*     1. The origin of this software must not be misrepresented; you must not claim that you
*     wrote the original software. If you use this software in a product, an acknowledgment
*     in the product documentation would be appreciated but is not required.
*
*     2. Altered source versions must be plainly marked as such, and must not be misrepresented
*     as being the original software.
*
*     3. This notice may not be removed or altered from any source distribution.
*
**********************************************************************************************/

#include "cwm_client.h"
#include "cwm_raylib_input.h"
#include <xkbcommon/xkbcommon.h>
#include <xkbcommon/xkbcommon-keysyms.h>

//----------------------------------------------------------------------------------
// Types and Structures Definition
//----------------------------------------------------------------------------------
// Platform-specific required data for timing (Win32)
#if defined(_WIN32)
typedef struct _LARGE_INTEGER { int64_t QuadPart; } LARGE_INTEGER;
__declspec(dllimport) int __stdcall QueryPerformanceCounter(LARGE_INTEGER *lpPerformanceCount);
__declspec(dllimport) int __stdcall QueryPerformanceFrequency(LARGE_INTEGER *lpFrequency);
#endif

typedef struct {
    int windowId;
    struct xkb_context *xkbContext;
    struct xkb_keymap *xkbKeymap;
    struct xkb_state *xkbState;
    int heldKeys[512];
    cwm_raylib_key_event keyEvents[128];
    int keyEventCount;
    int keyEventRead;
#if defined(_WIN32)
    LARGE_INTEGER timerFrequency;
#endif
} PlatformData;

//----------------------------------------------------------------------------------
// Global Variables Definition
//----------------------------------------------------------------------------------
extern CoreData CORE;                   // Global CORE state context

static PlatformData platform = { 0 };   // Platform specific data

int cwm_raylib_next_key_event(cwm_raylib_key_event *event)
{
    if (!event || platform.keyEventRead >= platform.keyEventCount) return 0;
    *event = platform.keyEvents[platform.keyEventRead++];
    return 1;
}

//----------------------------------------------------------------------------------
// Module Internal Functions Declaration
//----------------------------------------------------------------------------------
int InitPlatform(void);                 // Initialize platform (graphics, inputs and more)

//----------------------------------------------------------------------------------
// Module Functions Declaration
//----------------------------------------------------------------------------------
// NOTE: Functions declaration is provided by raylib.h

//----------------------------------------------------------------------------------
// Module Functions Definition: Window and Graphics Device
//----------------------------------------------------------------------------------

// Check if application should close
bool WindowShouldClose(void)
{
    if (CORE.Window.ready) return CORE.Window.shouldClose;
    else return true;
}

// Toggle fullscreen mode
void ToggleFullscreen(void)
{
    cwm_request_set_state(platform.windowId,
        (CORE.Window.flags & FLAG_FULLSCREEN_MODE) ? CWM_WINDOW_RESTORE : CWM_WINDOW_FULLSCREEN);
}

// Toggle borderless windowed mode
void ToggleBorderlessWindowed(void)
{
    TRACELOG(LOG_WARNING, "ToggleBorderlessWindowed() not available on target platform");
}

// Set window state: maximized, if resizable
void MaximizeWindow(void)
{
    cwm_request_set_state(platform.windowId, CWM_WINDOW_MAXIMIZE);
}

// Set window state: minimized
void MinimizeWindow(void)
{
    cwm_request_set_state(platform.windowId, CWM_WINDOW_MINIMIZE);
}

// Restore window from being minimized/maximized
void RestoreWindow(void)
{
    cwm_request_set_state(platform.windowId, CWM_WINDOW_RESTORE);
}

// Set window configuration state using flags
void SetWindowState(unsigned int flags)
{
    if (flags & FLAG_FULLSCREEN_MODE) cwm_request_set_state(platform.windowId, CWM_WINDOW_FULLSCREEN);
    else if (flags & FLAG_WINDOW_MAXIMIZED) MaximizeWindow();
    else if (flags & FLAG_WINDOW_MINIMIZED) MinimizeWindow();
}

// Clear window configuration state flags
void ClearWindowState(unsigned int flags)
{
    if (flags & (FLAG_FULLSCREEN_MODE | FLAG_WINDOW_MAXIMIZED | FLAG_WINDOW_MINIMIZED))
        RestoreWindow();
}

// Set icon for window
void SetWindowIcon(Image image)
{
    TRACELOG(LOG_WARNING, "SetWindowIcon() not available on target platform");
}

// Set icon for window
void SetWindowIcons(Image *images, int count)
{
    TRACELOG(LOG_WARNING, "SetWindowIcons() not available on target platform");
}

// Set title for window
void SetWindowTitle(const char *title)
{
    CORE.Window.title = title;
    cwm_request_set_title(platform.windowId, title);
}

// Set window position on screen (windowed mode)
void SetWindowPosition(int x, int y)
{
    TRACELOG(LOG_WARNING, "SetWindowPosition() not available on target platform");
}

// Set monitor for the current window
void SetWindowMonitor(int monitor)
{
    TRACELOG(LOG_WARNING, "SetWindowMonitor() not available on target platform");
}

// Set window minimum dimensions (FLAG_WINDOW_RESIZABLE)
void SetWindowMinSize(int width, int height)
{
    CORE.Window.screenMin.width = width;
    CORE.Window.screenMin.height = height;
}

// Set window maximum dimensions (FLAG_WINDOW_RESIZABLE)
void SetWindowMaxSize(int width, int height)
{
    CORE.Window.screenMax.width = width;
    CORE.Window.screenMax.height = height;
}

// Set window dimensions
void SetWindowSize(int width, int height)
{
    cwm_request_set_size(platform.windowId, width, height);
}

// Set window opacity, value opacity is between 0.0 and 1.0
void SetWindowOpacity(float opacity)
{
    TRACELOG(LOG_WARNING, "SetWindowOpacity() not available on target platform");
}

// Set window focused
void SetWindowFocused(void)
{
    TRACELOG(LOG_WARNING, "SetWindowFocused() not available on target platform");
}

// Get native window handle
void *GetWindowHandle(void)
{
    TRACELOG(LOG_WARNING, "GetWindowHandle() not implemented on target platform");
    return NULL;
}

// Get number of monitors
int GetMonitorCount(void)
{
    TRACELOG(LOG_WARNING, "GetMonitorCount() not implemented on target platform");
    return 1;
}

// Get current monitor where window is placed
int GetCurrentMonitor(void)
{
    TRACELOG(LOG_WARNING, "GetCurrentMonitor() not implemented on target platform");
    return 0;
}

// Get selected monitor position
Vector2 GetMonitorPosition(int monitor)
{
    TRACELOG(LOG_WARNING, "GetMonitorPosition() not implemented on target platform");
    return (Vector2){ 0, 0 };
}

// Get selected monitor width (currently used by monitor)
int GetMonitorWidth(int monitor)
{
    TRACELOG(LOG_WARNING, "GetMonitorWidth() not implemented on target platform");
    return 0;
}

// Get selected monitor height (currently used by monitor)
int GetMonitorHeight(int monitor)
{
    TRACELOG(LOG_WARNING, "GetMonitorHeight() not implemented on target platform");
    return 0;
}

// Get selected monitor physical width in millimetres
int GetMonitorPhysicalWidth(int monitor)
{
    TRACELOG(LOG_WARNING, "GetMonitorPhysicalWidth() not implemented on target platform");
    return 0;
}

// Get selected monitor physical height in millimetres
int GetMonitorPhysicalHeight(int monitor)
{
    TRACELOG(LOG_WARNING, "GetMonitorPhysicalHeight() not implemented on target platform");
    return 0;
}

// Get selected monitor refresh rate
int GetMonitorRefreshRate(int monitor)
{
    TRACELOG(LOG_WARNING, "GetMonitorRefreshRate() not implemented on target platform");
    return 0;
}

// Get the human-readable, UTF-8 encoded name of the selected monitor
const char *GetMonitorName(int monitor)
{
    TRACELOG(LOG_WARNING, "GetMonitorName() not implemented on target platform");
    return "";
}

// Get window position XY on monitor
Vector2 GetWindowPosition(void)
{
    TRACELOG(LOG_WARNING, "GetWindowPosition() not implemented on target platform");
    return (Vector2){ 0, 0 };
}

// Get window scale DPI factor for current monitor
Vector2 GetWindowScaleDPI(void)
{
    TRACELOG(LOG_WARNING, "GetWindowScaleDPI() not implemented on target platform");
    return (Vector2){ 1.0f, 1.0f };
}

// Set clipboard text content
void SetClipboardText(const char *text)
{
    cwm_client_set_clipboard(text);
}

// Get clipboard text content
// NOTE: returned string is allocated and freed by GLFW
const char *GetClipboardText(void)
{
    return cwm_client_get_clipboard();
}

// Get clipboard image
Image GetClipboardImage(void)
{
    Image image = { 0 };

    TRACELOG(LOG_WARNING, "GetClipboardImage() not implemented on target platform");

    return image;
}

// Show mouse cursor
void ShowCursor(void)
{
    CORE.Input.Mouse.cursorHidden = false;
}

// Hides mouse cursor
void HideCursor(void)
{
    CORE.Input.Mouse.cursorHidden = true;
}

// Enables cursor (unlock cursor)
void EnableCursor(void)
{
    // Set cursor position in the middle
    SetMousePosition(CORE.Window.screen.width/2, CORE.Window.screen.height/2);

    CORE.Input.Mouse.cursorHidden = false;
}

// Disables cursor (lock cursor)
void DisableCursor(void)
{
    // Set cursor position in the middle
    SetMousePosition(CORE.Window.screen.width/2, CORE.Window.screen.height/2);

    CORE.Input.Mouse.cursorHidden = true;
}

// Swap back buffer with front buffer (screen drawing)
void SwapScreenBuffer(void)
{
    int width = cwm_client_width();
    int height = cwm_client_height();
    void *dst = cwm_client_pixels();

    if (!dst || width <= 0 || height <= 0) return;
    // rlsw's full-frame readback is top-down BGRA, matching CWM's packed buffer.
    rlCopyFramebuffer(0, 0, width, height, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8, dst);
    if (cwm_request_commit_window(platform.windowId) < 0) CORE.Window.shouldClose = true;
}

//----------------------------------------------------------------------------------
// Module Functions Definition: Misc
//----------------------------------------------------------------------------------

// Get elapsed time measure in seconds since InitTimer()
double GetTime(void)
{
    double time = 0.0;
#if defined(_WIN32)
    LARGE_INTEGER now = { 0 };
    QueryPerformanceCounter(&now);
    return (double)(now.QuadPart - CORE.Time.base)/(double)platform.timerFrequency.QuadPart;
#elif defined(__linux__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__EMSCRIPTEN__)
    struct timespec ts = { 0 };
    clock_gettime(CLOCK_MONOTONIC, &ts);
    unsigned long long int nanoSeconds = (unsigned long long int)ts.tv_sec*1000000000LLU + (unsigned long long int)ts.tv_nsec;
    time = (double)(nanoSeconds - CORE.Time.base)*1e-9;  // Elapsed time since InitTimer()
#endif
    return time;
}

// Open URL with default system browser (if available)
// NOTE: This function is only safe to use if you control the URL given.
// A user could craft a malicious string performing another action.
// Only call this function yourself not with user input or make sure to check the string yourself.
// REF: https://github.com/raysan5/raylib/issues/686
void OpenURL(const char *url)
{
    // Security check to (partially) avoid malicious code on target platform
    if (strchr(url, '\'') != NULL) TRACELOG(LOG_WARNING, "SYSTEM: Provided URL could be potentially malicious, avoid [\'] character");
    else
    {
        char *cmd = (char *)RL_CALLOC(strlen(url) + 32, sizeof(char));
        sprintf(cmd, "explorer \"%s\"", url);
        int result = system(cmd);
        if (result == -1) TRACELOG(LOG_WARNING, "OpenURL() child process could not be created");
        RL_FREE(cmd);
    }
}

//----------------------------------------------------------------------------------
// Module Functions Definition: Inputs
//----------------------------------------------------------------------------------

// Set internal gamepad mappings
int SetGamepadMappings(const char *mappings)
{
    TRACELOG(LOG_WARNING, "SetGamepadMappings() not implemented on target platform");
    return 0;
}

// Set gamepad vibration
void SetGamepadVibration(int gamepad, float leftMotor, float rightMotor, float duration)
{
    TRACELOG(LOG_WARNING, "SetGamepadVibration() not implemented on target platform");
}

// Set mouse position XY
void SetMousePosition(int x, int y)
{
    CORE.Input.Mouse.currentPosition = (Vector2){ (float)x, (float)y };
    CORE.Input.Mouse.previousPosition = CORE.Input.Mouse.currentPosition;
}

// Set mouse cursor
void SetMouseCursor(int cursor)
{
    TRACELOG(LOG_WARNING, "SetMouseCursor() not implemented on target platform");
}

// Get physical key name
const char *GetKeyName(int key)
{
    TRACELOG(LOG_WARNING, "GetKeyName() not implemented on target platform");
    return "";
}

// Register all input events
static int RaylibKey(xkb_keysym_t sym)
{
    if (sym >= XKB_KEY_a && sym <= XKB_KEY_z) return KEY_A + (int)(sym - XKB_KEY_a);
    if (sym >= XKB_KEY_0 && sym <= XKB_KEY_9) return KEY_ZERO + (int)(sym - XKB_KEY_0);
    if (sym >= XKB_KEY_F1 && sym <= XKB_KEY_F12) return KEY_F1 + (int)(sym - XKB_KEY_F1);
    if (sym >= XKB_KEY_KP_0 && sym <= XKB_KEY_KP_9) return KEY_KP_0 + (int)(sym - XKB_KEY_KP_0);
    switch (sym)
    {
        case XKB_KEY_space: return KEY_SPACE;
        case XKB_KEY_Escape: return KEY_ESCAPE;
        case XKB_KEY_Return: return KEY_ENTER;
        case XKB_KEY_KP_Enter: return KEY_KP_ENTER;
        case XKB_KEY_Tab: return KEY_TAB;
        case XKB_KEY_BackSpace: return KEY_BACKSPACE;
        case XKB_KEY_Insert: return KEY_INSERT;
        case XKB_KEY_Delete: return KEY_DELETE;
        case XKB_KEY_Right: return KEY_RIGHT;
        case XKB_KEY_Left: return KEY_LEFT;
        case XKB_KEY_Down: return KEY_DOWN;
        case XKB_KEY_Up: return KEY_UP;
        case XKB_KEY_Page_Up: return KEY_PAGE_UP;
        case XKB_KEY_Page_Down: return KEY_PAGE_DOWN;
        case XKB_KEY_Home: return KEY_HOME;
        case XKB_KEY_End: return KEY_END;
        case XKB_KEY_Shift_L: return KEY_LEFT_SHIFT;
        case XKB_KEY_Shift_R: return KEY_RIGHT_SHIFT;
        case XKB_KEY_Control_L: return KEY_LEFT_CONTROL;
        case XKB_KEY_Control_R: return KEY_RIGHT_CONTROL;
        case XKB_KEY_Alt_L: return KEY_LEFT_ALT;
        case XKB_KEY_Alt_R: return KEY_RIGHT_ALT;
        case XKB_KEY_Super_L: return KEY_LEFT_SUPER;
        case XKB_KEY_Super_R: return KEY_RIGHT_SUPER;
        case XKB_KEY_Caps_Lock: return KEY_CAPS_LOCK;
        case XKB_KEY_apostrophe: return KEY_APOSTROPHE;
        case XKB_KEY_comma: return KEY_COMMA;
        case XKB_KEY_minus: return KEY_MINUS;
        case XKB_KEY_period: return KEY_PERIOD;
        case XKB_KEY_slash: return KEY_SLASH;
        case XKB_KEY_semicolon: return KEY_SEMICOLON;
        case XKB_KEY_equal: return KEY_EQUAL;
        case XKB_KEY_bracketleft: return KEY_LEFT_BRACKET;
        case XKB_KEY_backslash: return KEY_BACKSLASH;
        case XKB_KEY_bracketright: return KEY_RIGHT_BRACKET;
        case XKB_KEY_grave: return KEY_GRAVE;
        default: return KEY_NULL;
    }
}

void PollInputEvents(void)
{
    struct input_event event;
    int result;
#if SUPPORT_GESTURES_SYSTEM
    // NOTE: Gestures update must be called every frame to reset gestures correctly
    // because ProcessGestureEvent() is called on an event, not every frame
    UpdateGestures();
#endif

    // Reset keys/chars pressed registered
    CORE.Input.Keyboard.keyPressedQueueCount = 0;
    CORE.Input.Keyboard.charPressedQueueCount = 0;
    platform.keyEventCount = platform.keyEventRead = 0;

    // Reset key repeats
    for (int i = 0; i < MAX_KEYBOARD_KEYS; i++) CORE.Input.Keyboard.keyRepeatInFrame[i] = 0;

    // Reset last gamepad button/axis registered state
    CORE.Input.Gamepad.lastButtonPressed = 0; // GAMEPAD_BUTTON_UNKNOWN
    //CORE.Input.Gamepad.axisCount = 0;

    // Register previous touch states
    for (int i = 0; i < MAX_TOUCH_POINTS; i++) CORE.Input.Touch.previousTouchState[i] = CORE.Input.Touch.currentTouchState[i];

    // Reset touch positions
    // TODO: It resets on target platform the mouse position and not filled again until a move-event,
    // so, if mouse is not moved it returns a (0, 0) position... this behaviour should be reviewed!
    //for (int i = 0; i < MAX_TOUCH_POINTS; i++) CORE.Input.Touch.position[i] = (Vector2){ 0, 0 };

    // Register previous keys states
    // NOTE: Android supports up to 260 keys
    for (int i = 0; i < MAX_KEYBOARD_KEYS; i++)
    {
        CORE.Input.Keyboard.previousKeyState[i] = CORE.Input.Keyboard.currentKeyState[i];
        CORE.Input.Keyboard.keyRepeatInFrame[i] = 0;
    }

    CORE.Window.resizedLastFrame = false;
    CORE.Input.Mouse.previousPosition = CORE.Input.Mouse.currentPosition;
    CORE.Input.Mouse.currentWheelMove = (Vector2){0};
    for (int i = 0; i < MAX_MOUSE_BUTTONS; i++)
        CORE.Input.Mouse.previousButtonState[i] = CORE.Input.Mouse.currentButtonState[i];

    while ((result = cwm_client_poll_event(&event)) > 0)
    {
        if (event.type == CWM_PRT_TYPE_CLOSE_WINDOW) CORE.Window.shouldClose = true;
        else if (event.type == CWM_INPUT_EVENT_TYPE_WINDOW_STATE)
        {
            CORE.Window.flags &= ~(FLAG_WINDOW_MINIMIZED | FLAG_WINDOW_MAXIMIZED | FLAG_FULLSCREEN_MODE);
            if (event.keycode == CWM_WINDOW_MINIMIZE) CORE.Window.flags |= FLAG_WINDOW_MINIMIZED;
            if (event.keycode == CWM_WINDOW_MAXIMIZE) CORE.Window.flags |= FLAG_WINDOW_MAXIMIZED;
            if (event.keycode == CWM_WINDOW_FULLSCREEN) CORE.Window.flags |= FLAG_FULLSCREEN_MODE;
        }
        else if (event.type == CWM_INPUT_EVENT_TYPE_FOCUS)
        {
            if (event.down) CORE.Window.flags &= ~FLAG_WINDOW_UNFOCUSED;
            else
            {
                CORE.Window.flags |= FLAG_WINDOW_UNFOCUSED;
                memset(CORE.Input.Keyboard.currentKeyState, 0, sizeof(CORE.Input.Keyboard.currentKeyState));
                memset(CORE.Input.Keyboard.previousKeyState, 0, sizeof(CORE.Input.Keyboard.previousKeyState));
                memset(platform.heldKeys, 0, sizeof(platform.heldKeys));
                memset(CORE.Input.Mouse.currentButtonState, 0, sizeof(CORE.Input.Mouse.currentButtonState));
                platform.keyEventCount = platform.keyEventRead = 0;
                CORE.Input.Keyboard.keyPressedQueueCount = 0;
                CORE.Input.Keyboard.charPressedQueueCount = 0;
                if (platform.xkbState) xkb_state_unref(platform.xkbState);
                platform.xkbState = platform.xkbKeymap ? xkb_state_new(platform.xkbKeymap) : NULL;
            }
        }
        else if (event.type == CWM_INPUT_EVENT_TYPE_RESIZE)
        {
            int width = cwm_client_width();
            int height = cwm_client_height();
            CORE.Window.screen.width = CORE.Window.render.width = CORE.Window.currentFbo.width = width;
            CORE.Window.screen.height = CORE.Window.render.height = CORE.Window.currentFbo.height = height;
            CORE.Window.resizedLastFrame = true;
            rlResizeFramebuffer(width, height);
            SetupViewport(width, height);
        }
        else if (event.type == CWM_INPUT_EVENT_TYPE_MOUSE || event.type == CWM_INPUT_EVENT_TYPE_MOUSE_MOVE)
        {
            CORE.Input.Mouse.currentPosition = (Vector2){ (float)event.x, (float)event.y };
            if (event.type == CWM_INPUT_EVENT_TYPE_MOUSE)
                CORE.Input.Mouse.currentButtonState[event.button] = event.down;
        }
        else if (event.type == CWM_INPUT_EVENT_TYPE_MOUSE_WHEEL)
        {
            if (event.button == 1) CORE.Input.Mouse.currentWheelMove.x += (float)(int32_t)event.keycode;
            else CORE.Input.Mouse.currentWheelMove.y += (float)(int32_t)event.keycode;
        }
        else if (event.type == CWM_INPUT_EVENT_TYPE_KEYBOARD && platform.xkbState && event.keycode < 512)
        {
            int key = platform.heldKeys[event.keycode];
            if (event.down)
            {
                const xkb_keysym_t *syms = NULL;
                if (xkb_keymap_key_get_syms_by_level(platform.xkbKeymap, event.keycode, 0, 0, &syms) > 0)
                    key = RaylibKey(syms[0]);
                platform.heldKeys[event.keycode] = key;
            }
            xkb_state_update_key(platform.xkbState, event.keycode, event.down ? XKB_KEY_DOWN : XKB_KEY_UP);
            if (key > KEY_NULL && key < MAX_KEYBOARD_KEYS)
            {
                if (event.down && !CORE.Input.Keyboard.currentKeyState[key] &&
                    CORE.Input.Keyboard.keyPressedQueueCount < MAX_KEY_PRESSED_QUEUE)
                    CORE.Input.Keyboard.keyPressedQueue[CORE.Input.Keyboard.keyPressedQueueCount++] = key;
                CORE.Input.Keyboard.currentKeyState[key] = event.down;
                if (event.down && key == CORE.Input.Keyboard.exitKey) CORE.Window.shouldClose = true;
            }
            if (event.down)
            {
                uint32_t ch = xkb_state_key_get_utf32(platform.xkbState, event.keycode);
                int text = (ch >= 32 && ch != 127) ||
                           (ch >= 1 && ch <= 26 && key >= KEY_A && key <= KEY_Z);
                unsigned int modifiers = 0;

                if (xkb_state_mod_name_is_active(platform.xkbState, XKB_MOD_NAME_SHIFT,
                        XKB_STATE_MODS_EFFECTIVE) > 0) modifiers |= CWM_KEY_SHIFT;
                if (xkb_state_mod_name_is_active(platform.xkbState, XKB_MOD_NAME_CTRL,
                        XKB_STATE_MODS_EFFECTIVE) > 0) modifiers |= CWM_KEY_CONTROL;
                if (xkb_state_mod_name_is_active(platform.xkbState, XKB_MOD_NAME_ALT,
                        XKB_STATE_MODS_EFFECTIVE) > 0) modifiers |= CWM_KEY_ALT;
                if (platform.keyEventCount < 128)
                    platform.keyEvents[platform.keyEventCount++] = (cwm_raylib_key_event){
                        key, text ? (int)ch : 0, modifiers };
                if (ch >= 32 && ch != 127 &&
                    CORE.Input.Keyboard.charPressedQueueCount < MAX_CHAR_PRESSED_QUEUE)
                {
                    CORE.Input.Keyboard.charPressedQueue[CORE.Input.Keyboard.charPressedQueueCount++] = (int)ch;
                }
            }
            else platform.heldKeys[event.keycode] = 0;
        }
    }
    if (result < 0) CORE.Window.shouldClose = true;
}

//----------------------------------------------------------------------------------
// Module Internal Functions Definition
//----------------------------------------------------------------------------------

// Initialize platform: graphics, inputs and more
int InitPlatform(void)
{
    // Memory framebuffer can only work with software renderer
    if (rlGetVersion() != RL_OPENGL_SOFTWARE)
    {
        TRACELOG(LOG_WARNING, "DISPLAY: Memory platform requires software renderer (GRAPHICS_API_OPENGL_SOFTWARE)");
        TRACELOG(LOG_FATAL, "PLATFORM: Failed to initialize graphics device");
        return -1;
    }
    else
    {
        if (cwm_client_connect() < 0) return -1;
        platform.windowId = cwm_request_create_window(CORE.Window.title,
                             CORE.Window.screen.width, CORE.Window.screen.height);
        if (platform.windowId < 0) { cwm_client_disconnect(); return -1; }
        CORE.Window.screen.width = cwm_client_width();
        CORE.Window.screen.height = cwm_client_height();
        CORE.Window.display = CORE.Window.screen;
        platform.xkbContext = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        if (platform.xkbContext)
            platform.xkbKeymap = xkb_keymap_new_from_names(platform.xkbContext, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
        if (platform.xkbKeymap) platform.xkbState = xkb_state_new(platform.xkbKeymap);
    }
    //----------------------------------------------------------------------------

    // If everything worked as expected, continue
    CORE.Window.render.width = CORE.Window.screen.width;
    CORE.Window.render.height = CORE.Window.screen.height;
    CORE.Window.currentFbo.width = CORE.Window.render.width;
    CORE.Window.currentFbo.height = CORE.Window.render.height;

    TRACELOG(LOG_INFO, "DISPLAY: Device initialized successfully");
    TRACELOG(LOG_INFO, "    > Display size: %i x %i", CORE.Window.display.width, CORE.Window.display.height);
    TRACELOG(LOG_INFO, "    > Screen size:  %i x %i", CORE.Window.screen.width, CORE.Window.screen.height);
    TRACELOG(LOG_INFO, "    > Render size:  %i x %i", CORE.Window.render.width, CORE.Window.render.height);
    TRACELOG(LOG_INFO, "    > Viewport offsets: %i, %i", CORE.Window.renderOffset.x, CORE.Window.renderOffset.y);

    CORE.Window.ready = true;

    // TODO: Load OpenGL extensions
    // NOTE: GL procedures address loader is required to load extensions
    //----------------------------------------------------------------------------
    // ...
    //----------------------------------------------------------------------------

    // Initialize timing system
    //----------------------------------------------------------------------------
#if defined(_WIN32)
    LARGE_INTEGER time = { 0 };
    QueryPerformanceCounter(&time);
    QueryPerformanceFrequency(&platform.timerFrequency);
    CORE.Time.base = time.QuadPart;
#endif
    InitTimer();
    //----------------------------------------------------------------------------

    // Initialize storage system
    //----------------------------------------------------------------------------
    CORE.Storage.basePath = GetWorkingDirectory();
    //----------------------------------------------------------------------------

    TRACELOG(LOG_INFO, "PLATFORM: chrmaWM: Initialized successfully");

    return 0;
}

// Close platform
void ClosePlatform(void)
{
    if (platform.windowId > 0) cwm_request_destroy_window(platform.windowId);
    cwm_client_disconnect();
    if (platform.xkbState) xkb_state_unref(platform.xkbState);
    if (platform.xkbKeymap) xkb_keymap_unref(platform.xkbKeymap);
    if (platform.xkbContext) xkb_context_unref(platform.xkbContext);
}

// EOF
