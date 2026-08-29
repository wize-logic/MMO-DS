/* The window the viewer already speaks, on the app's own GLES2. */

#ifndef OPENMMO_SDLSHIM_SDL_H
#define OPENMMO_SDLSHIM_SDL_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t  Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef uint64_t Uint64;
typedef int16_t  Sint16;
typedef int32_t  Sint32;
typedef int64_t  Sint64;

#define SDL_memset memset
#define SDL_strcasecmp strcasecmp

/* ------------------------------------------------------------------ */
/* Init                                                                */
/* ------------------------------------------------------------------ */

#define SDL_INIT_VIDEO          0x00000020u
#define SDL_INIT_AUDIO          0x00000010u
#define SDL_INIT_GAMECONTROLLER 0x00002000u

int SDL_Init(Uint32 flags);
int SDL_InitSubSystem(Uint32 flags);
void SDL_Quit(void);
const char *SDL_GetError(void);

#define SDL_HINT_RENDER_SCALE_QUALITY "SDL_RENDER_SCALE_QUALITY"
int SDL_SetHint(const char *name, const char *value);

/* ------------------------------------------------------------------ */
/* Geometry, colour                                                    */
/* ------------------------------------------------------------------ */

typedef struct SDL_Rect { int x, y, w, h; } SDL_Rect;
typedef struct SDL_Color { Uint8 r, g, b, a; } SDL_Color;

/* ------------------------------------------------------------------ */
/* Window                                                              */
/* ------------------------------------------------------------------ */

typedef struct SDL_Window SDL_Window;

#define SDL_WINDOWPOS_UNDEFINED 0x1FFF0000
#define SDL_WINDOW_FULLSCREEN         0x00000001u
#define SDL_WINDOW_FULLSCREEN_DESKTOP 0x00001001u
#define SDL_WINDOW_RESIZABLE          0x00000020u
#define SDL_WINDOW_INPUT_FOCUS        0x00000200u
#define SDL_WINDOW_MAXIMIZED          0x00000080u

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h,
                             Uint32 flags);
void SDL_DestroyWindow(SDL_Window *win);
void SDL_SetWindowTitle(SDL_Window *win, const char *title);
void SDL_SetWindowSize(SDL_Window *win, int w, int h);
void SDL_GetWindowSize(SDL_Window *win, int *w, int *h);
int SDL_SetWindowFullscreen(SDL_Window *win, Uint32 flags);
Uint32 SDL_GetWindowFlags(SDL_Window *win);
int SDL_GetWindowDisplayIndex(SDL_Window *win);
int SDL_GetDisplayUsableBounds(int display, SDL_Rect *rect);
void SDL_RaiseWindow(SDL_Window *win);
int SDL_SetWindowInputFocus(SDL_Window *win);
const char *SDL_GetCurrentVideoDriver(void);

/* ------------------------------------------------------------------ */
/* Renderer                                                            */
/* ------------------------------------------------------------------ */

typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;

#define SDL_RENDERER_SOFTWARE     0x00000001u
#define SDL_RENDERER_ACCELERATED  0x00000002u
#define SDL_RENDERER_PRESENTVSYNC 0x00000004u

typedef struct SDL_RendererInfo {
    const char *name;
    Uint32 flags;
} SDL_RendererInfo;

typedef enum SDL_BlendMode {
    SDL_BLENDMODE_NONE  = 0,
    SDL_BLENDMODE_BLEND = 1
} SDL_BlendMode;

/* SDL2's own fourcc values, so a recorded number reads the same. */
#define SDL_PIXELFORMAT_XRGB8888 0x16161804u
#define SDL_PIXELFORMAT_ARGB8888 0x16362004u
#define SDL_PIXELFORMAT_ABGR8888 0x16762004u

#define SDL_TEXTUREACCESS_STATIC    0
#define SDL_TEXTUREACCESS_STREAMING 1

SDL_Renderer *SDL_CreateRenderer(SDL_Window *win, int index, Uint32 flags);
void SDL_DestroyRenderer(SDL_Renderer *ren);
int SDL_GetRendererInfo(SDL_Renderer *ren, SDL_RendererInfo *info);
int SDL_GetRendererOutputSize(SDL_Renderer *ren, int *w, int *h);
int SDL_SetRenderDrawColor(SDL_Renderer *ren, Uint8 r, Uint8 g, Uint8 b,
                           Uint8 a);
