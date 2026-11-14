# The platform layer: a new one on SDL3, written from zero (survey)

Written on 14 November 2026 for plan step 15. Nothing is implemented. This document holds what the present platform code does (read in this repository, with `file:line`), what is legacy or a workaround in it, what SDL 3 changes, what other engines did, a design written from zero, and the decisions the owner has to take. Sources are labelled primary (the project's own documentation or code), secondary (a project that reports on another) or community. What was not read or verified is in section 9.

## 1. Summary

- The client's platform code is SDL 2.0.12 (2020, `lib/SDL2/include/SDL_version.h:60-62`, prebuilt for Windows; Linux and macOS take the system's SDL2, `codemp/CMakeLists.txt:389-416`). It is `shared/sdl/sdl_window.cpp` (900 lines), `sdl_input.cpp` (1,224), `sdl_sound.cpp` (329), and a few calls in `shared/sys/sys_main.cpp` and `sys_loadlib.h`: 339 SDL calls in all. The dedicated server does not use SDL (its calls are under `#ifndef DEDICATED`, and it does not link the library).
- The engine already reaches that code through a small interface (`WIN_Init`, `WIN_Present`, `WIN_SetGamma`, `WIN_Shutdown`, `shared/sys/sys_public.h:201-204`; `IN_Init`, `IN_Frame`, `IN_Shutdown`, `IN_Restart`; `SNDDMA_*`, `shared/sdl/sdl_sound.h`), so "the engine no longer calls SDL" is true today. What is old is behind the interface, and partly in the interface itself (a gamma ramp, a table of video modes, a sound buffer shared with a callback).
- The owner's wish (14 November 2026): not a port, a new platform layer from zero on SDL 3, modern and well written, with the same features, no workaround, the present best practices, as fast as it can be.
- What a new layer gives this project, in the order of its worth: the input read off the main thread by SDL 3 on Windows and handed over with its time (the last delay left between a key and a command); the display handled as today's systems do (borderless fullscreen, several screens, pixel density, a fullscreen switch without a video restart); the audio device followed when the system's default changes; the gamepad through SDL's mappings; and the end of the code written for the hardware of 1999 to 2005.
- What it costs: the feel of the mouse and of the keys must be proven unchanged (it was tested by hand in step 14 part 10); SDL 3 is in neither Debian 12 nor Ubuntu 24.04, so it is built from source and shipped; hardware gamma is gone from SDL 3, so the gamma moves into the renderer for every player; `shared/` is also compiled by the single-player and Jedi Outcast trees.

## 2. What exists

### The window and the OpenGL context (`shared/sdl/sdl_window.cpp`)

| what | where | notes |
|---|---|---|
| a fixed table of 13 video modes, 320x240 to 2048x1536 and two wide ones, chosen by `r_mode`; `-1` takes `r_customwidth` and `r_customheight`, `-2` the desktop's | `:73-88`, `R_GetModeInfo` `:92` | the menus list these modes |
| the display's modes enumerated, sorted by closeness to the desktop's aspect, published in `r_availableModes` | `GLimp_DetectAvailableModes` `:226`, `GLimp_CompareModes` `:200` | |
| the window and the context created in a loop of 16 attempts that lower the colour, depth and stencil sizes in turn (24 to 16 to 8) | `GLimp_SetMode` `:318`, the loop from `:450` | 16-bit colour, 16-bit depth: the hardware of the time |
| exclusive fullscreen with a display mode set by pixel format, borderless fullscreen (`r_noborder`), a centred window (`r_centerWindow`) | `:401-434`, `:581-640` | |
| stereo buffers (`r_stereo`), software OpenGL allowed or not (`r_allowSoftwareGL`), a refresh rate (`r_displayRefresh`), multisampling (`r_ext_multisample`) | `:735-756` | |
| the context's version, profile and debug flag as the renderer asks | `:520-548` | used by rend2 |
| the swap, and the swap interval changed while running | `WIN_Present` `:145` | |
| fullscreen switched while running, or a `vid_restart` when SDL refuses | `:163-186` | |
| hardware gamma: a ramp set on the window, clamped on Windows as Windows 2000 required | `WIN_SetGamma` `:838`, the clamp `:853-870` | `glConfig->deviceSupportsGamma` from `SDL_SetWindowBrightness` `:774` |
| the window's native handle given to the renderer, on Windows | `:785-798` | for a Direct3D renderer that does not exist |
| the icon | `sdl_icon.h` | |

