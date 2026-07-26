#!/usr/bin/env bash

if [[ ! -f build/libSDL3.0.dylib ]]; then
    cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
    cmake --build build --target SDL3-shared --parallel

    # add_subdirectory() makes CMake mirror the source tree, so SDL lands in
    # build/third_party/SDL/ — but the link line is -Lbuild and the rpath is
    # @executable_path, i.e. build/. Copy it up. -a keeps libSDL3.dylib a
    # symlink instead of a second 4 MB copy. build.bat does this for SDL3.dll.
    cp -a build/third_party/SDL/libSDL3.0.dylib build/third_party/SDL/libSDL3.dylib build/
fi

FLAGS=(-Ithird_party/imgui -Ithird_party/imgui/backends)

IMGUI_SRC=(
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/imgui_demo.cpp
    third_party/imgui/backends/imgui_impl_sdl3.cpp
    third_party/imgui/backends/imgui_impl_sdlrenderer3.cpp
)

if [[ $1 == -p || $1 == --platform || ! -f build/game ]]; then
    clang++ "${FLAGS[@]}" code/sdl_platform.cpp "${IMGUI_SRC[@]}" -o build/game \
        -Ithird_party/SDL/include \
        -Lbuild -lSDL3 -Wl,-rpath,@executable_path
fi

clang++ "${FLAGS[@]}" -dynamiclib code/game.cpp -o build/libgame.dylib.tmp
mv -f build/libgame.dylib.tmp build/libgame.dylib