int SDL_SetRenderDrawBlendMode(SDL_Renderer *ren, SDL_BlendMode mode);
int SDL_GetRenderDrawBlendMode(SDL_Renderer *ren, SDL_BlendMode *mode);
int SDL_RenderClear(SDL_Renderer *ren);
int SDL_RenderFillRect(SDL_Renderer *ren, const SDL_Rect *rect);
int SDL_RenderDrawRect(SDL_Renderer *ren, const SDL_Rect *rect);
int SDL_RenderCopy(SDL_Renderer *ren, SDL_Texture *tex, const SDL_Rect *src,
                   const SDL_Rect *dst);
int SDL_RenderSetClipRect(SDL_Renderer *ren, const SDL_Rect *rect);
void SDL_RenderGetClipRect(SDL_Renderer *ren, SDL_Rect *rect);
int SDL_RenderReadPixels(SDL_Renderer *ren, const SDL_Rect *rect,
                         Uint32 format, void *pixels, int pitch);
void SDL_RenderPresent(SDL_Renderer *ren);

SDL_Texture *SDL_CreateTexture(SDL_Renderer *ren, Uint32 format, int access,
                               int w, int h);
void SDL_DestroyTexture(SDL_Texture *tex);
int SDL_UpdateTexture(SDL_Texture *tex, const SDL_Rect *rect,
                      const void *pixels, int pitch);
int SDL_LockTexture(SDL_Texture *tex, const SDL_Rect *rect, void **pixels,
                    int *pitch);
void SDL_UnlockTexture(SDL_Texture *tex);
int SDL_SetTextureColorMod(SDL_Texture *tex, Uint8 r, Uint8 g, Uint8 b);
int SDL_SetTextureAlphaMod(SDL_Texture *tex, Uint8 a);
int SDL_SetTextureBlendMode(SDL_Texture *tex, SDL_BlendMode mode);

/* ------------------------------------------------------------------ */
/* Surfaces                                                            */
/* ------------------------------------------------------------------ */

typedef struct SDL_Surface {
    Uint32 format;   /* one of the SDL_PIXELFORMAT_* words above */
    int w, h;
    int pitch;
    void *pixels;
} SDL_Surface;

SDL_Surface *SDL_CreateRGBSurfaceWithFormat(Uint32 flags, int w, int h,
                                            int depth, Uint32 format);
void SDL_FreeSurface(SDL_Surface *sf);
int SDL_LockSurface(SDL_Surface *sf);
void SDL_UnlockSurface(SDL_Surface *sf);
SDL_Texture *SDL_CreateTextureFromSurface(SDL_Renderer *ren, SDL_Surface *sf);

/* ------------------------------------------------------------------ */
/* Keys                                                                */
/* ------------------------------------------------------------------ */

/* SDL2's scancodes (usb hid usages). Only the ones the viewer speaks are
 * named; the state array is sized for the full range so an unnamed one is
 * merely a key nobody reads. */
typedef enum SDL_Scancode {
    SDL_SCANCODE_UNKNOWN = 0,
    SDL_SCANCODE_A = 4,  SDL_SCANCODE_B = 5,  SDL_SCANCODE_C = 6,
    SDL_SCANCODE_D = 7,  SDL_SCANCODE_E = 8,  SDL_SCANCODE_F = 9,
    SDL_SCANCODE_G = 10, SDL_SCANCODE_H = 11, SDL_SCANCODE_I = 12,
    SDL_SCANCODE_J = 13, SDL_SCANCODE_K = 14, SDL_SCANCODE_L = 15,
    SDL_SCANCODE_M = 16, SDL_SCANCODE_N = 17, SDL_SCANCODE_O = 18,
    SDL_SCANCODE_P = 19, SDL_SCANCODE_Q = 20, SDL_SCANCODE_R = 21,
    SDL_SCANCODE_S = 22, SDL_SCANCODE_T = 23, SDL_SCANCODE_U = 24,
    SDL_SCANCODE_V = 25, SDL_SCANCODE_W = 26, SDL_SCANCODE_X = 27,
    SDL_SCANCODE_Y = 28, SDL_SCANCODE_Z = 29,
    SDL_SCANCODE_RETURN = 40,
    SDL_SCANCODE_ESCAPE = 41,
    SDL_SCANCODE_BACKSPACE = 42,
    SDL_SCANCODE_TAB = 43,
    SDL_SCANCODE_SPACE = 44,
    SDL_SCANCODE_F10 = 67,
    SDL_SCANCODE_F11 = 68,
    SDL_SCANCODE_F12 = 69,
    SDL_SCANCODE_HOME = 74,
    SDL_SCANCODE_END = 77,
    SDL_SCANCODE_DELETE = 76,
    SDL_SCANCODE_RIGHT = 79,
    SDL_SCANCODE_LEFT = 80,
    SDL_SCANCODE_DOWN = 81,
    SDL_SCANCODE_UP = 82,
    SDL_SCANCODE_KP_ENTER = 88,
    SDL_NUM_SCANCODES = 512
} SDL_Scancode;

