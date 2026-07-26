# CLAUDE.md

Handmade-Hero-style game engine with an SDL3 platform layer. C-style C++, software
renderer, developed on macOS, eventually targeting Windows/Linux too. SDL is used
*only* as the platform layer (window, input, audio device, blitting a pixel buffer);
everything above that is written from scratch.

## The user writes the code

This is a learn-by-building project following the Handmade Hero platform-layer arc.
The roadmap is `docs/QUESTS.md` — read it for context on what's being built and in
what order. **The user implements the quests themselves; that is the point of the
project.** Your default role: answer questions, explain HH/SDL concepts, look up SDL3
API details, review code, fix build/tooling problems. Do not write platform- or
game-layer code unless explicitly asked to.

## Build & run

`build.sh` is the entry point. CMake is used only for SDL (first build compiles it
once, ~minutes; cached after) and for regenerating compile_commands.json; your own
code compiles as two direct clang++ invocations inside the script.

```
./build.sh --setup        # first build on a machine: cmake for SDL, then everything
./build.sh                # day-to-day: game dylib + platform executable
./build.sh -g             # game dylib only (hot-reload inner loop)
./build/game              # run
```

- SDL3 is vendored at `third_party/SDL/` (release source, version 3.5.0). Never edit
  anything inside it.
- SDL currently links as a shared dylib into `build/`. The original plan
  (docs/PROJECT_SETUP.md history) was static; treat this as an open decision, not a
  bug.
- `compile_commands.json` is symlinked from `build/` for clangd.
- `build.sh` sets the project flags (`-Wall -Wextra -Werror … -fno-exceptions
  -fno-rtti -DBUILD_INTERNAL=1 -DBUILD_SLOW=1`); `CMakeLists.txt` still compiles
  flagless — parity is a TODO item.
- Not a git repository yet.

## Layout

```
code/                 all hand-written code, flat, no subdirs
  sdl_platform.cpp    platform layer — owns main(), every SDL/OS call (executable)
  game.h              the platform↔game contract
  game.cpp            game layer — hot-reloaded shared lib (build/libgame.dylib)
docs/QUESTS.md        roadmap + per-quest notes distilled from HH days 1–25
third_party/SDL/      vendored SDL3 source — read-only
build/                all build output (would be gitignored)
```

Platform-specific files are prefixed with the platform (`sdl_platform.cpp`);
platform-independent files get no prefix. `code/` stays flat until that hurts.

## Architecture rules

These come from Handmade Hero and are the project's spine; flag violations when
reviewing:

1. **The platform calls the game, never the reverse.** Per frame the platform hands
   the game: a memory block, input, a backbuffer, a sound buffer. The game returns
   pixels and samples. The small back-channel (debug file I/O) goes through function
   pointers inside the memory struct, not linked calls.
2. **`game.cpp` includes only `game.h`.** No SDL headers, no OS headers, ever. The
   litmus test for where code goes: "would this line change if SDL were swapped for
   raw Win32?" Yes → platform file. No → game.
3. **Memory is allocated once, at startup.** One big block (permanent + transient),
   `mmap`ed by the platform, fixed base-address hint in debug builds. The game never
   allocates and keeps all state — no game-side globals — inside the block. Hot
   reload and input-replay both depend on this rule.
4. **No exceptions, no RTTI, no STL in engine code.** C-style C++: structs, functions,
   fixed-size types (`u8`/`i16`/`f32` typedefs — the typedef block itself is still a
   TODO; code uses `uint8_t`-style for now).

## Conventions

- Match Handmade Hero naming where it exists (`game_offscreen_buffer`,
  `HalfTransitionCount`, `DEBUG_platform_read_entire_file`) — the user cross-reads
  HH material constantly, so gratuitous renaming has a real cost.
- Debug-only platform services are `DEBUG_`-prefixed and gated behind
  `BUILD_INTERNAL`.
- Full rebuilds must stay fast (seconds); nothing gets added to the build that
  compromises that.

## Current status

All ten quests (`docs/QUESTS.md`) complete as of 2026-07-23 — the platform layer
works end to end: pixel buffer, audio, enforced 60 Hz, platform↔game split, hot
reload, input replay. Remaining bugs (audio underruns are the known one), unchecked
quest boxes (gamepad input), and the Windows/Linux ports are tracked in
`docs/TODO.md`.
