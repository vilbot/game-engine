@echo off
setlocal EnableDelayedExpansion

rem Windows counterpart of build.sh. Same shape: CMake builds SDL once, then our
rem own code compiles as two direct clang++ invocations.
rem
rem   build.bat --setup     first build on a machine: CMake for SDL, then everything
rem   build.bat             day-to-day: game dll + platform executable
rem   build.bat -g          game dll only (hot-reload inner loop)
rem   build\game.exe        run
rem
rem Requires: LLVM/clang on PATH (winget install LLVM.LLVM) and Visual Studio
rem (for the Windows SDK + MSVC headers clang targets, and the CMake/Ninja used
rem for the one-time SDL build).

set "CLANG=clang++"
where %CLANG% >nul 2>&1 || set "CLANG=C:\Program Files\LLVM\bin\clang++.exe"

if not exist build mkdir build

rem ---------------------------------------------------------------- SDL setup
if /I "%~1"=="--setup" set "SETUP=1"
if /I "%~1"=="-s" set "SETUP=1"
if not exist build\SDL3.dll set "SETUP=1"

if defined SETUP (
    echo [setup] locating Visual Studio build tools...
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    for /f "usebackq delims=" %%i in (`"!VSWHERE!" -products * -latest -property installationPath`) do set "VSROOT=%%i"
    if not defined VSROOT (
        echo [setup] ERROR: Visual Studio not found. Install VS with the C++ workload.
        exit /b 1
    )
    set "CMAKE=!VSROOT!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    set "NINJA=!VSROOT!\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
    set "VCVARS=!VSROOT!\VC\Auxiliary\Build\vcvars64.bat"

    echo [setup] building SDL3 ^(this takes a few minutes, once^)...
    call "!VCVARS!" >nul || exit /b 1
    "!CMAKE!" -S . -B build -G Ninja -DCMAKE_MAKE_PROGRAM="!NINJA!" ^
        -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON || exit /b 1
    "!CMAKE!" --build build --target SDL3-shared --parallel || exit /b 1

    rem SDL3.dll must sit beside game.exe: Windows resolves DLLs from the
    rem executable's directory, which is the moral equivalent of the
    rem -Wl,-rpath,@executable_path that build.sh passes on macOS.
    copy /Y build\third_party\SDL\SDL3.dll build\ >nul || exit /b 1
    copy /Y build\third_party\SDL\SDL3.lib build\ >nul || exit /b 1
)

rem -------------------------------------------------------------------- flags
rem Warning flags apply to our code only, not to the vendored imgui units;
rem -Werror across third-party sources is a rebuild hazard we don't want.
set "WARN=-Wall -Wextra -Werror"
set "FLAGS=-g -gcodeview -fno-exceptions -fno-rtti -DBUILD_INTERNAL=1 -DBUILD_SLOW=1"
set "INC=-Ithird_party/SDL/include -Ithird_party/imgui -Ithird_party/imgui/backends"

set "IMGUI_SRC=third_party/imgui/imgui.cpp third_party/imgui/imgui_draw.cpp"
set "IMGUI_SRC=!IMGUI_SRC! third_party/imgui/imgui_tables.cpp third_party/imgui/imgui_widgets.cpp"
set "IMGUI_SRC=!IMGUI_SRC! third_party/imgui/imgui_demo.cpp"
set "IMGUI_SRC=!IMGUI_SRC! third_party/imgui/backends/imgui_impl_sdl3.cpp"
set "IMGUI_SRC=!IMGUI_SRC! third_party/imgui/backends/imgui_impl_sdlrenderer3.cpp"

rem ----------------------------------------------------------------- platform
if /I "%~1"=="-g" goto :game
if /I "%~1"=="--game" goto :game
if /I "%~1"=="-p" goto :platform
if /I "%~1"=="--platform" goto :platform
if not exist build\game.exe goto :platform
goto :game

:platform
echo [build] platform executable...
rem /subsystem:console is explicit because SDL_main.h supplies a WinMain
rem alongside our main(); without it lld warns while picking console anyway,
rem and console is what we want for the per-frame printf.
%CLANG% %WARN% %FLAGS% %INC% code/sdl_platform.cpp !IMGUI_SRC! ^
    -o build/game.exe -Lbuild -lSDL3 -Wl,/subsystem:console || exit /b 1

:game
echo [build] game dll...
rem A fresh PDB name per build: the running engine loads a *copy* of
rem libgame.dll, but a debugger attached to it keeps the PDB open, and the
rem linker cannot overwrite a locked PDB. Unique names sidestep that entirely.
del /Q build\libgame_*.pdb >nul 2>&1
%CLANG% %WARN% %FLAGS% -shared code/game.cpp -o build/libgame_tmp.dll ^
    -Wl,/PDB:build/libgame_%RANDOM%.pdb -Wl,/IMPLIB:build/libgame_tmp.lib || exit /b 1
move /Y build\libgame_tmp.dll build\libgame.dll >nul || exit /b 1

echo [build] ok
