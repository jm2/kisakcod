#include "cl_sdl3.h"
#include "cl_sdl3_keys.h"

#include <SDL3/SDL.h>

#include <client/client.h>
#include <gfx_d3d/r_dvars.h>
#include <gfx_d3d/r_init.h>
#include <qcommon/cmd.h>
#include <qcommon/qcommon.h>
#include <qcommon/sys_local.h>
#include <qcommon/sys_time.h>
#include <sound/snd_public.h>
#include <ui/keycodes.h>

#ifdef _WIN32
#include <win32/win_local.h>
#endif

// The key table hard-codes SDL_Scancode numbers so it builds without SDL.
static_assert(SDL_SCANCODE_RETURN == 40 && SDL_SCANCODE_GRAVE == 53 && SDL_SCANCODE_F1 == 58);
static_assert(SDL_SCANCODE_F12 == 69 && SDL_SCANCODE_PAUSE == 72 && SDL_SCANCODE_UP == 82);
static_assert(SDL_SCANCODE_KP_1 == 89 && SDL_SCANCODE_KP_0 == 98 && SDL_SCANCODE_KP_PERIOD == 99);
static_assert(SDL_SCANCODE_KP_EQUALS == 103 && SDL_SCANCODE_LCTRL == 224 && SDL_SCANCODE_RALT == 230);
static_assert(SDL_BUTTON_LMASK == 1 && SDL_BUTTON_MMASK == 2 && SDL_BUTTON_RMASK == 4);
static_assert(SDL_BUTTON_X1MASK == 8 && SDL_BUTTON_X2MASK == 16);

extern const dvar_t *vid_xpos;
extern const dvar_t *vid_ypos;
extern const dvar_t *r_fullscreen;
void __cdecl VID_AppActivate(uint32_t activeState, int minimize);
void __cdecl IN_ActivateMouse(int force);

namespace
{
SDL_Window *s_window;
SDL_MouseButtonFlags s_buttons;
bool s_focused;

void *NativeHandle(SDL_Window *window)
{
#ifdef _WIN32
    return SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#else
    return window;
#endif
}

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
    const uint32_t time = EventTime(key.timestamp);
    // MainWndProc's WM_SYSKEYDOWN: Alt+Enter toggles fullscreen in developer mode.
    if (key.down && key.scancode == SDL_SCANCODE_RETURN && (key.mod & SDL_KMOD_ALT))
    {
        if (client_state != 7 && r_fullscreen && Dvar_GetInt("developer"))
        {
            Dvar_SetBool(r_fullscreen, !r_fullscreen->current.enabled);
            Cbuf_AddText(0, "vid_restart\n");
        }
        return;
    }

    const bool keypadAsText = (clientUIActives[0].keyCatchers & 0x11) != 0;
    const int32_t code = CL_SdlMapKey(key.scancode, key.key, keypadAsText, (key.mod & SDL_KMOD_NUM) != 0);
    if (code)
        Sys_QueEvent(time, SE_KEY, code, key.down, 0, nullptr);
    if (key.down)
    {
        const int32_t ch = ControlChar(key);
        if (ch)
            Sys_QueEvent(time, SE_CHAR, ch, 0, 0, nullptr);
    }
}

// WM_CHAR delivers one CP1252 byte per character; Latin-1 code points are the
// same bytes, and anything wider has no engine key.
void TextEvent(const SDL_TextInputEvent &text)
{
    const uint32_t time = EventTime(text.timestamp);
    const char *cursor = text.text;
    for (Uint32 cp = SDL_StepUTF8(&cursor, nullptr); cp; cp = SDL_StepUTF8(&cursor, nullptr))
    {
        if (cp >= 0x20 && cp <= 0xFF && cp != 0x7F)
            Sys_QueEvent(time, SE_CHAR, static_cast<int>(cp), 0, 0, nullptr);
    }
}

void WheelEvent(const SDL_MouseWheelEvent &wheel)
{
    int32_t steps = wheel.integer_y;
    if (wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
        steps = -steps;
    if (!steps)
        return;
    const uint32_t time = EventTime(wheel.timestamp);
    const int key = steps > 0 ? K_MWHEELUP : K_MWHEELDOWN;
    Sys_QueEvent(time, SE_KEY, key, 1, 0, nullptr);
    Sys_QueEvent(time, SE_KEY, key, 0, 0, nullptr);
}

void WindowMoved(const SDL_WindowEvent &window)
{
    // MainWndProc's WM_MOVE: remember the windowed position for the next start.
    if (r_fullscreen->current.enabled)
    {
        IN_ActivateMouse(0);
        return;
    }
    Dvar_SetInt(vid_xpos, window.data1);
    Dvar_SetInt(vid_ypos, window.data2);
    Dvar_ClearModified((dvar_s *)vid_xpos);
    Dvar_ClearModified((dvar_s *)vid_ypos);
    if (s_focused)
        IN_Activate(1);
}

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
    case SDL_EVENT_MOUSE_WHEEL:
        WheelEvent(ev.wheel);
        break;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        s_focused = ev.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
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

    const SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "Call of Duty 4");
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, wndParms->x);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, wndParms->y);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, wndParms->displayWidth);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, wndParms->displayHeight);
    // The Win32 path makes fullscreen a hidden topmost popup that
    // R_CreateGameWindow shows once the device exists.
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_BORDERLESS_BOOLEAN, wndParms->fullscreen || borderless);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_HIDDEN_BOOLEAN, wndParms->fullscreen);
    SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_ALWAYS_ON_TOP_BOOLEAN, wndParms->fullscreen);
    s_window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    if (!s_window)
    {
        Com_Printf(8, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        return false;
    }

    wndParms->hwnd = static_cast<decltype(wndParms->hwnd)>(NativeHandle(s_window));
    s_buttons = 0;
    s_focused = (SDL_GetWindowFlags(s_window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    SDL_StartTextInput(s_window);

    // MainWndProc's WM_CREATE work.
#ifdef _WIN32
    g_wv.hWnd = wndParms->hwnd;
#endif
    SND_SetHWND(wndParms->hwnd);
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

bool CL_SdlPumpEvents()
{
    if (!s_window)
        return false;
    SDL_Event ev;
    while (SDL_PollEvent(&ev))
        HandleEvent(ev);
    return true;
}
