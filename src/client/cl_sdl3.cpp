#include "cl_sdl3.h"
#include "cl_sdl3_keys.h"

#include <SDL3/SDL.h>

#include <client/client.h>
#include <gfx_d3d/r_display_modes.h>
#include <gfx_d3d/r_dvars.h>
#include <gfx_d3d/r_init.h>
#include <qcommon/cmd.h>
#include <qcommon/qcommon.h>
#include <qcommon/sys_local.h>
#include <qcommon/sys_time.h>
#include <sound/snd_public.h>
#include <ui/keycodes.h>
#include <universal/com_memory.h>

#ifdef _WIN32
#include <win32/win_local.h>
#endif

#ifdef __APPLE__
// The Objective-C runtime, declared here: <objc/objc.h> redefines BOOL.
extern "C" void *objc_getClass(const char *name);
extern "C" void *sel_registerName(const char *name);
extern "C" void objc_msgSend(void);
extern "C" void *objc_autoreleasePoolPush(void);
extern "C" void objc_autoreleasePoolPop(void *pool);
#endif

// The key table hard-codes SDL_Scancode numbers so it builds without SDL.
static_assert(SDL_SCANCODE_RETURN == 40 && SDL_SCANCODE_GRAVE == 53 && SDL_SCANCODE_F1 == 58);
static_assert(SDL_SCANCODE_F12 == 69 && SDL_SCANCODE_PAUSE == 72 && SDL_SCANCODE_UP == 82);
static_assert(SDL_SCANCODE_KP_1 == 89 && SDL_SCANCODE_KP_0 == 98 && SDL_SCANCODE_KP_PERIOD == 99);
static_assert(SDL_SCANCODE_KP_EQUALS == 103 && SDL_SCANCODE_LCTRL == 224 && SDL_SCANCODE_RALT == 230);
static_assert(SDL_BUTTON_LMASK == 1 && SDL_BUTTON_MMASK == 2 && SDL_BUTTON_RMASK == 4);
static_assert(SDL_BUTTON_X1MASK == 8 && SDL_BUTTON_X2MASK == 16);
static_assert(kSdlModAlt == SDL_KMOD_ALT && kSdlModGui == SDL_KMOD_GUI);

// win32/win_wndproc.cpp is not built with KISAK_CLIENT_SDL3; these are its
// definitions. r_dvars.cpp registers the dvars.
const dvar_t *vid_xpos;
const dvar_t *vid_ypos;
const dvar_t *r_fullscreen;
void __cdecl IN_ActivateMouse(int force);

