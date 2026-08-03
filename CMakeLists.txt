cmake_minimum_required(VERSION 3.20)
project(BestCompare LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable MSVC optimizations & AVX2 vectorization safely per build configuration
if(MSVC)
    add_compile_options(/arch:AVX2 /W4 /permissive-)
    # Apply /O2 optimization only to Release / RelWithDebInfo builds
    add_compile_options($<$<CONFIG:Release>:/O2> $<$<CONFIG:RelWithDebInfo>:/O2>)

    # Statically link MSVC C++ Runtime (/MT for Release, /MTd for Debug)
    # This prevents missing MSVCP140D_ATOMIC_WAIT.dll / VCRUNTIME140.dll on 2nd computer
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

add_compile_definitions(BESTCOMPARE_WIN32_GUI)



# Auto-increment build version on every build
add_custom_target(IncrementVersion ALL
    COMMAND ${CMAKE_COMMAND} -DVERSION_FILE="${CMAKE_CURRENT_SOURCE_DIR}/src/version.hpp" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/increment_build.cmake"
    COMMENT "Incrementing build version number..."
)

# Source files
set(SOURCES
    src/main.cpp
    src/version.hpp
    src/core/hash_engine.hpp
    src/core/scanner.hpp
    src/core/diff_engine.hpp
    src/core/file_ops.hpp
    src/network/protocol.hpp
    src/network/network_server.hpp
    src/network/network_client.hpp
    src/ui/app_window.hpp
    src/ui/gui_state.hpp
    src/ui/tree_node.hpp
    src/vendor/imgui/imgui.cpp
    src/vendor/imgui/imgui_demo.cpp
    src/vendor/imgui/imgui_draw.cpp
    src/vendor/imgui/imgui_tables.cpp
    src/vendor/imgui/imgui_widgets.cpp
    src/vendor/imgui/backends/imgui_impl_win32.cpp
    src/vendor/imgui/backends/imgui_impl_dx11.cpp
)

add_executable(BestCompare WIN32 ${SOURCES})
add_dependencies(BestCompare IncrementVersion)

# Include directories
target_include_directories(BestCompare PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vendor/imgui
)

# Link Windows Socket 32, DirectX 11, and DXGI libraries
if(WIN32)
    target_link_libraries(BestCompare PRIVATE Ws2_32 d3d11 dxgi Ole32 Shell32)
endif()

