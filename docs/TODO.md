# TODO

Platform layer is quest-complete (QUESTS.md 1–10) as of 2026-07-23. This tracks
what's left: bugs found in the 07-23 review, unchecked quest boxes, and the
Windows/Linux ports. Work top to bottom within a section; sections are ordered
by urgency.

## 1. Fix now — audio + correctness bugs (½–1 evening)

- [x] **Audio scratchiness/underruns** — fixed 2026-07-23 by switching from
      per-frame push to SDL3's pull model (`SDL_SetAudioStreamGetCallback`):
      the audio thread now calls `game_get_sound_samples` itself whenever the
      device needs data, so main-thread stalls (startup, slow frames, window
      drags) can't starve it. Latency dropped from ~66 ms of queue to the
      device period (logged at startup). New contract: the main thread holds
      `SDL_LockAudioStream` while swapping game code or overwriting game
      memory — guards are in place at the reload and replay-memcpy sites.
- [x] **Replay data loss** — fixed 2026-07-23: the write handle is closed
      (flushing the recorded tail) before reopening for playback.
- [x] **Hot reload can wedge on stubs** — fixed 2026-07-23: `last_write_time`
      recorded only on successful load, so half-written dylibs retry next
      frame (verified: engine survives a corrupted `libgame.dylib` and
      recovers on the next good build); `system("cp")` → `SDL_CopyFile`;
      stale `libgame_temp_*.dylib` swept at startup (kept during a run so
      lldb can resolve symbols).
- [x] **Game-side globals** — fixed 2026-07-23: `tone_sine` and `tone_hz` live
      in `game_state` (phase survives hot reload, replay reproduces pitch);
      `running_sample_index` deleted; `game_get_sound_samples` lost its
      `tone_hz` parameter. Pitch is now driven by `action_down` (J key), not
      space — remap in the platform's keyboard table if you want space back.
- [x] **mmap hardening** — fixed 2026-07-23: all three mmaps checked for
      `MAP_FAILED`; game memory requests base `Terabytes(2)` in
      `BUILD_INTERNAL` builds (verified honored on this machine) and logs a
      loud warning if the hint is not honored, since snapshot replay of
      pointer-holding state silently depends on it.
**Standing constraint (not a task): sound generation runs on the audio
thread.** `game_update_and_render` (main) and `game_get_sound_samples` (audio)
both touch `game_state`. Today's sharing is one `int` (`tone_hz`, written by
main, read by audio) — benign. Rules until real game audio exists: fields the
sound path reads are plain values written whole; fields the sound path owns
(`tone_sine`) are never written by the update path; the sound path tolerates a
zeroed pre-init `game_state` (audio can run before frame 1); the main thread
holds `SDL_LockAudioStream` around swapping game code or overwriting game
memory. When gameplay-driven mixing arrives, this needs a real design (HH
moves mixing into the game with explicit sync).

## 2. Platform-layer gaps (unchecked QUESTS.md boxes)

- [ ] **Gamepad input** (Quest 3 — not started; only `SDL_INIT_GAMEPAD` exists):
      open/close on `SDL_EVENT_GAMEPAD_ADDED/REMOVED`, poll buttons + axes per
      frame, deadzone + normalize (~7849/32768), stick synthesizes digital
      Move* presses, rumble on a button. (1–2 evenings)
- [ ] **Unified input** (Quest 6c/f): `Controllers[5]` — keyboard as
      controller 0, pads 1–4; buttons as named-struct + array union with a
      static assert; `is_analog` flag. (1 evening)
- [ ] **Double-buffered input** (Quest 6c): old/new `game_input` pair swapped
      each frame; new frame derives from old (ended_down carries, transition
      counts zero via a loop over the union array — replaces the 14 hand-written
      zeroing lines at the top of the main loop).
- [ ] **`dt_for_frame` in `game_input`** (Quest 7) — the game currently has no
      time. Fixed at 1/60 since the rate is enforced.
- [ ] **Event-driven keyboard** (Quest 3/6f): switch from `SDL_GetKeyboardState`
      polling to `SDL_EVENT_KEY_DOWN/UP` with `e.key.repeat` filtered. Polling
      caps `half_transition_count` at 1 per frame (press+release inside one
      frame is lost — the exact case the counter exists for). Also lets the
      `L` replay key use the normal button helper instead of `l_was_down`.

## 3. Windows + Linux ports

- [ ] **De-POSIX `sdl_platform.cpp`** — the file currently uses `dlopen`/
      `dlsym`, `open`/`read`/`write`, `mmap`, `unistd.h`; none exist on
      Windows. SDL already wraps most of it portably: `SDL_LoadObject`/
      `SDL_LoadFunction`/`SDL_UnloadObject` for the dylib, `SDL_IOStream` (or
      stdio) for debug file I/O. Keep memory as the one real `#ifdef`
      (`mmap` / `VirtualAlloc`) since the fixed-base debug hint needs platform
      code anyway. Doing this on macOS first makes the ports mostly build work.
- [ ] **Per-platform library naming**: `"libgame.dylib"` is hardcoded twice in
      `sdl_platform.cpp` → one constant switching `.dylib`/`.so`/`.dll`.
- [ ] **Linux build**: extend `build.sh` (uname branch): `-fPIC -shared` for
      the game lib, `-Wl,-rpath,$ORIGIN`, `.so` names. SDL needs X11/Wayland
      dev headers installed to *build* — the one non-vendored prerequisite;
      document it.
- [ ] **Windows build**: `build.bat` mirroring build.sh (cl.exe or clang-cl;
      SDL builds with the same CMake). Flags translate: `/W4 /WX /GR- /EHa-`.
- [ ] Boot each port and re-run the Quest 9/10 checklists there (hot reload
      and replay are the likely breakage points).

## 4. Cleanup / quality

- [x] **`git init` + `.gitignore`** — done 2026-07-26. `main` is the
      integration branch; topic branches (`imgui`, `windows-port`) merge into
      it. `.gitignore` covers `build/`, `.cache/`, `compile_commands.json`,
      the replay/hot-reload runtime artifacts, and `imgui.ini`.
- [ ] Rename the global `close()` in `sdl_platform.cpp` — it overloads POSIX
      `close(int)`; works today, but one signature typo away from calling the
      wrong one.
- [ ] Letterbox instead of stretch on resize (NULL dst rect distorts aspect;
      compute a 16:9 dst rect).
- [ ] Flag parity: `CMakeLists.txt` still compiles with no flags — mirror
      build.sh's warning/define set so clangd (compile_commands.json) sees
      `BUILD_INTERNAL` etc.
- [ ] HH typedef block (Quest 2 leftover): `u8…u64`, `i8…i64`, `f32/f64`,
      `internal`/`local_persist`/`global_variable`; per-frame printf → 30-frame
      rolling average.

## 5. Later (post-day-25 land, per QUESTS.md)

- [ ] Window drag on macOS 26 (Tahoe) is sticky for the first ~1 s of a drag,
      then tracks fine. Investigated 2026-07-23: not our code (a minimal SDL
      app with a blocking event loop does it too), not the SDL version (cocoa
      source identical between vendored 3.5.0 and upstream main) — it's an
      OS-level drag-tracking quirk. Ignore unless a macOS update changes it.
- [ ] Fullscreen toggle; derive update Hz from the display's refresh rate.
- [ ] Thread/job queue (~HH day 122+).
- [ ] GPU renderer decision (SDL3 GPU API vs Metal/Vulkan direct) — blocked on
      the 2D-vs-3D call, per PROJECT_SETUP.md §7.
- [ ] Asset pipeline (replaces DEBUG file I/O).