namespace
{
SDL_Window *s_window;
SDL_ThreadID s_windowThread;
bool s_minimized;
SDL_MouseButtonFlags s_buttons;
bool s_focused;
// Motion since the last CL_SdlTakeMouseMotion; fractions carry over.
float s_motionX;
float s_motionY;

void *NativeHandle(SDL_Window *window)
{
#ifdef _WIN32
    return SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#else
    return window;
#endif
}

#ifdef __APPLE__
// r_mode sizes are pixels, but macOS sizes windows in points, and a Retina
// point is two pixels. Size the (high-DPI) window so its drawable has r_mode's
// pixels, shrunk to fit the display's usable area with the title bar (#993).
void FitWindowSize(SDL_DisplayID display, float pixelsPerPoint, int borderW, int borderH, int *w, int *h)
{
    if (pixelsPerPoint > 1.0f)
    {
        *w = static_cast<int>(static_cast<float>(*w) / pixelsPerPoint + 0.5f);
        *h = static_cast<int>(static_cast<float>(*h) / pixelsPerPoint + 0.5f);
    }
    SDL_Rect usable;
    if (!display || !SDL_GetDisplayUsableBounds(display, &usable))
        return;
    const int maxW = usable.w - borderW;
    const int maxH = usable.h - borderH;
    if (maxW <= 0 || maxH <= 0 || (*w <= maxW && *h <= maxH))
        return;
    // Keep the aspect ratio; dxvk scales the back buffer to the window.
    const float fit = SDL_min(static_cast<float>(maxW) / static_cast<float>(*w),
                              static_cast<float>(maxH) / static_cast<float>(*h));
    *w = SDL_max(1, static_cast<int>(static_cast<float>(*w) * fit));
    *h = SDL_max(1, static_cast<int>(static_cast<float>(*h) * fit));
}

// Fit the windowed window, once it exists and has a title bar to measure.
void FitWindowedWindow(int pixelW, int pixelH)
{
    int top = 0;
    int left = 0;
    int bottom = 0;
    int right = 0;
    SDL_GetWindowBordersSize(s_window, &top, &left, &bottom, &right);
    int w = pixelW;
    int h = pixelH;
    FitWindowSize(SDL_GetDisplayForWindow(s_window), SDL_GetWindowPixelDensity(s_window), left + right, top + bottom,
                  &w, &h);
    SDL_SetWindowSize(s_window, w, h);
}
#endif

#ifdef __APPLE__
// An Objective-C message through the runtime, so this file stays C++.
template <typename R, typename... Args> R ObjcSend(void *receiver, const char *selector, Args... args)
{
    return reinterpret_cast<R (*)(void *, void *, Args...)>(objc_msgSend)(receiver, sel_registerName(selector), args...);
}

// macOS shows an opaque layer that fills a fullscreen display straight on the
// panel, past the window compositor. With KosmicKrisp's drawables that path
// went black or froze until an overlay such as Spotlight brought the
// compositor back, and once it hung WindowServer (#991). A non-opaque layer
// always goes through the compositor. KosmicKrisp makes the layer opaque again
// whenever dxvk rebuilds the swapchain, so each pump checks it. Windowed keeps
// the opaque layer; it is composited anyway.
void KeepFullscreenComposited()
{
    void *nsWindow =
        SDL_GetPointerProperty(SDL_GetWindowProperties(s_window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    if (!nsWindow)
        return;
    // Nothing else drains a pool on this thread, and the getters below can
    // return autoreleased objects, so each pump gets its own.
    void *const pool = objc_autoreleasePoolPush();
    // Objective-C's BOOL is bool on arm64.
    const bool opaque = (SDL_GetWindowFlags(s_window) & SDL_WINDOW_FULLSCREEN) == 0;
    void *const metalLayer = objc_getClass("CAMetalLayer");
    void *subviews = ObjcSend<void *>(ObjcSend<void *>(nsWindow, "contentView"), "subviews");
    const unsigned long count = ObjcSend<unsigned long>(subviews, "count");
    for (unsigned long i = 0; i < count; ++i)
    {
        void *layer = ObjcSend<void *>(ObjcSend<void *>(subviews, "objectAtIndex:", i), "layer");
        if (layer && ObjcSend<bool>(layer, "isKindOfClass:", metalLayer) && ObjcSend<bool>(layer, "isOpaque") != opaque)
            ObjcSend<void>(layer, "setOpaque:", opaque);
    }
    objc_autoreleasePoolPop(pool);
}
#endif

// SDL stamps events in nanoseconds on its own clock; the queue wants the
// engine's millisecond clock, as Win32 gets from the message time.
uint32_t EventTime(Uint64 timestampNs)
{
    const Uint64 ageMs = (SDL_GetTicksNS() - timestampNs) / SDL_NS_PER_MS;
    return Sys_Milliseconds() - static_cast<uint32_t>(ageMs);
}

// TranslateMessage turns these key downs into WM_CHAR control characters, and
// the console and UI edit fields rely on them; SDL text input never carries them.
int32_t ControlChar(const SDL_KeyboardEvent &key)
{
    switch (key.scancode)
    {
    case SDL_SCANCODE_BACKSPACE: return '\b';
    case SDL_SCANCODE_TAB: return '\t';
    case SDL_SCANCODE_RETURN:
    case SDL_SCANCODE_KP_ENTER: return '\r';
    case SDL_SCANCODE_ESCAPE: return 0x1B;
    default: break;
    }
    if ((key.mod & SDL_KMOD_CTRL) && !(key.mod & SDL_KMOD_ALT) && key.key >= 'a' && key.key <= 'z')
        return static_cast<int32_t>(key.key - 'a' + 1);
    return 0;
}

void KeyEvent(const SDL_KeyboardEvent &key)
{
    const uint32_t eventTime = EventTime(key.timestamp);
    // MainWndProc's WM_SYSKEYDOWN: Alt+Enter toggles fullscreen (#917);
    // Command+Return on macOS. Every press of the combination is the toggle's,
    // held repeats too, so it never reaches a binding or the chat line as
    // Enter; only the first press toggles, as WM_SYSKEYDOWN's auto-repeat bit
    // does on Win32.
#if defined(__APPLE__)
    constexpr bool apple = true;
#else
    constexpr bool apple = false;
#endif
    if (key.down && CL_SdlIsFullscreenToggle(key.scancode, key.mod, apple))
    {
        if (!key.repeat && client_state != 7 && r_fullscreen)
        {
            const bool fullscreen = !r_fullscreen->current.enabled;
            Dvar_SetBool(r_fullscreen, fullscreen);
            R_SetFullscreenAtDesktop(fullscreen);
            Cbuf_AddText(0, "vid_restart\n");
        }
        return;
    }

    const bool keypadAsText = (clientUIActives[0].keyCatchers & 0x11) != 0;
    const int32_t code = CL_SdlMapKey(key.scancode, key.key, keypadAsText, (key.mod & SDL_KMOD_NUM) != 0);
    if (code)
        Sys_QueEvent(eventTime, SE_KEY, code, key.down, 0, nullptr);
    if (key.down)
    {
        const int32_t ch = ControlChar(key);
        if (ch)
            Sys_QueEvent(eventTime, SE_CHAR, ch, 0, 0, nullptr);
    }
}

// WM_CHAR delivers one CP1252 byte per character; anything CP1252 can't
// represent has no engine character.
void TextEvent(const SDL_TextInputEvent &text)
{
    const uint32_t eventTime = EventTime(text.timestamp);
    const char *cursor = text.text;
    for (Uint32 cp = SDL_StepUTF8(&cursor, nullptr); cp; cp = SDL_StepUTF8(&cursor, nullptr))
    {
        const uint32_t ch = CL_SdlCodepointToCp1252(cp);
        if (ch)
            Sys_QueEvent(eventTime, SE_CHAR, static_cast<int>(ch), 0, 0, nullptr);
    }
}

void WheelEvent(const SDL_MouseWheelEvent &wheel)
{
    int32_t steps = wheel.integer_y;
    if (wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
        steps = -steps;
    if (!steps)
        return;
    const uint32_t eventTime = EventTime(wheel.timestamp);
    const int key = steps > 0 ? K_MWHEELUP : K_MWHEELDOWN;
    // One press and release per whole notch, so a fast flick isn't merged.
    for (int32_t step = steps > 0 ? steps : -steps; step > 0; --step)
    {
        Sys_QueEvent(eventTime, SE_KEY, key, 1, 0, nullptr);
        Sys_QueEvent(eventTime, SE_KEY, key, 0, 0, nullptr);
    }
}

void WindowMoved(const SDL_WindowEvent &window)
{
    // MainWndProc's WM_MOVE: remember the windowed position for the next start.
    if (r_fullscreen->current.enabled)
    {
        IN_ActivateMouse(0);
        return;
    }
    // Not for a window still fullscreen after Alt+Enter has cleared
    // r_fullscreen, nor for one a restart has since destroyed: its events can
    // still be queued (#946).
    if (s_window && window.windowID == SDL_GetWindowID(s_window) &&
        R_IsWindowedMove(r_fullscreen->current.enabled, vidConfig.isFullscreen != 0))
    {
        Dvar_SetInt(vid_xpos, window.data1);
        Dvar_SetInt(vid_ypos, window.data2);
        Dvar_ClearModified(const_cast<dvar_s *>(vid_xpos));
        Dvar_ClearModified(const_cast<dvar_s *>(vid_ypos));
    }
    if (s_focused)
        IN_Activate(1);
}

} // namespace

// MainWndProc's activation handling, unchanged: focus or minimise clears key
// state and (de)activates the mouse.
void __cdecl VID_AppActivate(uint32_t activeState, int minimize)
{
    s_minimized = minimize != 0;
    Key_ClearStates(0);
    const bool active = activeState && !s_minimized;
#ifdef _WIN32
    g_wv.isMinimized = minimize;
    g_wv.activeApp = active;
#endif
    if (active)
        Com_TouchMemory();
    IN_Activate(active);
}

namespace
{
void HandleEvent(const SDL_Event &ev)
{
    switch (ev.type)
    {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        Key_RemoveCatcher(0, -3);
        Com_Quit_f();
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        KeyEvent(ev.key);
        break;
    case SDL_EVENT_TEXT_INPUT:
        TextEvent(ev.text);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (ev.button.down)
            s_buttons |= SDL_BUTTON_MASK(ev.button.button);
        else
            s_buttons &= ~SDL_BUTTON_MASK(ev.button.button);
        IN_MouseEvent(CL_SdlMapMouseButtons(s_buttons));
        break;
    case SDL_EVENT_MOUSE_MOTION:
        s_motionX += ev.motion.xrel;
        s_motionY += ev.motion.yrel;
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        WheelEvent(ev.wheel);
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        s_focused = ev.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
        // A button released while another application had focus never
        // reaches us, so release them all on the way out.
        if (!s_focused && s_buttons)
        {
            s_buttons = 0;
            IN_MouseEvent(0);
        }
        VID_AppActivate(s_focused, (SDL_GetWindowFlags(s_window) & SDL_WINDOW_MINIMIZED) != 0);
        break;
    case SDL_EVENT_WINDOW_MINIMIZED:
        VID_AppActivate(0, 1);
        break;
    case SDL_EVENT_WINDOW_RESTORED:
        VID_AppActivate(s_focused, 0);
        break;
    case SDL_EVENT_WINDOW_MOVED:
        WindowMoved(ev.window);
        break;
    default:
        break;
    }
}
} // namespace

bool CL_SdlCreateWindow(GfxWindowParms *wndParms, bool borderless)
{
    iassert(wndParms);
    iassert(!wndParms->hwnd);
    iassert(!s_window);
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        Com_Printf(8, "SDL video init failed: %s\n", SDL_GetError());
        return false;
    }

    // The Win32 build reads cursor deltas, which carry the OS pointer speed
    // and acceleration; keep them in relative mode so sensitivity matches.
    SDL_SetHint(SDL_HINT_MOUSE_RELATIVE_SYSTEM_SCALE, "1");
    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Call of Duty 4");
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, wndParms->x);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, wndParms->y);
#ifdef __APPLE__
    int createW = wndParms->displayWidth;
    int createH = wndParms->displayHeight;
    const SDL_Point createAt = {wndParms->x, wndParms->y};
    SDL_DisplayID display = SDL_GetDisplayForPoint(&createAt);
    if (!display)
        display = SDL_GetPrimaryDisplay();
    const SDL_DisplayMode *desktop = display ? SDL_GetDesktopDisplayMode(display) : nullptr;
    FitWindowSize(display, desktop ? desktop->pixel_density : 1.0f, 0, 0, &createW, &createH);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, createW);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, createH);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIGH_PIXEL_DENSITY_BOOLEAN, true);
