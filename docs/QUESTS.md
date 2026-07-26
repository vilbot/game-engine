# Platform Layer Quests

Handmade Hero days 1–25 (the complete Win32 platform-layer prototyping arc), summarized
per day so you can write the SDL3 layer without rewatching. Work top to bottom — the
order is chronological and it is also the build order. Each quest is self-contained:
constants and decisions are restated where they are used, so you should almost never
need to scroll up.

Template per quest: **what Casey built** (the video content), **SDL3 translation**
(what replaces the Win32 API), **gotchas**, **done when** (your checklist).

---

## Resources

1. **guide.handmadehero.org** — annotated episode guide with per-episode topic
   timestamps. When this doc isn't enough, jump to the exact minute instead of
   rewatching a whole episode.
2. **davidgow.net/handmadepenguin** — Handmade Penguin, an SDL2 port of the early
   platform-layer days. Closest existing text to what you're doing. Mind two things:
   SDL2→SDL3 renames (Controller→Gamepad, event names), and **do not copy its audio
   chapters** — it emulates DirectSound's ring buffer on SDL2's callback API, which
   SDL3's stream API makes unnecessary (see Quest 4).
3. **handmadehero.org** — owning HH gets you per-day source snapshots. Diffing day N
   against N−1 shows exactly what changed, which beats any summary including this one.
4. **yakvi.github.io/handmade-hero-notes** — the early days rewritten as readable
   book chapters, with code.