### The input (`shared/sdl/sdl_input.cpp`)

| what | where | notes |
|---|---|---|
| SDL's key symbols turned into the game's key codes (`fakeAscii_t`, `A_*`), with the character for the console | `IN_TranslateSDLToJKKey` `:279` | by the layout's symbol, not by the key's place |
| the console key, by a list of keys and characters (`cl_consoleKeys`), also with a modifier | `IN_IsConsoleKey` `:89`, `IN_ModTogglesConsole` `:262` | its comment waits for SDL's scancodes to "improve" (`:85`) |
| the keypad: digits or navigation by the num lock state, read from Windows directly | `IN_NumLockEnabled` `:182`, `IN_TranslateNumpad` `:192` | changed on this branch (digits only while a key catcher is active) |
| text: UTF-8 from SDL turned into the engine's one-byte character set | `ConvertUTF32ToExpectedCharset` `:655` | text input is started once, at the start, and never stopped (`:634`) |
| the mouse: relative mode with the window's grab; `in_mouse 2` asks SDL to emulate it by warping the cursor; the cursor put back at the window's centre when the mouse is given back; the pending motion events thrown away at each change | `IN_ActivateMouse` `:440`, `IN_DeactivateMouse` `:478`, `IN_GobbleMotionEvents` `:421`, the hint `:639-643` | |
| when the mouse is taken: not with the console down in a window, not while loading in a window, not without the focus | `IN_Frame` `:1156` | polled every frame, and since step 14 part 10 at every tick |
| the events read in one loop: keys, text, mouse motion, buttons, wheel, quit, minimized, focus | `IN_ProcessEvents` `:819` | every event is queued with the time it is read at (`Sys_QueEvent( 0, ... )`); SDL's own time stamps are not used |
| the focus lost: `com_unfocused`, the sound paused | `:936-947` | |
| the joystick: one device chosen by number at start (`in_joystick`, latched; `in_joystickNo`), read by polling: axes, hats, buttons and trackballs turned into keys and mouse moves | `IN_InitJoystick` `:545`, `IN_JoyMove` `:956` | no hot plug, no mapping |

### The sound device (`shared/sdl/sdl_sound.cpp`)

The mixer (`codemp/client/snd_dma.cpp`) paints into a ring buffer it shares with the device (the "DMA" model of the sound cards of the 1990s); SDL's callback copies from that ring into the device's buffer (`SNDDMA_AudioCallback` `:51`); the mixer learns the time from how far the callback has read (`SNDDMA_GetDMAPos` `:264`) and locks the device while it paints (`SNDDMA_BeginPainting` `:304`, `SNDDMA_Submit` `:294`). The format is asked by four cvars (`s_sdlBits`, `s_sdlChannels`, `s_sdlDevSamps`, `s_sdlMixSamps`, `:161-164`). The device is the default one at the start; it does not follow a change of the system's default. An OpenAL path exists beside it (`USE_OPENAL`, `:309`).

### The rest (`shared/sys/`)

The clipboard's text (`sys_main.cpp:107-116`), the error message box (`:214`, `:227`), SDL's version printed (`:783-790`), `SDL_Quit` (`:175`), and the loading of the modules' libraries through `SDL_LoadObject` except where `dlopen` is used (`sys_loadlib.h:33-43`). The system events go through a queue of 256 entries (`sys_event.cpp:37`).

## 3. Legacy and workarounds in it

1. The 16 attempts at a pixel format, the 16-bit colour and depth, the mode table from 320x240, software OpenGL, stereo buffers: written for hardware that no longer exists.
2. Hardware gamma, with a clamp for Windows 2000: it changes the whole screen, also outside the window, and SDL 3 removed it for that reason (section 4).
3. The pending mouse motion thrown away at each change of the mouse's state, and the cursor warped back to the centre: remains of the time relative mode was emulated.
4. The num lock state read from the Windows API beside SDL.
5. The keys bound by the layout's symbol: a bind made on one keyboard layout is another key on another layout; the console key needs a list per layout.
6. Text input always on: the system's text services (dead keys, input methods) run during play.
7. The joystick as raw axes and buttons, chosen by number, with trackballs.
8. The sound ring shared with a callback under a lock, and the mixer's clock derived from the callback's position.
9. A window handle kept for a renderer that does not exist.
10. Every input event dated when it is read, not when it happened.