#else
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, wndParms->displayWidth);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, wndParms->displayHeight);
#endif
#ifdef __APPLE__
    // macOS fullscreen is the window's own native (Spaces) fullscreen, over a
    // windowed device (R_SetD3DPresentParameters, #974).
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, borderless);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_FULLSCREEN_BOOLEAN, wndParms->fullscreen);
#else
    // The Win32 path makes fullscreen a hidden topmost popup that
    // R_CreateGameWindow shows once the device exists.
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, wndParms->fullscreen || borderless);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN, wndParms->fullscreen);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_ALWAYS_ON_TOP_BOOLEAN, wndParms->fullscreen);
#endif
    s_window = SDL_CreateWindowWithProperties(props);
    s_windowThread = SDL_GetCurrentThreadID();
    SDL_DestroyProperties(props);
    if (!s_window)
    {
        Com_Printf(8, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }

#ifdef __APPLE__
    if (!wndParms->fullscreen)
        FitWindowedWindow(wndParms->displayWidth, wndParms->displayHeight);
#endif
    wndParms->hwnd = static_cast<decltype(wndParms->hwnd)>(NativeHandle(s_window));
    s_buttons = 0;
    s_motionX = s_motionY = 0.0f;
    s_focused = (SDL_GetWindowFlags(s_window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    // Text input follows the key catchers (SyncTextInput, each pump).

    // MainWndProc's WM_CREATE work.
#ifdef _WIN32
    g_wv.hWnd = wndParms->hwnd;
    SND_SetHWND(wndParms->hwnd);
#endif
    iassert(r_reflectionProbeGenerate);
    if (r_reflectionProbeGenerate->current.enabled && r_fullscreen->current.enabled)
    {
        Dvar_SetBool(r_fullscreen, 0);
        Cbuf_AddText(0, "vid_restart\n");
    }
    return true;
}

void CL_SdlShowWindow(void *nativeWindow)
{
    if (s_window && NativeHandle(s_window) == nativeWindow)
        SDL_ShowWindow(s_window);
}

void CL_SdlSetWindowMode(const GfxWindowParms *wndParms, bool borderless)
{
    if (!s_window || NativeHandle(s_window) != wndParms->hwnd)
        return;
#ifdef __APPLE__
    // Native (Spaces) fullscreen, as other macOS games do: with a fullscreen
    // device instead, dxvk's own mode switch fought this window's and the
    // screen went black (#974). The back buffer keeps r_mode's size, and dxvk
    // scales it to the window.
    if (wndParms->fullscreen)
    {
        SDL_SetWindowFullscreenMode(s_window, nullptr);
        SDL_SetWindowFullscreen(s_window, true);
    }
    else
    {
        SDL_SetWindowFullscreen(s_window, false);
        SDL_SyncWindow(s_window);
        SDL_SetWindowBordered(s_window, !borderless);
        FitWindowedWindow(wndParms->displayWidth, wndParms->displayHeight);
        SDL_SetWindowPosition(s_window, wndParms->x, wndParms->y);
    }
    SDL_SyncWindow(s_window);
    SDL_ShowWindow(s_window);
    return;
#endif
    SDL_SetWindowBordered(s_window, !(wndParms->fullscreen || borderless));
    SDL_SetWindowAlwaysOnTop(s_window, wndParms->fullscreen);
    SDL_SetWindowSize(s_window, wndParms->displayWidth, wndParms->displayHeight);
    SDL_SetWindowPosition(s_window, wndParms->x, wndParms->y);
    SDL_ShowWindow(s_window);
}

void CL_SdlDestroyWindow(void *nativeWindow)
{
    if (!s_window || NativeHandle(s_window) != nativeWindow)
        return;
    SDL_StopTextInput(s_window);
    SDL_DestroyWindow(s_window);
    s_window = nullptr;
#ifdef _WIN32
    g_wv.hWnd = nullptr;
#endif
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

namespace
{
// Text input only when CL_CharEvent would take the characters: the console
// (catcher 0x1), the chat line (0x20), a menu (0x10), or the full-screen
// console while disconnected. In play it stays off: on macOS an active text
// input shows the press-and-hold accent picker while a movement key is held.
void SyncTextInput()
{
    const bool wanted = (clientUIActives[0].keyCatchers & 0x31) != 0
        || clientUIActives[0].connectionState == CA_DISCONNECTED;
    if (wanted == SDL_TextInputActive(s_window))
        return;
    if (wanted)
        SDL_StartTextInput(s_window);
    else
        SDL_StopTextInput(s_window);
}
} // namespace

bool CL_SdlPumpEvents()
{
    if (!s_window)
        return false;
    // SDL pumps events only on the thread that made the window; Cocoa throws
    // otherwise. The render thread's loading keep-alive leaves them queued.
    if (SDL_GetCurrentThreadID() != s_windowThread)
        return true;
    SyncTextInput();
    SDL_Event ev;
    while (SDL_PollEvent(&ev))
        HandleEvent(ev);
#ifdef __APPLE__
    KeepFullscreenComposited();
#endif
    return true;
}

bool CL_SdlHasFocus()
{
    return s_window && (SDL_GetWindowFlags(s_window) & SDL_WINDOW_INPUT_FOCUS) != 0;
}

void CL_SdlRaiseWindow()
{
    if (s_window)
        SDL_RaiseWindow(s_window);
}

namespace
{
// Back-buffer pixels per window unit. The menus take the cursor in back-buffer
// pixels, as the Win32 client's are; macOS native fullscreen makes the window
// the display's size while dxvk scales r_mode's back buffer to it (#977).
void BackBufferPerWindowUnit(float *sx, float *sy)
{
    *sx = 1.0f;
    *sy = 1.0f;
    int w = 0;
    int h = 0;
    if (s_window && SDL_GetWindowSize(s_window, &w, &h) && w > 0 && h > 0 && vidConfig.displayWidth &&
        vidConfig.displayHeight)
    {
        *sx = static_cast<float>(vidConfig.displayWidth) / static_cast<float>(w);
        *sy = static_cast<float>(vidConfig.displayHeight) / static_cast<float>(h);
    }
}
} // namespace

void CL_SdlTakeMouseMotion(int *x, int *y, int *dx, int *dy)
{
    float fx = 0.0f;
    float fy = 0.0f;
    SDL_GetMouseState(&fx, &fy);
    float sx;
    float sy;
    BackBufferPerWindowUnit(&sx, &sy);
    *x = static_cast<int>(fx * sx);
    *y = static_cast<int>(fy * sy);
    *dx = static_cast<int>(s_motionX);
    *dy = static_cast<int>(s_motionY);
    s_motionX -= static_cast<float>(*dx);
    s_motionY -= static_cast<float>(*dy);
}

void CL_SdlSetMouseCaptured(bool captured)
{
    if (s_window && SDL_GetWindowRelativeMouseMode(s_window) != captured)
        SDL_SetWindowRelativeMouseMode(s_window, captured);
}

void CL_SdlWarpMouse(int x, int y)
{
    if (!s_window)
        return;
    float sx;
    float sy;
    BackBufferPerWindowUnit(&sx, &sy);
    SDL_WarpMouseInWindow(s_window, static_cast<float>(x) / sx, static_cast<float>(y) / sy);
}

bool CL_SdlIsMinimized()
{
    return s_minimized;
}
