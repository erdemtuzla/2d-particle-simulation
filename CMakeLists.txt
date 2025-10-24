cmake_minimum_required(VERSION 3.25)
project(two-d-sim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(VKH_ENABLE_VALIDATION "Enable Vulkan validation layers in Debug" ON)

# Export compile_commands.json for tooling (clangd, etc.)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Dependencies
find_package(Vulkan REQUIRED)

# Fetch GLFW (builds without OpenGL; we'll use it only for window+surface later)
include(FetchContent)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
# Flip to ON if you specifically want Wayland; X11 is usually the safer default on Linux desktops
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  glfw
  GIT_REPOSITORY https://github.com/glfw/glfw.git
  GIT_TAG        3.4
)
FetchContent_MakeAvailable(glfw)

########################################################
# Target
add_executable(two-d-sim
    src/main.cpp
)

# (Optional) Nice binary name
set_target_properties(two-d-sim PROPERTIES
    OUTPUT_NAME "2D-Sim"
)

# Warnings and dev flags
if (MSVC)
    target_compile_options(two-d-sim
        PRIVATE
            /W4
            /permissive-
    )
    # Show console for printf on Windows
    target_compile_definitions(two-d-sim PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(two-d-sim
        PRIVATE
            -Wall
            -Wextra
            -Wpedantic
    )
endif()

# Link libs
target_link_libraries(two-d-sim
    PRIVATE
        Vulkan::Vulkan
        glfw
)

# Validation switch (only when VKH_ENABLE_VALIDATION=ON and in Debug config)
target_compile_definitions(two-d-sim
    PRIVATE
        $<$<AND:$<CONFIG:Debug>,$<BOOL:${VKH_ENABLE_VALIDATION}>>:VKH_VALIDATION=1>
)
