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

include_directories(src)

# 1. External Developer Console (CLI Client)
add_executable(War3JassDevTool 
    src/main.cpp
)

# 2. JassHook DLL (Injected into Warcraft III)
add_library(JassHook SHARED
    src/JassHook.cpp
)
target_link_libraries(JassHook PRIVATE user32 kernel32)

# 3. Injector
add_executable(Injector
    src/injector.cpp
)
if(MSVC)
    set_target_properties(Injector PROPERTIES LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")
endif()