#define SDLK_SCANCODE_MASK (1 << 30)
#define SDL_SCANCODE_TO_KEYCODE(sc) ((sc) | SDLK_SCANCODE_MASK)

typedef Sint32 SDL_Keycode;

enum {
    SDLK_BACKSPACE = '\b',
    SDLK_TAB = '\t',
    SDLK_RETURN = '\r',
    SDLK_ESCAPE = 27,
    SDLK_DELETE = 127,
    SDLK_a = 'a', SDLK_n = 'n', SDLK_o = 'o', SDLK_y = 'y', SDLK_z = 'z',
    SDLK_F10 = SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_F10),
    SDLK_F11 = SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_F11),
    SDLK_F12 = SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_F12),
    SDLK_HOME = SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_HOME),
    SDLK_END = SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_END),
    SDLK_RIGHT = SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_RIGHT),
    SDLK_LEFT = SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_LEFT),
    SDLK_DOWN = SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_DOWN),
    SDLK_UP = SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_UP),
    SDLK_KP_ENTER = SDL_SCANCODE_TO_KEYCODE(SDL_SCANCODE_KP_ENTER)
};

typedef enum SDL_Keymod {
    KMOD_NONE  = 0x0000,
    KMOD_SHIFT = 0x0003,
    KMOD_CTRL  = 0x00C0,
    KMOD_ALT   = 0x0300,
    KMOD_GUI   = 0x0C00
} SDL_Keymod;

typedef struct SDL_Keysym {
    SDL_Scancode scancode;
    SDL_Keycode sym;
    Uint16 mod;
} SDL_Keysym;

const Uint8 *SDL_GetKeyboardState(int *numkeys);
SDL_Keymod SDL_GetModState(void);
SDL_Scancode SDL_GetScancodeFromName(const char *name);
void SDL_StartTextInput(void);
void SDL_StopTextInput(void);
int SDL_SetClipboardText(const char *text);

/* ------------------------------------------------------------------ */
/* Mouse                                                               */
/* ------------------------------------------------------------------ */

#define SDL_BUTTON(x)     (1u << ((x) - 1))
#define SDL_BUTTON_LEFT   1
#define SDL_BUTTON_LMASK  SDL_BUTTON(SDL_BUTTON_LEFT)

Uint32 SDL_GetMouseState(int *x, int *y);

/* ------------------------------------------------------------------ */
/* Game controller                                                     */
/* ------------------------------------------------------------------ */

typedef struct SDL_GameController SDL_GameController;
typedef struct SDL_Joystick SDL_Joystick;
typedef Sint32 SDL_JoystickID;

typedef enum SDL_GameControllerButton {
    SDL_CONTROLLER_BUTTON_A = 0,
    SDL_CONTROLLER_BUTTON_B = 1,
    SDL_CONTROLLER_BUTTON_X = 2,
    SDL_CONTROLLER_BUTTON_Y = 3,
    SDL_CONTROLLER_BUTTON_BACK = 4,
    SDL_CONTROLLER_BUTTON_START = 6,
    SDL_CONTROLLER_BUTTON_LEFTSHOULDER = 9,
    SDL_CONTROLLER_BUTTON_RIGHTSHOULDER = 10,
    SDL_CONTROLLER_BUTTON_DPAD_UP = 11,
    SDL_CONTROLLER_BUTTON_DPAD_DOWN = 12,
    SDL_CONTROLLER_BUTTON_DPAD_LEFT = 13,
    SDL_CONTROLLER_BUTTON_DPAD_RIGHT = 14,
    SDL_CONTROLLER_BUTTON_MAX = 15
} SDL_GameControllerButton;

typedef enum SDL_GameControllerAxis {
    SDL_CONTROLLER_AXIS_LEFTX = 0,
    SDL_CONTROLLER_AXIS_LEFTY = 1,
    SDL_CONTROLLER_AXIS_MAX = 6
} SDL_GameControllerAxis;

int SDL_NumJoysticks(void);
int SDL_IsGameController(int index);
SDL_GameController *SDL_GameControllerOpen(int index);
void SDL_GameControllerClose(SDL_GameController *gc);
const char *SDL_GameControllerName(SDL_GameController *gc);
Uint8 SDL_GameControllerGetButton(SDL_GameController *gc,
                                  SDL_GameControllerButton button);
Sint16 SDL_GameControllerGetAxis(SDL_GameController *gc,
                                 SDL_GameControllerAxis axis);