## 4. What SDL 3 changes

From the migration guide (primary) unless noted:

- Gamma: `SDL_SetWindowGammaRamp`, its getter and the window brightness calls are removed, "bad behaviour on modern operating systems" and effects outside the window given as the reason.
- Relative mouse mode is per window (`SDL_SetWindowRelativeMouseMode`).
- Display modes: `SDL_GetFullscreenDisplayModes` replaces the enumeration by index; a window is in borderless fullscreen when it has no fullscreen mode and in exclusive fullscreen when it has one (`SDL_SetWindowFullscreenMode`); refresh rates are floats.
- Pixel density: `SDL_WINDOW_HIGH_PIXEL_DENSITY`, the window's size in pixels asked apart from its size in points (`SDL_GetWindowSizeInPixels`).
- Text input is off until started, per window.
- Audio is "dramatically different": a device is opened with a stream that converts format and rate; the callback devices, the device lock and the pause-then-start pattern are gone. A stream can be fed by a callback or by pushing data (porting notes of other projects, secondary).
- Events carry a time in nanoseconds (`SDL_events.h`, primary).
- On Windows, raw mouse input is read on a thread of its own (the SDL commits list, primary), and the keyboard can be, behind `SDL_HINT_WINDOWS_RAW_KEYBOARD`, off by default (SDL issue 11336, primary; a framework that turned it on for lower latency: osu-framework PR 6507, secondary). The raw events are stamped with SDL's clock when they are processed, not with the hardware's time (an audit in another project, secondary; a fix to those stamps, SDL PR 11553, primary).
- Gamepads: an API of its own with SDL's mapping database and hot plug by events (SDL 3 headers, primary; not checked in detail here).

Availability (secondary: issues of other projects, Repology): SDL 3.2.0, the first stable release, is of January 2025; Debian 12 and Ubuntu 24.04 have no `libsdl3`; Debian 13 and Ubuntu 25.04 do. SDL 2 is in maintenance, its retirement planned; `sdl2-compat` gives the SDL 2 API on top of SDL 3.

## 5. What other engines did

