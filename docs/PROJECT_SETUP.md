# Game Engine — Project Setup & Plan

A Handmade-Hero-style engine with an SDL3 platform layer, developed on macOS,
targeting Windows, macOS, and Linux. Written in C-style C++, starting with a
software renderer. This document describes how the repo is organized, how it
builds, and what to implement in what order. It intentionally contains no
code — the point is to know *what* to build and *why*, then build it yourself.

---

## 1. Philosophy

These are the ground rules everything below follows:

- **You write the engine; libraries only talk to the OS.** SDL is used purely
  as the *platform layer* — window, input events, audio device, blitting a
  pixel buffer. Everything above that (rendering, memory, assets, game logic)
  is yours. If SDL disappeared tomorrow you'd rewrite one file, not the engine.
- **The platform calls the game, not the other way around.** The game layer is
  a pile of pure-ish functions the platform layer invokes each frame. The game
  never talks to the OS directly; anything it needs from the platform comes in
  through a small, explicit interface. This is what makes hot reloading and
  input playback possible.
- **Allocate memory once, up front.** No `malloc` scattered through the
  codebase. The platform hands the game one (or two) big blocks at startup and
  the game sub-allocates from them with arenas. Out-of-memory is a startup
  failure, not a runtime one.
- **Self-contained repo.** Cloning the repo and running the build script is
  all it takes. All third-party source lives in the repo (vendored, not
  submodules — submodules break plain zip downloads). The only things assumed
  to exist on the machine are a C++ compiler and CMake (see §4).
- **No build system for *your* code.** Your code compiles as two translation
  units (a "unity build") via a ~20-line shell script. Full rebuilds should
  stay under a second or two. CMake exists in this repo only to build SDL,
  once.

---

## 2. Repository layout

```
game-engine/
├── build.sh              # builds everything on macOS/Linux (entry point)
├── build.bat             # same for Windows (write later, when needed)
├── .gitignore            # ignores build/ — never commit artifacts
├── README.md             # one paragraph + "run ./build.sh"
├── docs/
│   └── PROJECT_SETUP.md  # this file
├── code/                 # ALL code you write lives here, flat, no subdirs yet
│   ├── game.h            # the platform↔game interface (the most important file)
│   ├── game.cpp          # game layer — compiled as a shared library
│   └── sdl_platform.cpp  # platform layer — compiled as the executable
├── data/                 # game assets (art, sound, levels) — committed
├── third_party/
│   └── SDL/              # full SDL3 source tree, copied from a release tarball
└── build/                # ALL build output (gitignored)
    ├── sdl/              # SDL's own build directory + the built static lib
    ├── game.dylib        # (.so on Linux, .dll on Windows)
    └── engine            # the platform executable
```

Notes on the choices:

- `code/` stays **flat** until it hurts. Handmade Hero never needed
  subdirectories for a long time; premature folder taxonomy is a way to feel
  productive without building anything.
- File naming convention: platform-specific files are prefixed with the
  platform (`sdl_platform.cpp`; later maybe `win32_platform.cpp` if you ever
  want a raw Win32 layer again). Platform-independent files have no prefix.
- `third_party/SDL/` is a verbatim copy of an SDL3 **release source tarball**
  (e.g. `SDL3-3.x.y`, from github.com/libsdl-org/SDL/releases). Don't edit
  anything inside it. Record which version it is in a small
  `third_party/SDL/VERSION.txt` or in the README so future-you knows.

---

## 3. The two translation units

This is the core architectural decision, straight from Handmade Hero:

### `sdl_platform.cpp` → the executable

Owns `main()`, the event loop, and every SDL call in the project. Its job:

1. Create the window, the backbuffer texture, and the audio stream.
2. Allocate the game's memory block(s).
3. Each frame: gather input into a plain struct, call the game's update
   function, hand the game's pixel output to SDL, hand its audio output to
   SDL.
4. Load/reload the game shared library (hot reload, §6.6).
5. Record/replay input (looped live editing, §6.7).

### `game.cpp` → a shared library (`game.dylib` / `.so` / `.dll`)

Owns everything about the actual game. Exports (for now) two functions the
platform looks up by name:

- one called every frame with: the memory block, the frame's input, and the
  backbuffer to draw into — "update and render";
- one called when the audio device wants samples — "get sound samples".

It includes **no SDL headers and no OS headers**. Its entire view of the
outside world is `game.h`.

### `game.h` → the contract

Defines, as plain structs: the memory layout handed to the game, the input
state for a frame (keyboard/gamepad, analog sticks, buttons with
half-transition counts), the backbuffer description (pixels pointer, width,
height, pitch), the sound buffer description (samples pointer, sample count,
samples-per-second), and typedefs for the game's exported function signatures.
Also declares the handful of *platform services* the game may call back into
(initially just debug file read/write — passed to the game as function
pointers inside the memory struct, so the game still links against nothing).

