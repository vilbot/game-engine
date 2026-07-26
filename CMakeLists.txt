cmake_minimum_required(VERSION 3.16)
project(game)

# CMake builds SDL only — see build.sh, which compiles code/*.cpp directly
# with clang++ and links against the library this produces.
add_subdirectory(third_party/SDL EXCLUDE_FROM_ALL)
