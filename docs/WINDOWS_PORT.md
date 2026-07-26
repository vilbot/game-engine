# Windows port — de-POSIX spec

Companion to TODO.md §3. Line numbers refer to the tree at commit `e486603`.

> **Status: done.** The port is complete and running on Windows — `build.bat`
> builds clean under `-Wall -Wextra -Werror`, `build\game.exe` holds 60 fps with
> audio at a 10 ms device period, and hot reload works (verified live: rebuilding
> the DLL under a running engine loads a new temp copy without dropping the
> process, at the cost of one ~58 ms frame).
>
> Everything below is kept as the record of *why* each change is shaped the way
> it is. Two things remain unverified — see "Still to verify" at the end.

## Already done (tooling)

- **LLVM/clang 22.1.8** installed (`C:\Program Files\LLVM`), target
  `x86_64-pc-windows-msvc` — it uses the MSVC headers/libs from the installed
  Visual Studio 2026, so the C ABI matches the MSVC-built SDL.
- **SDL3 built for Windows** via the VS-bundled CMake + Ninja (MSVC 14.51).
  Produces `build\SDL3.dll` + `build\SDL3.lib`, staged next to the executable —
  Windows resolves DLLs from the exe's directory, which is what
  `-Wl,-rpath,@executable_path` buys us on macOS.
- **`build.bat`** written, mirroring `build.sh` (`--setup` / `-g` / `-p`).
- **Verified end to end**: clang compiles all seven imgui units *and*
  `-Wall -Wextra -Werror` clean; links against SDL3; the resulting exe opens a
  window, initializes an audio device, and `SDL_GetBasePath()` returns
  `...\build\` **with a trailing separator** — so the existing `"%s" + name`
  concatenation pattern works unchanged.

Two things worth knowing about `build.bat` that differ from `build.sh`:

- Warning flags are applied to *our* code only, not the vendored imgui units.
- The game DLL links with a randomized PDB name. The engine loads a *copy* of
  `libgame.dll`, but an attached debugger holds the PDB open and the linker
  cannot overwrite a locked PDB. Unique names sidestep it.

## Blocking: what does not compile

### game.cpp / game.h — do these first, they're small and unblock `build.bat -g`

**1. `M_PI` is undefined.** *(verified: 3 errors, game.cpp:24, 25, 25)*
The UCRT only defines `M_PI` if `_USE_MATH_DEFINES` is set before `<math.h>`,
and `game.h:1` includes `<cmath>` first thing. HH-authentic fix is a constant of
our own in `game.h` rather than a define-dance:

```c
#define Pi32 3.14159265359f
```

That is the *only* compile error in `game.cpp`.

**2. The DLL exports nothing.** *(verified: built `game.cpp` as a DLL — the COFF
export table is empty)* On Windows, `extern "C"` controls name mangling, not
export visibility; a symbol leaves a DLL only with `__declspec(dllexport)` (or a
.def file). Left as-is the DLL loads fine, `SDL_LoadFunction` returns null for
both entry points, `is_valid` stays false, and **the engine silently runs the
stubs forever** — black screen, no sound, no error.

Suggested macro in `game.h`, applied to both the declarations (game.h:75–78) and
the definitions (game.cpp:10, 29):

```c
#if defined(_WIN32)
    #define GAME_API extern "C" __declspec(dllexport)
#else
    #define GAME_API extern "C"
#endif
```

On x64 an `extern "C"` export is undecorated, so the name strings already passed
to `dlsym`/`SDL_LoadFunction` (`"game_update_and_render"`,
`"game_get_sound_samples"`) stay correct.

### sdl_platform.cpp

#### A. Headers (lines 17–23)

`<sys/mman.h>`, `<fcntl.h>`, `<unistd.h>`, `<sys/stat.h>`, `<dlfcn.h>`,
`<cerrno>` — none exist on Windows. Sections B–D delete the need for all of them
except the memory one; only that one gets a `#ifdef` (and `<windows.h>`).

#### B. Dynamic loading → SDL (portable, no `#ifdef` at all)

Add `<SDL3/SDL_loadso.h>`. Signatures confirmed against the vendored headers:

| line | now | becomes |
|------|-----|---------|
| 52 | `void* game_dylib` | `SDL_SharedObject* game_dylib` |
| 72 | `dlopen(temp_path, RTLD_NOW\|RTLD_LOCAL)` | `SDL_LoadObject(temp_path)` |
| 74–75 | `dlsym(handle, name)` | `SDL_LoadFunction(handle, name)` |
| 96, 118 | `dlclose(handle)` | `SDL_UnloadObject(handle)` |

`SDL_LoadFunction` returns `SDL_FunctionPointer`, so the existing casts stay
(and stay legal — it's already a function-pointer type, unlike `dlsym`'s
`void*`).

#### C. Library naming (lines 65, 69, 106, 331)

`"libgame.dylib"` is hardcoded at 65 and 331, the temp pattern at 69, and the
sweep glob at 106. One constant per TODO.md:

```c
#if defined(_WIN32)
    #define GAME_LIB      "libgame.dll"
    #define GAME_LIB_TEMP "libgame_temp_%d.dll"
    #define GAME_LIB_GLOB "libgame_temp_*.dll"
#elif defined(__APPLE__)
    ... .dylib ...
#else
    ... .so ...
#endif
```

`build.bat` already emits `build\libgame.dll`, matching the `libgame` basename.

Nothing else in the hot-reload path needs changing — copy-to-temp-then-load is
already the correct Windows pattern (Windows locks the loaded *temp* copy and
leaves `libgame.dll` writable for the next compile), and the startup sweep works
because previous runs' temps aren't loaded.

#### D. Debug file I/O (lines 157–191) → `SDL_LoadFile` / `SDL_SaveFile`

Simpler than hand-rolled `SDL_IOStream`, and deletes the `fcntl`/`unistd`/`stat`
includes outright:

```c
DEBUG_PLATFORM_READ_ENTIRE_FILE(sdl_debug_platform_read_entire_file) {
    debug_read_file_result result{};
    size_t size = 0;
    void* contents = SDL_LoadFile(filename, &size);
    if (contents) {
        result.contents = contents;
        result.contents_size = (uint32_t)size;   // narrowing: -Werror will demand the cast
    }
    return result;
}
```

- `free(memory)` (line 181) **must** become `SDL_free` — `SDL_LoadFile`
  allocates with `SDL_malloc`, and mixing allocators across the DLL boundary is
  exactly the kind of thing that works until it doesn't.
- Write (184–191) collapses to `return SDL_SaveFile(filename, memory, memory_size);`
- Minor: `SDL_LoadFile` allocates `size+1` and null-terminates. It reports the
  true size in `size`, so the struct stays honest; harmless for current use.

#### E. Memory — the one real `#ifdef` (lines 275–276, 290–291, 298, 300–302)

Three `mmap` calls: pixel backbuffer (275), game memory with the fixed base
hint (290), replay block (298). A helper keeps the branch in one place:

```c
static void* platform_alloc(void* base, size_t size) {
#if defined(_WIN32)
    return VirtualAlloc(base, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    void* p = mmap(base, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    return (p == MAP_FAILED) ? nullptr : p;   // normalize failure to NULL
#endif
}
```

Then the checks at 300 become `== nullptr`, and `strerror(errno)` at 302 has no
Windows equivalent — either `#ifdef` it to `GetLastError()` or just drop to a
generic message.

**The semantic difference worth knowing** (line 306–311's warning depends on
it): `mmap` treats the base address as a *hint* and silently relocates, which is
why that code compares the result against `base_address` and warns. `VirtualAlloc`
does not relocate — if 2 TB is unavailable it **returns NULL and fails outright**.
So on Windows the existing warning branch is unreachable; the failure shows up as
an allocation failure instead. Decide which you want: fail hard, or retry with
`base = nullptr` and let the existing warning fire. `Terabytes(2)` is fine on
Win64 (128 TB of user address space, and it's 64 KB-granularity aligned).

## Things that need no change

- `fopen`/`fwrite`/`fseek` for replay — already `"wb"`/`"rb"`, correct on Windows.
- `main(argc, argv)` + `SDL_main.h` — works; `build.bat` passes
  `/subsystem:console` explicitly so the per-frame `printf` has somewhere to go.
- `snprintf`, `SDL_GetPathInfo`, `SDL_CopyFile`, `SDL_GlobDirectory`, the audio
  callback, the frame-pacing code.

## Surfaced during the build, not predicted above

Three `-Werror` failures that `build.sh` never showed, because it passes no
warning flags at all (CLAUDE.md claims otherwise; it's wrong):

- **`main`'s parameters are not exempt from `-Wunused-parameter` here.** The
  usual exemption applies to *the* `main`, but `SDL_main.h` `#define`s `main` to
  `SDL_main`, so it's an ordinary function. Parameters are now unnamed.
- `total_amount` in the audio callback — unused, now unnamed.
- **`fopen` is deprecated by the UCRT** in favour of `fopen_s`. It's standard C
  and the code is correct, so this is silenced with `-D_CRT_SECURE_NO_WARNINGS`
  in `build.bat` rather than worked around in the source.

## Still to verify

- **Input replay (Quest 10)** — needs a live `L` keypress, so it was not
  exercised. The memory-snapshot path it depends on is `memcpy` over the block
  and is platform-neutral, but the `fopen`/`fwrite` replay file is worth one
  manual run.
- **macOS still builds.** Sections B–D are portable and E branches cleanly, but
  this could not be compiled on macOS from here. Re-run `./build.sh` there.
  The one change to look at first is `game.h`'s `#elif defined(__APPLE__)` —
  it was originally `#elseif defined(___APPLE__)`, which is not a directive the
  preprocessor recognizes, so macOS would have silently fallen through to the
  `.so` branch and looked for a `libgame.so` that never exists.