When you're unsure where something goes, ask: "would this code differ if I
swapped SDL for raw Win32?" If yes → platform layer. If no → game layer.

---

## 4. Toolchain & prerequisites

Per platform, the only things not in the repo:

| Platform | Compiler                              | Also needed |
|----------|---------------------------------------|-------------|
| macOS    | Apple clang (`xcode-select --install`)| CMake       |
| Linux    | clang or gcc                          | CMake       |
| Windows  | MSVC (VS Build Tools) or clang        | CMake       |

CMake is used **only** to build SDL, and the build script drives it — you
never write or read CMake files. (SDL is realistically only buildable with
CMake; this is the one pragmatic concession to the "download nothing" goal.
If you want to go further later, you can commit the built static libs per
platform and make CMake optional.)

Compiler settings to standardize on (put these in the build script, not your
head):

- Compile as C++ but disable the runtime features you don't use:
  **no exceptions, no RTTI** (`-fno-exceptions -fno-rtti`).
- Warnings on and fatal: `-Wall -Wextra -Werror`, then explicitly disable the
  few that fight this style (unused parameters/variables are the usual ones —
  disable those two rather than littering casts).
- Debug build by default: `-g -O0`, plus a define like `-DBUILD_DEBUG=1` so
  code can `#if` on it. An optimized mode (`-O2`) as a script flag for when
  you want to feel fast.
- Define `-DBUILD_INTERNAL=1` in dev builds to gate debug-only platform
  services (like the file I/O helpers) so they can't sneak into a shipping
  build.

---

## 5. The build script

`build.sh`, run from the repo root. What it does, in order:

1. **Build SDL if needed** (first run only): if the SDL static library doesn't
   exist in `build/sdl/`, invoke CMake on `third_party/SDL/` configured for a
   **static** library, build it, done. Takes a minute or two, once. Every
   later run skips this because the file exists.
   - Static linking is deliberate: the final program is a single executable
     with no `.dylib`/`.so` distribution or rpath headaches. (On Linux, a
     static SDL still works everywhere because SDL `dlopen`s X11/Wayland/ALSA
     at runtime by itself.)
2. **Build the game library**: one compiler invocation for `game.cpp` →
   `build/game.dylib`. No SDL involved at all.
3. **Build the platform executable**: one compiler invocation for
   `sdl_platform.cpp` → `build/engine`, linking the SDL static lib plus the
   OS frameworks SDL requires (on macOS that's a list of `-framework` flags —
   SDL's docs/CMake output tell you which; on Linux it's roughly `-lm -ldl
   -lpthread`).

That's it — three steps, and step 1 is usually a no-op. Steps 2 and 3 are each
a single compiler command you can read in full. When you touch only game code,
you only need step 2 (and with hot reload running, you don't even restart the
program).

---

## 6. Functionality roadmap

In order. Each milestone is small, runnable, and visibly does something. This
mirrors the Handmade Hero platform-layer arc, translated to SDL3.

### 6.1 Window + event loop
Blank window that opens, stays open, and closes cleanly on the close button
and a quit key. Core pieces: SDL init, window creation, the
poll-events-then-do-a-frame loop, clean shutdown. Get comfortable with the
event loop — everything else hangs off it.

### 6.2 Software backbuffer ("weird gradient")
The heart of the software renderer. You allocate a plain block of memory as a
width×height array of 32-bit pixels. Each frame the *game* writes pixels into
it (start with the classic animated XY gradient), then the *platform* hands it
to SDL via a streaming texture and presents it. Decide and document your pixel
format (e.g. BGRA in memory) and understand *pitch* (bytes per row — don't
assume `width * 4`). Handle window resize by recreating the buffer.
macOS note: with a high-DPI display the window's size in *points* differs from
its size in *pixels* — SDL3 has explicit APIs for the pixel size; use those
for the buffer or your image will be scaled/blurry.

### 6.3 Input
Fill a normalized input struct each frame from SDL keyboard events and the
SDL3 gamepad API (handles controller hot-plugging via device-added/removed
events). Per button, store "ended down" plus a half-transition count so the
game can detect presses/releases even at low frame rates. Analog sticks: apply
a deadzone, normalize to -1..1. Prove it works by moving the gradient with
keys/stick.

