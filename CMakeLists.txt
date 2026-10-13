cmake_minimum_required(VERSION 3.10)
project(War3DevTool VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8 /W3)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

# Output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

include_directories(
    src
)

# 1. Core JassHook DLL (Loaded into Warcraft III)
add_library(JassHook SHARED
    src/DllMain.cpp
    src/Common/Config.cpp
    src/Common/Logger.cpp
    src/Core/Memory.cpp
    src/Core/HookEngine.cpp
    src/Jass/JassTypes.cpp
    src/Jass/NativeRegistry.cpp
    src/Jass/NativeInvoker.cpp
    src/Game/AntiDefeat.cpp
    src/Game/Selection.cpp
    src/Modules/MallSpoofer.cpp
    src/Modules/SaveManager.cpp
    src/IPC/PipeServer.cpp
)
target_link_libraries(JassHook PRIVATE user32 kernel32)

# 2. External Client Console (CLI / Panel)
add_subdirectory(client)

# 3. DLL Injector
add_subdirectory(injector)