5. **third_party/SDL/include/SDL3/** and **wiki.libsdl.org/SDL3** — your vendored SDL
   is 3.5.0; the headers are the ground truth for it.

---

## Quest map

| # | Quest | HH days | Topic | Estimate | Status |
|---|-------|---------|-------|----------|--------|
| 1 | Window + event loop | 1–2 | Setup | 1h | ~done |
| 2 | Pixel buffer + weird gradient | 3–5 | Pixel buffer | 2–4h | todo |
| 3 | Gamepad + keyboard basics | 6 | Input part 1 | 2–3h | todo |
| 4 | Square wave → sine wave | 7–9 | Audio part 1 | 2–4h | todo |
| 5 | Frame timing readout | 10 | Timing part 1 | 1–2h | todo |
| 6 | The platform↔game split | 11–15, 17 | Architecture | 4–8h | todo |
| 7 | Enforced frame rate | 18 | Timing part 2 | 1–2h | todo |
| 8 | Audio latency + sync | 19–20 | Audio part 2 | 1–2h | todo |
| 9 | Hot reload | 21–22 | Architecture | 2–4h | todo |
| 10 | Looped live editing + mouse | 23–25 | Debug tooling | 3–5h | todo |

Total ≈ 20–35 hours. Estimates assume evenings-sized sessions and that you read the
quest before coding. After Quest 10 the platform layer is *done* in the same sense HH's
was after day 25: everything for months afterward is game code.

---

## Quest 1 — Window + event loop (Days 1–2) — ~done

**Goal:** a window that opens, stays open, and closes cleanly.

### What Casey built

- **Day 1:** project skeleton. `build.bat` compiling one translation unit (unity
  build), `WinMain` entry point, `MessageBoxA` to prove the toolchain. The lasting
  ideas: the whole platform layer is one file, the build is one compiler line you can
  read, rebuilds are near-instant.
- **Day 2:** a real window. Register a window class with a callback (`WindowProc`),
  create the window, run the message loop (`GetMessage`/`TranslateMessage`/
  `DispatchMessage`). Handled `WM_CLOSE` (user clicked X — you decide what happens),
  `WM_DESTROY` (window is dying — post quit), `WM_ACTIVATEAPP` (focus change),
  `WM_PAINT` (flashed the window with `PatBlt` to see repaints). A global `Running`
  bool ends the loop. Casey's shutdown stance: don't carefully free everything on
  exit — the OS reclaims it all anyway; clean shutdown paths are for things with
  side effects, not memory.

### SDL3 translation

`SDL_Init(SDL_INIT_VIDEO)` → `SDL_CreateWindow(title, w, h, flags)` →
`while (running) { while (SDL_PollEvent(&e)) {...} }` → handle `SDL_EVENT_QUIT`
(also sent when the last window closes) and optionally
`SDL_EVENT_WINDOW_CLOSE_REQUESTED`. A quit key (Esc) on `SDL_EVENT_KEY_DOWN`.
`SDL_Quit()` at the end.

### Status note

`code/sdl_platform.cpp` already does this (Lazy Foo-style: window, poll loop, quit
event, key events). Two deltas from the HH shape, both erased by Quest 2 anyway: it
uses the window-surface API (`SDL_GetWindowSurface`/`SDL_UpdateWindowSurface`), which
Quest 2 replaces with renderer + streaming texture, and there's a leftover unused
`g_hello_world` surface. Don't polish it — move on.

### Done when

- [x] Window opens, stays open, closes via X and via a key
- [x] Dead tutorial code (`g_hello_world`, unused includes `<atomic>`, `<string>`) removed when Quest 2 rewrites the file

---

## Quest 2 — Pixel buffer + weird gradient (Days 3–5)

**Goal:** a CPU-owned block of pixels the game draws into every frame, blitted to the
window by SDL. This buffer is the foundation of the entire software renderer.

### What Casey built

- **Day 3 (backbuffer):** allocate a width×height array of 32-bit pixels; describe it
  to Windows with a `BITMAPINFO` (32 bits per pixel even though only 24 carry color —
  the 4th byte is padding for alignment; **negative height** to make the bitmap
  top-down so row 0 is the top of the screen); blit with `StretchDIBits`. Recreated
  the buffer on `WM_SIZE`.
- **Day 4 (rendering + animating):** the canonical pixel loop, which you will write a
  hundred variants of:
  - `u8 *row = buffer.memory;` — loop over y — `u32 *pixel = (u32 *)row;` — loop over
    x — write `*pixel++` — `row += buffer.pitch;`
  - **Pitch** = bytes from one row's start to the next. For your own buffer it equals
    `width * 4`, but pass pitch everywhere anyway — buffers you didn't allocate
    (textures, bitmaps) have padded rows, and code written against pitch never breaks.
  - **Pixel format:** Windows DIBs are, in memory byte order, `BB GG RR xx` — i.e. the
    32-bit value read on a little-endian machine is `0x00RRGGBB`. The gradient:
    blue channel from x, green from y, offset by animation variables →
    `*pixel++ = (green << 8) | blue;` with `u8` truncation giving the tiling.
  - Switched the message loop from blocking `GetMessage` to non-blocking
    `PeekMessage`, because a game renders continuously instead of waiting for events:
    drain all pending events, then do one frame, repeat. (Your SDL loop already has
    this shape — `SDL_PollEvent` is the non-blocking drain.)
  - Allocated the pixel memory with `VirtualAlloc` instead of `malloc`/`new` (reserve
    + commit pages straight from the OS; freed with `VirtualFree`), starting the
    "talk to the OS directly for memory" habit.
  - Introduced the HH typedef/define block you'll want too: `u8/u16/u32/u64`,
    `i8...i64`, `f32/f64` (his: `uint8`, `real32`), and
    `#define internal static`, `#define local_persist static`,
    `#define global_variable static` to disambiguate the three meanings of `static`.
- **Day 5 (graphics review + cleanup):** stopped resizing the buffer with the window.
  The backbuffer is now **fixed-size** (his: 1280×720), allocated once; window resizes
  just stretch it at blit time. Bundled it into a struct — `{void *Memory; int Width;
  int Height; int Pitch;}` — instead of loose globals. Reviewed everything from days
  3–4 slowly (this is the day to check if days 3–4 left gaps).

### SDL3 translation

- Create once at startup: `SDL_CreateRenderer(window, NULL)`, then
  `SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
  SDL_TEXTUREACCESS_STREAMING, 1280, 720)`.
  `SDL_PIXELFORMAT_ARGB8888` names the packed 32-bit value, so on little-endian its
  memory byte order is `BB GG RR AA` — **exactly HH's format**; every HH pixel-math
  snippet works unchanged.
- Allocate the pixel memory yourself (the game must own it — the whole point). HH
  parity on mac/Linux is `mmap(0, size, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANON,
  -1, 0)` / `munmap` (this also previews Quest 6's memory work). `malloc` works too;
  mmap is the faithful choice.
- Per frame: game writes pixels → `SDL_UpdateTexture(texture, NULL, buffer.memory,
  buffer.pitch)` → `SDL_RenderClear(renderer)` → `SDL_RenderTexture(renderer,
  texture, NULL, NULL)` (NULL dst = stretch to window) → `SDL_RenderPresent(renderer)`.
- Alternative to `SDL_UpdateTexture`: `SDL_LockTexture` hands you SDL's pixels+pitch
  to write into directly (one copy fewer, but then SDL owns the memory and dictates
  the pitch). Handmade Penguin discusses both; UpdateTexture into your own buffer is
  the right call here because the game layer must render into memory the platform
  hands it, not into an SDL object.
- Resize: with a fixed buffer there is nothing to reallocate — the render-stretch
  absorbs it. (Handle `SDL_EVENT_WINDOW_RESIZED` only if you later do 1:1 blits.)
- **macOS HiDPI:** window size in *points* ≠ size in *pixels*. The renderer handles
  scaling for you with the stretch blit, but any time you need the true pixel size
  (1:1 blit, mouse math later), use `SDL_GetWindowSizeInPixels`, not
  `SDL_GetWindowSize`.

### Gotchas

- Byte-order confusion is the classic bug here: if your gradient comes out with
  swapped colors, you wrote `0xRRGGBB`-style values into a format that isn't ARGB8888,
  or vice versa. Fix the format, don't swizzle your math.
- Animate by incrementing offsets once per frame *outside* the pixel loop; the `u8`
  wraparound in the channel math is what makes the gradient tile and scroll.
- Delete the `SDL_GetWindowSurface` path entirely; mixing the surface API and the
  renderer API on one window doesn't work.

### Done when

- [x] Fixed 1280×720 buffer, allocated once with mmap, described by a struct with pitch
- [x] Animated gradient scrolls smoothly; colors are what the math says they are
- [x] Resizing the window stretches the image; no allocation happens per frame
- [ ] Typedefs (`u8`…`f32`) and `internal`/`global_variable` defines in place

---

## Quest 3 — Gamepad + keyboard basics (Day 6) — Input part 1

**Goal:** read a gamepad and the keyboard; prove it by moving the gradient. (The
*normalized input abstraction* — half-transition counts, deadzones, unified
keyboard-as-controller — is Quest 6; today is raw device access.)

### What Casey built

- **XInput polling:** each frame, `XInputGetState(i, &state)` for each possible
  controller; `state.Gamepad` has `wButtons` (bitmask: DPAD_*, START, BACK, A/B/X/Y,
  shoulders), `sThumbLX/LY` (`SHORT`, −32768..32767), `bLeftTrigger/bRightTrigger`
  (0–255). Polling, not events — you sample the current state each frame.
- **The dynamic-loading pattern** (the most reusable lesson of the day): linking
  xinput.lib directly means the exe *fails to launch* on machines missing the DLL.
  Instead: a macro defines the function signature once; from it, a typedef, a stub
  implementation returning "device not connected", and a global function pointer
  initialized to the stub; `LoadLibrary` tries `xinput1_4.dll`, then `xinput1_3.dll`,
  then `xinput9_1_0.dll`, and `GetProcAddress` swaps the real function in if found.
  The program *degrades* instead of dying. **This exact pattern is how you'll load
  your own game library in Quest 9** — that's why it matters even though SDL hides
  the XInput dance itself.
- **Keyboard:** `WM_KEYDOWN/WM_KEYUP/WM_SYSKEYDOWN/WM_SYSKEYUP` in the window proc.
  `wParam` is the virtual key code; `lParam` bit 30 = key was previously down, bit 31
  = key is being released, so `WasDown != IsDown` filters out auto-repeat. Bit 29 =
  Alt held → he implemented Alt+F4 manually (bypassing `DefWindowProc` for key
  messages loses the default handling).
- **Rumble:** `XInputSetState` with `XINPUT_VIBRATION` motor speeds, as a toy.

### SDL3 translation

- **Hot-plug via events** (SDL solves what XInput's poll-any-slot approach papered
  over): on `SDL_EVENT_GAMEPAD_ADDED` → `SDL_OpenGamepad(e.gdevice.which)`, store the
  `SDL_Gamepad *`; on `SDL_EVENT_GAMEPAD_REMOVED` → `SDL_CloseGamepad`. Init with
  `SDL_INIT_GAMEPAD` added to `SDL_Init`.
- **Poll each frame, HH-style:** `SDL_GetGamepadButton(gp, SDL_GAMEPAD_BUTTON_SOUTH)`
  (SOUTH/EAST/WEST/NORTH = A/B/X/Y positions), `SDL_GetGamepadAxis(gp,
  SDL_GAMEPAD_AXIS_LEFTX)` — same −32768..32767 range as XInput, so HH's stick math
  ports verbatim. Triggers are axes too (`SDL_GAMEPAD_AXIS_LEFT_TRIGGER`).
- **Keyboard:** `SDL_EVENT_KEY_DOWN`/`SDL_EVENT_KEY_UP`; `e.key.down` (bool),
  `e.key.repeat` (skip these — SDL gives you the bit-30 dance for free), `e.key.key`
  (symbolic keycode, layout-dependent) vs `e.key.scancode` (physical position). Use
  **scancodes for WASD-style movement** so non-QWERTY layouts keep the physical
  diamond, keycodes for symbolic keys (Esc, L for the loop key later).
- **Rumble:** `SDL_RumbleGamepad(gp, low_freq, high_freq, duration_ms)` — do it once
  on a button press for the same grin Casey got.
- Alt+F4 needs nothing: SDL delivers `SDL_EVENT_QUIT`.

### Done when

- [x] Gradient scrolls with left stick and with WASD/arrow keys
- [ ] Plugging and unplugging the pad mid-run works (no crash, control resumes)
- [ ] Auto-repeat filtered (holding a key doesn't spam transitions)
- [ ] A button triggers rumble

---

## Quest 4 — Square wave → sine wave (Days 7–9) — Audio part 1

**Goal:** continuous tone out of the speakers, pitch tied to input so you can *feel*
latency. Format decision, made now and kept forever: **48000 Hz, 16-bit signed,
2 channels, interleaved LR LR** — 4 bytes per stereo sample-pair.

### What Casey built

- **Day 7 (DirectSound init):** load `dsound.dll` dynamically (same stub pattern as
  XInput), create the DirectSound object, `SetCooperativeLevel`, then two buffers: a
  *primary* buffer whose only modern purpose is declaring the output format (a relic
  of hardware-mixing days), and a *secondary* buffer — a 1-second circular buffer
  (48000 × 4 = 192,000 bytes) you write samples into while the card plays it in a
  loop. Format via `WAVEFORMATEX`: PCM, 2 channels, 48 kHz, 16-bit, block align =
  channels × bits/8 = 4. Plus a tour of COM vtables to explain the `->lpVtbl` calls.
- **Day 8 (square wave):** the circular-buffer write dance: `Lock(byteToLock,
  bytesToWrite, ...)` returns **two regions** (write range may wrap past the buffer
  end); fill both, `Unlock`. A `u32 RunningSampleIndex` counts stereo pairs forever;
  `byteToLock = (RunningSampleIndex * 4) % bufferSize`. Square wave at 256 Hz:
  period = 48000/256 = 187 samples; flip between +volume and −volume (~3000 of
  32767) every half period; write the same value to L and R. `Play(...DSBPLAY_LOOPING)`.
- **Day 9 (sine + latency):** replace the flip with `sinf`. Keep a running phase
  `tSine`, advanced by `2π / samplesPerPeriod` per sample — **accumulate phase,
  don't recompute from RunningSampleIndex**, so changing frequency mid-stream stays
  click-free. Introduced **latency control**: stop filling the whole buffer; only
  write ahead of the play cursor by `latencySampleCount = 48000/15` (~3200 samples ≈
  67 ms). Tied `toneHz` to the stick (256↔512) — the delay between moving the stick
  and hearing the pitch change *is* your latency, now audible.

### SDL3 translation

SDL3's push-stream model replaces the entire circular-buffer apparatus. **No ring
buffer, no Lock/two-regions, no play cursor, no 1-second backing buffer.** (This is
also why Handmade Penguin's audio chapters don't apply — SDL2 only had a pull
callback, so Gow rebuilt DirectSound's ring buffer on top of it. Skip all that.)

- Init: `SDL_AudioSpec spec = { SDL_AUDIO_S16, 2, 48000 };`
  `SDL_AudioStream *stream = SDL_OpenAudioDeviceStream(
  SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);` — one call opens the
  default device and binds a converting stream (device runs at whatever format it
  likes; SDL converts from yours). The device starts **paused**:
  `SDL_ResumeAudioStreamDevice(stream);` (add `SDL_INIT_AUDIO` to `SDL_Init`).
- Per frame:
  1. `int queued = SDL_GetAudioStreamQueued(stream);` — bytes you've pushed that
     haven't been consumed yet. **This number is your latency.**
  2. `int target = targetQueuedBytes;` — start at ~3 frames of audio:
     48000/60 × 3 × 4 = 9600 bytes ≈ 50 ms. (Tuning it down is Quest 8.)
  3. If `queued < target`: generate `target − queued` bytes of sine into a scratch
     `i16` buffer (keep `RunningSampleIndex` and `tSine` exactly as HH does), then
     `SDL_PutAudioStreamData(stream, scratch, bytes)`.
- What survives from days 7–9 unchanged: the format decision, interleaved LR writing,
  RunningSampleIndex, phase-accumulated sine, square-first-then-sine as the debugging
  order (a square wave that sounds wrong is easier to reason about), and
  pitch-on-stick as the latency probe.

### Gotchas

- Wrap `tSine` (subtract 2π when it exceeds it); a forever-growing float loses
  precision and the tone goes gritty after a while.
- If you hear periodic clicks: you're either recomputing phase from scratch on
  frequency change, or your queue is running dry (`queued` hitting 0) — print it.
- `SDL_GetAudioStreamQueued` reports bytes *in the stream*; the device buffer adds a
  little on top (`SDL_GetAudioDeviceFormat` tells you its `sample_frames`). Ignore
  until Quest 8.

### Done when

- [x] Clean continuous sine, no clicks, survives window drags
- [x] Stick moves pitch between 256 and 512 Hz; delay is short and *perceptible* —
      you know what ~50 ms feels like now
- [x] Volume at a sane level (~3000/32767), not full-scale

---

## Quest 5 — Frame timing readout (Day 10) — Timing part 1

**Goal:** know your ms/frame at all times, from now until the project dies.

### What Casey built

`QueryPerformanceFrequency` once at startup, `QueryPerformanceCounter` at the top of
the loop; ms/frame = `1000 × (end − start) / frequency`; FPS = the reciprocal. Also
`__rdtsc()` for raw CPU cycles per frame ("megacycles"), with the distinction spelled
out: **the wall clock (QPC) is for time — pacing, dt; the cycle counter (rdtsc) is
for cost — profiling work done.** Printed `X ms/f, Y fps, Z Mc/f` every frame via
`OutputDebugString`.

### SDL3 translation

- `SDL_GetPerformanceCounter()` / `SDL_GetPerformanceFrequency()` — identical shape,
  `Uint64`. (`SDL_GetTicksNS()` also exists if you'd rather have nanoseconds
  directly.)
- Cycles: `__rdtsc` is x86-only. On Apple Silicon use
  `clock_gettime_nsec_np(CLOCK_UPTIME_RAW)` or just skip the cycle counter and keep
  ms — the wall-clock number is the one that matters for this quest.
- Print with `SDL_Log`/`printf` every frame (or a 30-frame rolling average if the
  spam annoys you). Later this becomes an on-screen overlay; console is fine for
  months.

### Done when

- [x] Live ms/frame + FPS readout while the gradient runs
- [x] You've noted the current number — this is your baseline before the frame-rate
      lock in Quest 7 (expect it to be silly-fast right now)

---

## Quest 6 — The platform↔game split (Days 11–15, 17) — the big one

**Goal:** `game.h` exists and is the *entire* world as far as game code is concerned.
Platform passes in: memory, input, a backbuffer, a sound buffer. Game passes back:
pixels and samples. No SDL header, no OS header, no allocation south of the boundary.
This is 4–8 hours; the sub-parts (a–f) are each one sitting. HH keeps everything in
one translation unit for now — `#include "game.cpp"` at the top of the platform file —
and the actual shared-library split waits until Quest 9. Do the same; it means you can
restructure freely without touching the build.

### a) Day 11 — API design philosophy + first boundary

Two ways to architect a platform layer: the platform is a set of services the game
calls (game says "give me a window, blit this") — or **the game is a service the
platform calls** (platform owns `main`, the loop, the OS; once a frame it calls the
game with everything it needs). HH picks the second for the frame path: it keeps OS
grossness out of game code, and it's what makes Quests 9–10 (reload, replay) almost
free. A small back-channel of platform services (file I/O, part e) goes the other
way, as function pointers — not linked calls.

Concretely: create `game.h` + `game.cpp`; define `game_offscreen_buffer {void
*Memory; int Width; int Height; int Pitch;}`; move the gradient render into
`GameUpdateAndRender(...)`; platform fills the struct from its own buffer each frame
and calls in. The platform file keeps *its* buffer struct separately — same fields
today, but platform-side may grow SDL details; the two structs are allowed to drift.

### b) Day 12 — sound crosses the boundary

`game_sound_output_buffer {int SamplesPerSecondl; int SampleCount; i16 *Samples;}`
(48000 Hz, 16-bit stereo interleaved — same format as Quest 4). Flow: **platform**
computes how many samples this frame needs (your queue math from Quest 4: target
minus queued, ÷4 = sample count), points `Samples` at a platform-owned scratch buffer
(allocate once at init, size it for the worst case, e.g. 2 seconds), calls the game;
**game** writes exactly `SampleCount` samples (the sine generator and its `tSine`
move into game state); platform pushes the result to SDL. Two exported entry points
from here on: `GameUpdateAndRender(memory, input, backbuffer)` and
`GameGetSoundSamples(memory, soundbuffer)` (HH formalizes the second one at the DLL
split, but shape it now).

### c) Day 13 — input crosses the boundary

The structs, which outlive everything else in this doc:

- `game_button_state { int HalfTransitionCount; bool EndedDown; }` —
  **half-transition count** = how many down↔up flips happened during the frame.
  `EndedDown` alone loses a press+release inside one slow frame; the count doesn't.
  "Was it pressed this frame?" = `EndedDown && HalfTransitionCount > 0` (or count ≥ 2).
- `game_controller_input` — `bool IsAnalog;` stick values; and the union trick:
  a named struct of buttons (`Up, Down, Left, Right, LeftShoulder, RightShoulder`,
  growing in part f) overlaid with `game_button_state Buttons[N]` so code can address
  buttons by name *or* iterate them. Static-assert the array length matches the
  struct.
- `game_input { game_controller_input Controllers[5]; }` (his count: keyboard + 4
  pads — see part f).
- **Double-buffered input:** platform keeps `Input[2]`, `NewInput`/`OldInput`
  pointers, swapped each frame. New frame starts by deriving from old (`EndedDown`
  carries over, transition counts zero), then events/polling mutate `NewInput`. The
  game receives a controller's current state *and* implicitly its history.

### d) Day 14 — memory crosses the boundary

- `game_memory { u64 PermanentStorageSize; void *PermanentStorage;
  u64 TransientStorageSize; void *TransientStorage; bool IsInitialized; }`
- **One allocation at startup**, total = permanent + transient;
  `TransientStorage = (u8 *)PermanentStorage + PermanentStorageSize`. His sizes:
  permanent 64 MB, transient 1–4 GB — generous is free, it's virtual pages, and
  running out of memory is a *startup* failure, never a runtime one.
- Size macros with the lesson attached: `#define Kilobytes(v) ((v) * 1024LL)` etc. —
  `Gigabytes(4)` in 32-bit int math overflows to 0; the `LL` (or a `(u64)` cast in
  the chain) is load-bearing.
- mac/Linux: `mmap(BaseAddress, totalSize, PROT_READ|PROT_WRITE,
  MAP_PRIVATE|MAP_ANON, -1, 0)`. In internal/debug builds pass a **fixed base
  address hint** (his: 2 TB) and assert you got it — pointers inside the block then
  have identical values across runs, which Quest 10's memory-snapshot replay silently
  depends on. Don't use `MAP_FIXED` (it will happily map over something that's
  already there); the plain hint works on macOS in practice. Anonymous mmap is
  zero-filled by the OS — rely on that, as HH relies on `VirtualAlloc` zeroing.
- Game side: `game_state *state = (game_state *)memory->PermanentStorage;` at the
  top of `GameUpdateAndRender`; `if (!memory->IsInitialized) { …seed…;
  IsInitialized = true; }`. **All game state lives in this block.** No game-side
  globals, no game-side allocation — this single rule is what makes hot reload and
  replay work later, so violations are architecture bugs even when they run fine.
- Support macros from the same day: `Assert(expr)` as `if (!(expr)) { *(int *)0 = 0; }`
  gated by a `SLOW` build define; `ArrayCount(a)`.

### e) Day 15 — debug file I/O

Three platform functions so game code can touch files before a real asset system
exists (months away):

- `DEBUG_platform_read_entire_file(name)` → `{ u32 ContentsSize; void *Contents; }` —
  open, get size, allocate, read *whole file*, close. `SafeTruncateU64` asserts the
  u64 file size fits u32.
- `DEBUG_platform_free_file_memory(ptr)`
- `DEBUG_platform_write_entire_file(name, size, memory)` → bool.

Plain POSIX (`open`/`read`/`write`/`close` or stdio) in the platform file. Passed to
the game as **function pointers inside `game_memory`** (HH wires the pointers up at
the DLL split; doing it now costs nothing and saves rework in Quest 9). The `DEBUG_`
prefix and a `BUILD_INTERNAL` compile gate are the honesty mechanism: these block,
they allocate, they have no error strategy — they must not survive into shipping
code paths. Test: read `sdl_platform.cpp` itself and write a copy back out.

### Day 16 aside — compiler switches (no quest)

An MSVC flag tour; the portable takeaways are the flags already planned for this
project: `-Wall -Wextra -Werror` (minus a couple of noisy ones), `-fno-exceptions
-fno-rtti`, `-g -O0` debug / `-O2` fast, defines `BUILD_INTERNAL`/`BUILD_SLOW`.
The CMakeLists doesn't set any of these yet — add them when you next touch it.

### f) Day 17 — unified input: keyboard becomes a controller

- `Controllers[0]` = keyboard, `[1..4]` = gamepads. Helper
  `GetController(input, i)` with a bounds assert instead of raw indexing.
- Keyboard events write straight into controller 0's `game_button_state`s via one
  helper — `ProcessKeyboardMessage(&state, isDown)`: sets `EndedDown`, increments
  `HalfTransitionCount`. Keyboard controller **carries over** across the frame swap
  (copy old→new at frame start, zero the transition counts); pads are re-polled from
  scratch.
- Button set finalized-ish: `MoveUp/MoveDown/MoveLeft/MoveRight` +
  `ActionUp/ActionDown/ActionLeft/ActionRight` (face buttons) + shoulders +
  `Start/Back`.
- **Deadzone + normalization** (XInput's documented left-thumb deadzone constant is
  7849 of 32768; with SDL the same ~24% is a fine start, tune by feel):
  below −dz → `(v + dz) / (32768 − dz)`, above +dz → `(v − dz) / (32767 − dz)`,
  inside → 0. This maps to −1..1 **without a cliff at the deadzone edge** — the
  naive `v/32767`-then-clamp version makes small movements feel notchy.
- Stick also *synthesizes* digital presses: |average| > 0.5 → treat as the
  corresponding Move* button transition, so game code may treat everything as
  buttons; `IsAnalog` tells it whether the stick data is real.

### Done when

- [x] `game.cpp` includes only `game.h`; `grep -c SDL code/game.cpp` returns 0
- [x] Gradient + sine both live in game code, driven through the four structs
- [x] Keyboard and gamepad both arrive via `Controllers[]`, half-transitions counted,
      deadzone applied
- [x] Memory block allocated once at the fixed hint address; game state cast from
      permanent storage; `IsInitialized` pattern in place; zero game-side globals
- [x] DEBUG file read/write round-trips a real file
- [x] The boundary test passes mentally: for each line in the platform file ask
      "would this change if SDL were swapped for Win32?" — yes = stays, no = move it

---

## Quest 7 — Enforced frame rate (Day 18) — Timing part 2

**Goal:** the game updates at a fixed, chosen rate; dt is known, not measured noise.

### What Casey built

- Chose `GameUpdateHz` from the monitor (his: 60 Hz display ÷ 2 = 30 for the game —
  2015 laptop; **you target 60**, or the display's rate via
  `SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window))->refresh_rate`).
  `TargetSecondsPerFrame = 1.0f / GameUpdateHz`.
- After update+render, measure elapsed wall-clock; while under target: sleep, then
  spin. The Windows-specific pain: `Sleep` granularity is the scheduler tick, so
  `timeBeginPeriod(1)` + sleep for *slightly less than needed* + busy-spin the
  remainder. Missed frames aren't hidden: log/flag them.
- Passed the frame's dt to the game (`dtForFrame` in `game_input` — fixed at target,
  since the rate is enforced).

### SDL3 translation

`SDL_DelayPrecise(ns)` does the sleep-then-spin dance internally (in your 3.5.0) —
the day-18 mechanism in one call. Keep HH's *structure* anyway: measure with
`SDL_GetPerformanceCounter`, compute remaining ns, delay, re-measure, log misses.
Vsync (`SDL_SetRenderVSync`) is the rival approach — HH's position: own your timing;
vsync ties you to whatever the display does and can't be reasoned about with the
audio math in Quest 8. One authority for pacing, and it's you.

### Done when

- [x] Readout says 16.6 ms steady (±0.5); gradient scroll speed is identical on
      every run
- [x] Deliberately heavy frame (add a busy loop) → miss is *logged*, not silently
      absorbed
- [ ] `dtForFrame` reaches game code

---

## Quest 8 — Audio latency + sync (Days 19–20) — Audio part 2

**Goal:** lowest stable latency, *measured* — and a debug view so audio timing is
seen, not guessed. Two of HH's hardest days; SDL3 deletes most of the pain but keep
the ideas.

### What Casey built

- **Day 19:** DirectSound exposes a play cursor (device reading here) and a write
  cursor (unsafe before here, ~30 ms ahead, coarse granularity). The frame must
  decide `byteToLock`/`bytesToWrite` against a *future* boundary: compute
  `ExpectedSoundBytesPerFrame = 48000×4 / GameUpdateHz`; if the card is low-latency
  (write cursor lands before the next frame boundary) → write exactly up to the next
  frame boundary (audio locked to video, minimal delay); else → write a frame plus a
  **safety margin** (`SafetyBytes`, derived from observed write-cursor jitter).
  Also: the sound card's clock drifts relative to the wall clock, so everything is
  recomputed per frame from cursor reality, never assumed.
- **Day 20:** debug visualization — colored vertical lines drawn into the backbuffer
  marking play/write cursor byte positions for the last ~15 frames, plus the
  expected-frame-boundary marker; printed latency in ms
  (`(writeCursor − playCursor)` → seconds). The jitter, drift, and latency became
  *visible*, and the day-19 logic was debugged against the picture.

### SDL3 translation

The stream queue replaces both cursors: **latency ≈ bytes queued in the stream**
(`SDL_GetAudioStreamQueued`) plus the device's own period
(`SDL_GetAudioDeviceFormat` → `sample_frames`, typically a few ms). Your Quest 4
loop already targets a queue depth; this quest is tuning and instrumenting it:

1. Compute per-frame need as `ExpectedSoundBytesPerFrame = 48000×4 / GameUpdateHz`
   (= 3200 bytes at 60 Hz) and generate in those units — audio stays frame-aligned,
   HH-style.
2. Tune `targetQueuedBytes` down from Quest 4's ~3 frames until it crackles
   (underrun: `queued` hits 0), then back off ~2×. Around 2 frames ≈ 33 ms total is
   typical; machine-dependent.
3. Port day 20's spirit: track `queued` (in ms) per frame — min/max over the last 30
   frames printed with the fps readout, or literal HH-style marker lines drawn into
   the backbuffer if you want the picture. Watch it breathe during window drags and
   deliberately-heavy frames.
4. Log every underrun loudly. An underrun you didn't notice becomes a click you
   can't reproduce.

### Done when

- [x] Latency displayed live, in ms, and you know your number
- [x] Pitch change on stick feels immediate compared to Quest 4's 50 ms
- [x] Zero underruns during normal play; heavy-frame test recovers cleanly

---

## Quest 9 — Hot reload (Days 21–22)

**Goal:** edit `game.cpp` while the engine runs; see the change in ~1 second with all
state intact. **Gate quest: do not proceed until this is boringly reliable — every
quest after (and every month after) leans on it.**

### What Casey built

- **Day 21:** game code becomes a DLL. Exports: `GameUpdateAndRender`,
  `GameGetSoundSamples`. Signatures defined once via macro —
  `#define GAME_UPDATE_AND_RENDER(name) void name(game_memory *Memory, game_input
  *Input, game_offscreen_buffer *Buffer)` — from which the typedef, the game's
  definition, and a platform-side **stub** all derive, so they can't drift. A
  `win32_game_code` struct holds the module handle, the two function pointers
  (initialized to stubs — the program limps instead of crashing when the DLL is
  mid-rebuild), a `LastWriteTime`, and `IsValid`.
- **The two load rules** (both bite for real):
  1. **Copy the library, load the copy** (`handmade.dll` → `handmade_temp.dll`).
     Loading the original locks it (compiler can't relink), and loaders cache by
     path — re-loading a path you loaded before can hand back the stale image.
  2. On reload: unload, **null every pointer into the old image**, copy, load, look
     up again. Any surviving pointer into the old module is a use-after-free with a
     delay fuse.
- Poll the DLL's file write-time each frame (`GetFileAttributesEx` +
  `CompareFileTime`); changed → reload. **Game state survives because it lives in
  the platform's `game_memory` block** — the DLL's own globals *reset* on every
  reload, which is why the no-globals rule from Quest 6d is enforced by reality, not
  discipline.
- **Day 22:** making it instant + fixing Windows-specific locks — the debugger holds
  the PDB hostage, so: randomized PDB filename per build + `del *.pdb`; plus
  guarding against loading a half-written DLL (a lock/sentinel file the build script
  creates first and deletes last; loader skips reload while it exists). Demo:
  tweaking jump gravity live. This day is why HH's iteration speed is famous.

### SDL3 / macOS translation

- Build `game.cpp` as `game.dylib`: new CMake target `add_library(game SHARED
  code/game.cpp)` — **no SDL linked**, which mechanically enforces the Quest 6
  boundary. Rebuild just it with `cmake --build build --target game`.
- Exports need `extern "C"` (clang mangles C++ names; `dlsym` wants the exact
  string). `dlopen(path, RTLD_NOW | RTLD_LOCAL)` / `dlsym` / `dlclose`.
- Mod time: `SDL_GetPathInfo(path, &info)` → `info.modify_time` — portable, no
  stat-struct #ifdefs.
- Copy-then-load still mandatory, with one macOS upgrade: **use a unique temp name
  per reload** (counter or timestamp suffix), because dyld may return the cached
  image for a previously-seen path even after `dlclose`. Unique names sidestep the
  whole question. Delete old temps opportunistically.
- macOS caveat: `dlclose` can silently not-unload a library that uses thread-locals
  or Objective-C. Plain C-style C++ unloads fine — one more reason the game layer
  stays plain.
- The half-written-file race exists here too (copy can catch the linker mid-write):
  if `dlopen`/`dlsym` fails, keep the stubs, retry next frame. That plus unique
  names replaces day 22's lock-file, though the lock-file works too.
- No PDB equivalent (clang debug info lives in object files / dSYM); lldb attach
  works across reloads.

### Done when

- [x] Engine running → change a constant in the gradient → rebuild target `game` →
      change visible in under ~2 s
- [x] Gradient offsets and sine phase carry across the reload (state intact)
- [x] 50 consecutive reloads: no crash, no growing temp-file pile
- [x] Rebuild-while-frame-in-flight (spam builds) never kills the engine — worst
      case is stub frames

---

## Quest 10 — Looped live editing + mouse + cleanup (Days 23–25)

**Goal:** record a stretch of input, replay it as a perfect infinite loop, and edit
game code *while it loops*. Highest-leverage debug tool in the entire HH toolkit:
any bug becomes a repeatable ten seconds you can iterate on live.

### What Casey built

- **Day 23 (the loop):** a state struct tracks
  `InputRecordingIndex`/`InputPlayingIndex` + two file handles. The `L` key cycles:
  - **Begin recording:** create a scratch file (his: `foo.hmi`); write the **entire
    game memory block** once (this is the snapshot — legal only because *all* state
    is in the block, Quest 6d); then each frame append the frame's `game_input`.
  - **Begin playback:** restore the memory block from the file, then each frame read
    the next recorded `game_input` and hand it to the game instead of live input.
    Hitting EOF → restore + rewind again: a seamless loop, forever.
  - Why it's exact: one frame is `newMemory = Game(memory, input)` — deterministic.
    Same starting block + same input sequence = same run, every time. (This is also
    the quiet argument for keeping `rand()`-style hidden state out of game code —
    randomness belongs in game state where the snapshot catches it.)
  - Combined with Quest 9: loop runs, you edit code, rebuild — the same recorded
    input now drives the *new* code. Casey tuned player movement this way for years.
- **Day 24 (cleanup):** paths. The DLL, temp DLL, and recording files were resolving
  against the CWD; fixed by getting the exe's own directory and building full paths
  next to the binary. Assorted replay hardening.
- **Day 25 (mouse + replay buffers, prototype declared done):**
  - Mouse into `game_input`: cursor position + 5 buttons (through the same
    `ProcessKeyboardMessage`-style helper), intended for debug UI more than gameplay.
  - Replay snapshot moved from write-the-block-to-disk to **memory-mapped file
    copies** (`CreateFileMapping`/`MapViewOfFile` + `CopyMemory`) with multiple
    numbered slots — snapshotting a multi-GB block via WriteFile stalls; a mapped
    copy doesn't. Input still streams to per-slot files.
  - End of day 25: the Win32 prototyping layer is **done**. Day 26 starts the game.

### SDL3 translation

- Recording: stdio (`fopen`/`fwrite`/`fread`) is plenty. `L` keycode via
  `e.key.key == SDLK_L`.
- Snapshot: simplest correct version is a second anonymous `mmap` block +
  `memcpy` at record-start and every loop-restart. At 64 MB permanent + 1 GB
  transient the loop-point memcpy is a one-time ~100–200 ms hitch — live with it, or
  shrink transient storage while prototyping, or do HH's day-25 file-backed-mmap
  version if it bothers you. Input always streams to a file either way.
- Paths: `SDL_GetBasePath()` returns the executable's directory — build dylib/temp/
  replay paths from it, never from CWD (day 24 in one call).
- Mouse: `SDL_GetMouseState(&x, &y)` (floats, window coords, plus a button mask) —
  **map window coords to backbuffer coords** (window and 1280×720 buffer differ; a
  divide by the stretch factor) before handing to the game. Buttons through the same
  button-state helper as everything else.

### Done when

- [x] `L` starts recording (visible indicator), `L` again loops it; loop runs
      untouched for minutes, perfectly repeating
- [x] Edit game code mid-loop → same recorded input drives new behavior
- [x] All engine-adjacent files (dylib copies, replay files) land next to the
      executable regardless of launch directory
- [x] Game draws something at the mouse position (proves coords + mapping)

---

## After Quest 10

The platform layer is done in the day-25 sense: HH then writes *game* for hundreds of
episodes, touching the platform only occasionally — a thread/job queue (~day 122+),
GPU-assisted blit and later real GPU rendering (~day 235+), fullscreen toggle, audio
mixing moving into the game. None of it blocks starting the game, and none of it
belongs in your head right now. Deferred list stands: GPU, asset pipeline,
multithreading, `build.bat`, engine generalization.

Next after this doc: whatever the game is. That's the point.
