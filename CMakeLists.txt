cmake_minimum_required(VERSION 3.25)
project(two-d-sim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(two-d-sim 
    src/main.cpp
)

set_target_properties(two-d-sim PROPERTIES
    OUTPUT_NAME "2D-Sim"
)

# Dependencies
# find_package(Vulkan REQUIRED)


# Warnings and dev flag
if (MSVC)
    target_compile_options(two-d-sim
        PRIVATE
            /W4
            /permissive-
    )
else ()
    target_compile_options(two-d-sim
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
endif ()

# Export compile_commands.json for tooling (clangd, etc.)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)