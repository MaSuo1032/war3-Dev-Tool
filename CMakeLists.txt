cmake_minimum_required(VERSION 3.15)
project(War3DevTool VERSION 2.0.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "War3DevTool requires Windows and the 32-bit MSVC toolchain.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Warcraft III 1.27 requires x86 binaries. Configure a new build directory with -A Win32.")
endif()
if(NOT MSVC)
    message(FATAL_ERROR "War3DevTool requires MSVC for its x86 inline assembly and structured exception handling.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8 /W3)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)
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

# 4. All-in-One GUI Client (Injector + Dev Tool Panel + Web Controller)
add_executable(War3DevToolUI WIN32
    src/gui.cpp
    src/Wc3Memory.cpp
)
target_link_libraries(War3DevToolUI PRIVATE user32 kernel32 gdi32 comctl32 shell32 advapi32 comdlg32 ws2_32)
if(MSVC)
    set_target_properties(War3DevToolUI PROPERTIES LINK_FLAGS "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")
endif()

# Install the complete runnable package. Defaults are seeded only when absent;
# rebuilding must not reset the user's current INI or map-specific lists.
install(TARGETS War3JassDevTool JassHook Injector War3DevToolUI
    RUNTIME DESTINATION . COMPONENT Runtime
)
install(FILES scripts/death_trace.lua scripts/death_trace_stop.lua DESTINATION . COMPONENT Runtime)
install(SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/cmake/InstallDefaults.cmake"
    COMPONENT Runtime
)

include(CTest)
if(BUILD_TESTING)
    add_executable(HookRegression tests/hook_regression.cpp)
    target_link_libraries(HookRegression PRIVATE user32 kernel32)
    add_test(NAME hook_regression COMMAND HookRegression)

    add_library(InjectorPayload SHARED tests/injector_payload.cpp)
    add_library(InjectorReject SHARED tests/injector_payload.cpp)
    target_compile_definitions(InjectorReject PRIVATE REJECT_PAYLOAD)
    add_executable(InjectorRegression tests/injector_regression.cpp)
    target_link_libraries(InjectorRegression PRIVATE advapi32)
    add_dependencies(InjectorRegression InjectorPayload InjectorReject)
    add_test(NAME injector_regression COMMAND InjectorRegression)

    add_executable(MemoryWebRegression tests/memory_web_regression.cpp src/Wc3Memory.cpp)
    target_link_libraries(MemoryWebRegression PRIVATE ws2_32 advapi32 shell32 user32)
    add_test(NAME memory_web_regression COMMAND MemoryWebRegression)

    add_executable(SaveDiffRegression tests/save_diff_regression.cpp)
    add_test(NAME save_diff_regression COMMAND SaveDiffRegression)

    set_tests_properties(hook_regression injector_regression memory_web_regression
        PROPERTIES TIMEOUT 45 RUN_SERIAL TRUE)
endif()
