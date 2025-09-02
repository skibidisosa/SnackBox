cmake_minimum_required(VERSION 3.16)
project(SnackBox LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(SNACKBOX_ENABLE_TESTS "Build unit tests" ON)

# Output dirs
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Sources
file(GLOB SB_SOURCES
        src/http.cpp
        src/server.cpp
        src/router.cpp
        src/utils.cpp
)

add_executable(snackbox
        src/main.cpp
        ${SB_SOURCES}
)
target_include_directories(snackbox PRIVATE ${CMAKE_SOURCE_DIR}/src)

if(UNIX)
    target_link_libraries(snackbox PRIVATE pthread)
endif()

# Tests
if(SNACKBOX_ENABLE_TESTS)
    add_executable(snackbox_tests
            tests/test_main.cpp
            ${SB_SOURCES}
    )
    target_include_directories(snackbox_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    if(UNIX)
        target_link_libraries(snackbox_tests PRIVATE pthread)
    endif()
    add_test(NAME snackbox_tests COMMAND snackbox_tests)
    enable_testing()
endif()