### 6.4 Audio
Output a continuous sine wave, then tie its pitch to input to feel the
latency. SDL3's audio model is a *stream you push samples into* (much saner
than SDL2's callback or Win32 DirectSound ring buffers): each frame you ask
how much is queued, compute how many samples to generate to stay just ahead of
the device, ask the game for exactly that many, push them. Keep queued audio
small (2–4 frames' worth) — this is your latency. Decide your sample format
(16-bit signed stereo at 48000 Hz is a fine choice) and put it in `game.h`.

### 6.5 Timing
Fixed target frame rate (start at 60, or the display's refresh rate).
Measure frame time with SDL's high-resolution counter; sleep off the excess
(sleep is imprecise — sleep *most* of it, spin the rest, exactly like HH's
Win32 `Sleep` dance). Pass the frame's delta-time into the game. Print/overlay
ms-per-frame from day one so you always notice when you get slow. Later,
consider vsync via the renderer and reconcile it with manual timing.

### 6.6 Platform/game split + memory + hot reload
The payoff milestone.

- **Memory**: at startup, the platform allocates one big region (`mmap` on
  mac/Linux, `VirtualAlloc` on Windows) split into *permanent* and *transient*
  storage, sizes fixed in code (e.g. 64 MB / 1 GB — generous is free, it's
  virtual). Zeroed at start. The game's entire state lives in permanent
  storage as one big struct — which is exactly why reloads and replays work.
  In debug builds, request a fixed base address so pointers stay valid across
  replay runs.
- **Split**: move all game code behind the `game.h` boundary; the platform
  `dlopen`s the game library and looks up its exported functions.
- **Hot reload**: each frame (or each second), stat the game library's file
  modification time; if it changed, unload and reload it. Two gotchas:
  (1) always **copy the library to a temp/unique filename and load the copy**,
  never load the original — otherwise you block the compiler from writing it,
  and `dlopen` may hand you back the stale cached image for a path it's seen
  before; (2) keep *zero* pointers to functions or globals inside the old
  library after unloading. Game state survives because it lives in the
  platform-owned memory block, not in the library.
  The workflow this buys: game running, edit `game.cpp`, run build step 2,
  watch the running game change. Do not proceed past this milestone until that
  works — it's the tool you'll use for everything after.

### 6.7 Looped live code editing (input record/playback)
Debug feature built on 6.6: a key begins recording — snapshot the entire game
memory block to a file, then append each frame's input struct. Another key
stops and starts looping: restore the snapshot, replay the recorded inputs,
repeat. Because the game is deterministic given memory + inputs, the loop
replays identically — and because of hot reload, you can *edit code while the
loop runs* and watch the same ten seconds of gameplay change behavior. This is
the single highest-leverage debugging tool in the whole HH toolkit.

### 6.8 Debug platform services
The game sometimes needs files before a real asset system exists. Provide
three platform functions via function pointers in the memory struct: read an
entire file into memory, free that memory, write an entire file. Explicitly
`DEBUG`-prefixed and compiled only when `BUILD_INTERNAL` — they're
placeholders for the real asset pipeline, and naming them ugly keeps you
honest.

### Milestone checklist

- [ ] 6.1 Window opens/closes cleanly
- [ ] 6.2 Animated gradient rendered by CPU, blitted by SDL
- [ ] 6.3 Keyboard + gamepad move the gradient
- [ ] 6.4 Sine wave, pitch tied to input, low latency
- [ ] 6.5 Steady 60 fps with measured frame times
- [ ] 6.6 Edit game.cpp → rebuild → running game changes, state intact
- [ ] 6.7 Record N seconds of input, loop it, edit code mid-loop
- [ ] 6.8 DEBUG file read/write callable from game code

---

## 7. Explicitly deferred (don't touch yet)

- **GPU rendering.** The software renderer is behind the backbuffer interface;
  when 2D-vs-3D resolves, a GPU backend (SDL3 GPU API, or Metal/Vulkan/D3D
  directly) slots in as an alternate consumer of the game's render output —
  by then, HH-style, via a renderer command buffer rather than raw pixels.
- **Asset pipeline / file formats.** DEBUG file I/O carries you for months.
- **Multithreading / job system.** Comes naturally later (HH does audio &
  rendering work on threads eventually); the architecture above doesn't block it.
- **Windows build script.** Write `build.bat` when you actually sit at a
  Windows machine; the game layer needs zero changes, the SDL platform layer
  compiles as-is, only compiler flags/link libraries differ.
- **Engine/game separation, editors, scripting.** You are building *a game*
  with full control; the engine is whatever the game needs. Resist
  generalizing ahead of need.

## 8. First session, concretely

1. `git init`, commit this doc, add `.gitignore` containing `build/`.
2. Download an SDL3 release source tarball, extract into `third_party/SDL/`,
   note the version, commit it.
3. Write `build.sh` (§5) with just steps 1 and 3 (no game library yet — put
   everything in `sdl_platform.cpp` until milestone 6.6, exactly like HH kept
   everything in `win32_handmade.cpp` at first).
4. Milestone 6.1: a window.
