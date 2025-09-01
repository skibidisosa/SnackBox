cmake_minimum_required(VERSION 3.15)
project(SnackBox LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(snackbox src/main.cpp)

# macOS/Linux: define POSIX feature test macro for some libc headers
if(UNIX)
    target_compile_definitions(snackbox PRIVATE _POSIX_C_SOURCE=200809L)
endif()
