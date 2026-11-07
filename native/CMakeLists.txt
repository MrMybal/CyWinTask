cmake_minimum_required(VERSION 3.24)
project(CyWinTaskNative LANGUAGES CXX RC)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CYUI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/CyUI")
if(NOT EXISTS "${CYUI_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "CyUI is missing. Run native/Sync-CyUI.ps1 with access to the private CyUI repository.")
endif()
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/CYUI_REVISION" CYUI_REQUIRED_REVISION)
string(STRIP "${CYUI_REQUIRED_REVISION}" CYUI_REQUIRED_REVISION)
if(EXISTS "${CYUI_ROOT}/REVISION")
    file(READ "${CYUI_ROOT}/REVISION" CYUI_ACTUAL_REVISION)
    string(STRIP "${CYUI_ACTUAL_REVISION}" CYUI_ACTUAL_REVISION)
else()
    execute_process(COMMAND git -c "safe.directory=${CYUI_ROOT}" -C "${CYUI_ROOT}" rev-parse HEAD
        OUTPUT_VARIABLE CYUI_ACTUAL_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE CYUI_REVISION_RESULT ERROR_QUIET)
    if(NOT CYUI_REVISION_RESULT EQUAL 0)
        message(FATAL_ERROR "Cannot determine the CyUI revision.")
    endif()
endif()
if(NOT CYUI_ACTUAL_REVISION STREQUAL CYUI_REQUIRED_REVISION)
    message(FATAL_ERROR "CyUI revision mismatch. Run native/Sync-CyUI.ps1 to use the pinned revision.")
endif()
set(IMGUI_DIR "${CYUI_ROOT}/third_party/imgui")
add_library(cywintask_imgui STATIC
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp")
target_include_directories(cywintask_imgui PUBLIC "${IMGUI_DIR}")
target_compile_options(cywintask_imgui PRIVATE /utf-8)
set(CYUI_IMGUI_TARGET cywintask_imgui CACHE STRING "Use the application's Dear ImGui target" FORCE)
set(CYUI_MODULES "Plots" CACHE STRING "" FORCE)
set(CYUI_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(CYUI_BUILD_AUDIO OFF CACHE BOOL "" FORCE)
set(CYUI_BUILD_GALLERY OFF CACHE BOOL "" FORCE)
add_subdirectory("${CYUI_ROOT}" "${CMAKE_BINARY_DIR}/cyui")

add_executable(CyWinTask WIN32
    src/main.cpp src/BridgeClient.cpp src/ProcessTrust.cpp src/AppUi.cpp src/UiScale.cpp src/resources.rc
    "${IMGUI_DIR}/backends/imgui_impl_win32.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_dx11.cpp")
target_include_directories(CyWinTask PRIVATE "${IMGUI_DIR}" "${IMGUI_DIR}/backends")
target_compile_options(CyWinTask PRIVATE /W4 /utf-8)
target_compile_definitions(CyWinTask PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(CyWinTask PRIVATE CyUI::Core CyUI::Plots d3d11 dxgi d3dcompiler dwmapi shell32 gdi32 user32 advapi32 wintrust)