SDL_Joystick *SDL_GameControllerGetJoystick(SDL_GameController *gc);
SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick *js);

/* ------------------------------------------------------------------ */
/* Events                                                              */
/* ------------------------------------------------------------------ */

enum {
    SDL_QUIT = 0x100,
    SDL_WINDOWEVENT = 0x200,
    SDL_KEYDOWN = 0x300,
    SDL_KEYUP = 0x301,
    SDL_TEXTINPUT = 0x303,
    SDL_MOUSEMOTION = 0x400,
    SDL_MOUSEBUTTONDOWN = 0x401,
    SDL_MOUSEBUTTONUP = 0x402,
    SDL_MOUSEWHEEL = 0x403,
    SDL_CONTROLLERDEVICEADDED = 0x653,
    SDL_CONTROLLERDEVICEREMOVED = 0x654
};

enum {
    SDL_WINDOWEVENT_SHOWN = 1,
    SDL_WINDOWEVENT_EXPOSED = 3,
    SDL_WINDOWEVENT_FOCUS_GAINED = 12
};

#define SDL_TEXTINPUTEVENT_TEXT_SIZE 32

typedef struct SDL_KeyboardEvent {
    Uint32 type;
    Uint8 repeat;
    SDL_Keysym keysym;
} SDL_KeyboardEvent;

typedef struct SDL_TextInputEvent {
    Uint32 type;
    char text[SDL_TEXTINPUTEVENT_TEXT_SIZE];
} SDL_TextInputEvent;

typedef struct SDL_MouseMotionEvent {
    Uint32 type;
    Uint32 state;
    Sint32 x, y;
    Sint32 xrel, yrel;
} SDL_MouseMotionEvent;

typedef struct SDL_MouseButtonEvent {
    Uint32 type;
    Uint8 button;
    Uint8 clicks;
    Sint32 x, y;
} SDL_MouseButtonEvent;

typedef struct SDL_MouseWheelEvent {
    Uint32 type;
    Sint32 x, y;
} SDL_MouseWheelEvent;

typedef struct SDL_ControllerDeviceEvent {
    Uint32 type;
    Sint32 which;
} SDL_ControllerDeviceEvent;

typedef struct SDL_WindowEvent {
    Uint32 type;
    Uint8 event;
} SDL_WindowEvent;

typedef union SDL_Event {
    Uint32 type;
    SDL_KeyboardEvent key;
    SDL_TextInputEvent text;
    SDL_MouseMotionEvent motion;
    SDL_MouseButtonEvent button;
    SDL_MouseWheelEvent wheel;
    SDL_ControllerDeviceEvent cdevice;
    SDL_WindowEvent window;
} SDL_Event;

int SDL_PollEvent(SDL_Event *ev);

/* ------------------------------------------------------------------ */
/* Audio                                                               */
/* ------------------------------------------------------------------ */

typedef Uint32 SDL_AudioDeviceID;
typedef Uint16 SDL_AudioFormat;

#define AUDIO_S16SYS 0x8010
#define SDL_AUDIO_ALLOW_FREQUENCY_CHANGE 0x00000001

typedef void (*SDL_AudioCallback)(void *userdata, Uint8 *stream, int len);

typedef struct SDL_AudioSpec {
    int freq;
    SDL_AudioFormat format;
    Uint8 channels;
    Uint8 silence;
    Uint16 samples;
    Uint32 size;
    SDL_AudioCallback callback;
    void *userdata;
} SDL_AudioSpec;

SDL_AudioDeviceID SDL_OpenAudioDevice(const char *device, int iscapture,
                                      const SDL_AudioSpec *want,
                                      SDL_AudioSpec *obtained,
                                      int allowed_changes);
void SDL_CloseAudioDevice(SDL_AudioDeviceID dev);
void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on);
void SDL_LockAudioDevice(SDL_AudioDeviceID dev);
void SDL_UnlockAudioDevice(SDL_AudioDeviceID dev);
int SDL_GetNumAudioDevices(int iscapture);
const char *SDL_GetAudioDeviceName(int index, int iscapture);

/* ------------------------------------------------------------------ */
/* Time                                                                */
/* ------------------------------------------------------------------ */

Uint32 SDL_GetTicks(void);
Uint64 SDL_GetTicks64(void);
void SDL_Delay(Uint32 ms);
Uint64 SDL_GetPerformanceCounter(void);
Uint64 SDL_GetPerformanceFrequency(void);

#ifdef __cplusplus
}
#endif

#endif /* OPENMMO_SDLSHIM_SDL_H */