- ioquake3 (the engine this code descends from): a port from SDL 2 to SDL 3 was done live in about 90 minutes in November 2024 (the project's blog, primary); a pull request "SDL 3" is open since August 2025, where the missing gamma is named as the gap and a shader as the answer (ioq3 PR 749, primary); no tagged release with SDL 3 was found.
- Yamagi Quake II: SDL 3 support, marked experimental (its changelog, primary).
- OpenJKDF2 builds with a vendored SDL 3 (its repository, primary).
- OpenJK: no pull request or issue about SDL 3 was found (a web search, not a search in the repository itself).

So the ports that exist are mechanical ones, and gamma is where they all stop. None of them is the redesign asked for here: this document's design has no model to copy, only SDL 3's own documentation.

## 6. A design from zero

### Principles

1. One module, the client's only door to the system: window and context, input, sound device, gamepad, clipboard, message box. It does not know the game; the engine does not know SDL.
2. Written new, in C++20 as `codemp/netcodec` is, by the C++ Core Guidelines, with a public header the engine's C++11 can include. No code copied from the present files except the tables that hold knowledge (the key translation, the character set).
3. A place of its own (`codemp/platform/`), for the multiplayer client only. `shared/sdl/` stays as it is for the single-player and Jedi Outcast trees, which this work does not touch.
4. No fallback for hardware that no longer exists, no setting for what can be asked from the system. A setting is kept when a player has a reason to choose (the owner's rule for the network settings, applied here).
5. Everything the layer reports carries the time it happened at, on one clock.
6. The same features for the player. What is dropped is listed and decided (section 7), not dropped silently.

### The parts

**Window and context.** Three states, as today's games offer: a window, borderless fullscreen on a chosen screen, exclusive fullscreen with a mode of that screen (resolution and refresh rate, from the screen's own list). The switch between them without restarting the renderer. The size always in pixels; the pixel density asked from the system. One request for the context (24-bit colour, 24-bit depth, 8-bit stencil, the samples asked) and one retry without multisampling, then an error that says what failed. The swap interval with the adaptive mode where the system has it. The screens followed when one is added or removed.

**Gamma.** Not in the platform layer any more: the renderer applies it. rd-vanilla already has a shader path (`r_gammaShaders`, `codemp/rd-vanilla/tr_init.cpp:706-718`, off by default, needing fragment programs and rectangle and 3D textures); without it the gamma is applied to the textures when they are loaded. The clean form is one pass at the end of the frame for the whole picture. This is renderer work, and the only part of this step that changes the picture.

**Input.** One function that hands over everything that happened since the last call, each event with its time: keys by their place on the keyboard (scancode) with the symbol the layout gives for display, text only while a text field is open (console, chat, a menu's field), the mouse's relative motion as the device counts it, buttons, the wheel, the window's focus and state. Read at the frames and at the ticks, as step 14 part 10 does. No motion thrown away, no cursor warp, no call beside SDL for the num lock. The keyboard read raw on Windows if the measure shows a gain (section 8).

**Gamepad.** SDL's gamepad API: devices come and go by events, the layout is SDL's, the sticks and triggers go to the existing joystick keys and axes. No trackballs, no device number to set.

**Sound device.** The mixer keeps producing 16-bit stereo; the device layer asks it for the next samples when the device needs them (a stream fed by a callback), without a shared ring or a lock, and SDL converts to what the device plays. The device opened is the system's default and follows it. The mixer's clock becomes the count of samples handed over. The four format cvars go. This touches how `snd_dma.cpp` learns the time; the open point of the sound louder at 333 frames per second than at 125 (step 14 part 11) is in the same code and is to look at with it, not after.

**The rest.** Clipboard, message box, the library's version; the modules' libraries loaded by the system's own call on each platform (`LoadLibrary`, `dlopen`), which needs no SDL.

**The wait of the main loop** (optional, not SDL): `Com_Frame` ends its wait with a busy loop of up to a millisecond, at each frame and since part 10 at each tick. A high-resolution timer of the system (a waitable timer on Windows, `clock_nanosleep` on Linux) with the socket would wake at the instant without the loop. It belongs to this layer; it is listed apart because it is not needed for the rest.

### The build

SDL 3 at a pinned release, built from source with the project on every platform (it is not in the distributions the owner builds on), linked as a shared library shipped with the client. The Debian VM needs the development packages SDL builds its video and audio drivers against (X11, Wayland, PipeWire or PulseAudio, ALSA). The old code stays selectable by a CMake option until the new one is accepted, then goes.

## 7. Decisions for the owner

Each with options, what it costs (CPU, bytes on the wire: none for any of them, memory: none worth counting) and a recommendation.

| # | question | options | recommendation |
|---|---|---|---|
| P1 | The keys: bound by their place or by the layout's symbol? | (a) by place (scancode), the menus showing the symbol of the player's layout: a bind is the same physical key on every layout, the console key is one key, no list; existing configuration files keep working only for the keys whose name is the same; (b) by symbol, as today | (a): it is what current games do, and it removes two workarounds; it changes what `bind w` means on a non-QWERTY keyboard |
| P2 | Gamma | (a) one pass at the end of the frame in rd-vanilla, written new; (b) the existing shader path turned on by default; (c) applied to the textures at load only | (a), with (b) as the first step to see the picture; it is renderer work, a part of its own |
| P3 | What is dropped | 16-bit colour and depth, the mode table (the screen's own modes instead), `r_allowSoftwareGL`, `r_stereo`, `r_colorbits`, `r_depthbits`, `r_stencilbits`, `in_mouse 2`, the trackballs, `in_joystickNo`, the four `s_sdl*` cvars, the window handle | drop all of them; each is a setting for a case that no longer exists or that the system answers |
| P4 | The display's settings | (a) three states and a screen, a resolution and a refresh rate only for exclusive fullscreen; (b) keep `r_mode`, `r_customwidth`, `r_customheight`, `r_noborder`, `r_fullscreen` with their present meanings | (a); the menus that list the modes change with it |
| P5 | The sound device | (a) the mixer asked for samples by the device (a pull), the clock by samples; (b) keep the shared ring and put SDL 3's stream under it | (a), together with the look at the loudness that depends on the frame rate |
| P6 | The OpenAL path | keep it beside the new device, or remove it | not read enough to recommend: to look at before the sound part |
| P7 | The raw keyboard on Windows | on, off, or a debug cvar | decided by a measure (section 8), not by its promise |
| P8 | The main loop's wait | with this step or not | with it, last, measured by the process's CPU use and by `netstats` |
| P9 | macOS | kept building, or left out as it is for the netcode (not a target, README) | left out, the code not written against it |

## 8. The checks

The layer is accepted when, on Windows 11 and on Linux (X11 and Wayland):

- the feel is unchanged: the same turn for the same mouse movement (a count of the device against degrees, compared between the two builds), the binds, the keypad, the console key on a QWERTY and an AZERTY layout, text with accents in the chat, the clipboard;
- the input is not later: `netstats`' input margin at 100, 125 and 333 frames per second (3.6 to 3.8 ms after step 14 part 10) no higher, and with the raw keyboard on and off;
- the display: the three states and the switch between them, two screens, a screen unplugged, alt-tab, minimize, a pixel density above 1;
- the picture: the same brightness as with hardware gamma at the same setting, compared on screenshots;
- the sound: the default device changed while playing, the focus lost and regained, the loudness at 125 and 333 frames per second;
- the gamepad: plugged and unplugged while playing;
- no setting needed for any of it.

## 9. What was not read or verified

- `codemp/client/snd_dma.cpp` and the OpenAL path: the sound part's design is from the device layer only.
- rend2 and its use of the context's version and profile; the Direct3D handle's users, if any.
- SDL 3's gamepad, audio stream and display APIs were not checked call by call against the headers; the migration guide's text was read through a search's summary, not in full.
- How SDL 3 stamps keyboard events on Windows, and what the raw keyboard really gains: secondary sources only, nothing measured.
- Whether OpenJK has work on SDL 3: searched on the web only.
- The single-player and Jedi Outcast trees' use of `shared/sdl/`: assumed unchanged since they are left on it.
- The time: one to two weeks was said for a port; a layer from zero with the gamma pass and the sound's clock is more, and is not estimated here.

## 10. Sources

Primary:
- SDL 3 migration guide: https://wiki.libsdl.org/SDL3/README-migration
- SDL 3 `SDL_events.h`: https://github.com/libsdl-org/SDL/blob/main/include/SDL3/SDL_events.h
- SDL issue 11336 (raw keyboard on Windows): https://github.com/libsdl-org/SDL/issues/11336
- SDL PR 11553 (raw mouse time stamps): https://github.com/libsdl-org/SDL/pull/11553
- SDL commits, raw input read on one thread: https://discourse.libsdl.org/t/sdl-fixed-dropping-raw-input-because-of-mixing-getrawinputbuffer-and-wm-input-handling/48116
- SDL 3 source and SDL 2's status: https://wiki.libsdl.org/SDL3/SourceCode
- ioquake3, the port in 90 minutes: https://ioquake3.org/ioquake3/ryan-c-gordon-ports-ioquake3-from-sdl2-to-sdl3-in-90-minutes/
- ioquake3 PR 749, "SDL 3": https://github.com/ioquake/ioq3/pull/749
- Yamagi Quake II changelog: https://github.com/yquake2/yquake2/blob/master/CHANGELOG
- OpenJKDF2 PR 457: https://github.com/shinyquagsire23/OpenJKDF2/pull/457
- OpenJK pull requests: https://github.com/JACoders/OpenJK/pulls

Secondary:
- osu-framework PR 6507 (raw keyboard turned on): https://github.com/ppy/osu-framework/pull/6507
- OpenH1 PR 270 (an audit of SDL's part of the input latency): https://github.com/OpenH1/OpenH1/pull/270
- XCSoar issue 3165 (porting notes: text input, audio, pixel density): https://github.com/XCSoar/XCSoar/issues/3165
- ddnet PR 12558 (porting notes): https://github.com/ddnet/ddnet/pull/12558
- linapple issue 267 (SDL 3 missing on Debian 12 and Ubuntu 24.04): https://github.com/linappleii/linapple/issues/267
- sdl2-compat releases: https://github.com/libsdl-org/sdl2-compat/releases
