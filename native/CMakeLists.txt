cmake_minimum_required(VERSION 3.24)
project(CyWinTaskNative LANGUAGES CXX RC)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(CYUI_ROOT "" CACHE PATH "Full CyUI source checkout")
if(NOT CYUI_ROOT)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../CyImgui_Interface/src/modules/DocumentViews.cpp")
        set(CYUI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../CyImgui_Interface")
    else()
        set(CYUI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/CyUI")
    endif()
endif()
if(NOT EXISTS "${CYUI_ROOT}/CMakeLists.txt")
    message(FATAL_ERROR "CyUI is missing. Run native/Sync-CyUI.ps1 with access to the private CyUI repository.")
endif()
file(REAL_PATH "${CYUI_ROOT}" CYUI_ROOT)
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
set(CYUI_MODULES "Plots;DocumentViews" CACHE STRING "" FORCE)
set(CYUI_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(CYUI_BUILD_AUDIO OFF CACHE BOOL "" FORCE)
set(CYUI_BUILD_GALLERY OFF CACHE BOOL "" FORCE)
add_subdirectory("${CYUI_ROOT}" "${CMAKE_BINARY_DIR}/cyui")

set(CYHWINFO_SOURCE_DIR "" CACHE PATH "CyHWInfo source checkout")
if(NOT CYHWINFO_SOURCE_DIR)
    set(CYHWINFO_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../CyHWInfo")
endif()
if(NOT EXISTS "${CYHWINFO_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "CyHWInfo sources are missing. Set CYHWINFO_SOURCE_DIR or run native/Build-Native.ps1 with GitHub access.")
endif()
file(REAL_PATH "${CYHWINFO_SOURCE_DIR}" CYHWINFO_ROOT)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/CYHWINFO_REVISION" CYHWINFO_REQUIRED_REVISION)
string(STRIP "${CYHWINFO_REQUIRED_REVISION}" CYHWINFO_REQUIRED_REVISION)
if(EXISTS "${CYHWINFO_ROOT}/REVISION")
    file(READ "${CYHWINFO_ROOT}/REVISION" CYHWINFO_ACTUAL_REVISION)
    string(STRIP "${CYHWINFO_ACTUAL_REVISION}" CYHWINFO_ACTUAL_REVISION)
else()
    execute_process(COMMAND git -c "safe.directory=${CYHWINFO_ROOT}" -C "${CYHWINFO_ROOT}" rev-parse HEAD
        OUTPUT_VARIABLE CYHWINFO_ACTUAL_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE CYHWINFO_REVISION_RESULT ERROR_QUIET)
    if(NOT CYHWINFO_REVISION_RESULT EQUAL 0)
        message(FATAL_ERROR "Cannot determine the CyHWInfo revision.")
    endif()
endif()
if(NOT CYHWINFO_ACTUAL_REVISION STREQUAL CYHWINFO_REQUIRED_REVISION)
    message(FATAL_ERROR "CyHWInfo revision mismatch. Use the revision in native/CYHWINFO_REVISION.")
endif()
if(NOT EXISTS "${CYHWINFO_ROOT}/REVISION")
    execute_process(COMMAND git -c "safe.directory=${CYHWINFO_ROOT}" -C "${CYHWINFO_ROOT}"
        status --porcelain --untracked-files=normal
        OUTPUT_VARIABLE CYHWINFO_LOCAL_CHANGES OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE CYHWINFO_STATUS_RESULT ERROR_QUIET)
    if(NOT CYHWINFO_STATUS_RESULT EQUAL 0 OR CYHWINFO_LOCAL_CHANGES)
        message(FATAL_ERROR "CyHWInfo checkout has local changes; use a clean pinned checkout.")
    endif()
endif()
set(FETCHCONTENT_SOURCE_DIR_LFREIST_HWINFO "${CMAKE_CURRENT_SOURCE_DIR}/third_party/hwinfo" CACHE PATH "Offline hwinfo source" FORCE)
set(CYHWINFO_COMPONENTS "Cpu;Gpu;GpuTelemetry;GpuProcesses;Memory;Motherboard;LibreHardwareMonitor" CACHE STRING "" FORCE)
set(CYHWINFO_BUILD_UI ON CACHE BOOL "" FORCE)
set(CYHWINFO_BUILD_APP OFF CACHE BOOL "" FORCE)
add_subdirectory("${CYHWINFO_ROOT}" "${CMAKE_BINARY_DIR}/cyhwinfo")

set(CYMCP_SOURCE_DIR "" CACHE PATH "CyMCPSDK source checkout")
if(NOT CYMCP_SOURCE_DIR)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../CyMCPSDK/CMakeLists.txt")
        set(CYMCP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../CyMCPSDK")
    else()
        set(CYMCP_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/CyMCPSDK")
    endif()
endif()
if(NOT EXISTS "${CYMCP_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "CyMCPSDK sources are missing. Set CYMCP_SOURCE_DIR to a local checkout.")
endif()
file(REAL_PATH "${CYMCP_SOURCE_DIR}" CYMCP_ROOT)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/CYMCP_REVISION" CYMCP_REQUIRED_REVISION)
string(STRIP "${CYMCP_REQUIRED_REVISION}" CYMCP_REQUIRED_REVISION)
execute_process(COMMAND git -c "safe.directory=${CYMCP_ROOT}" -C "${CYMCP_ROOT}" rev-parse HEAD
    OUTPUT_VARIABLE CYMCP_ACTUAL_REVISION OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE CYMCP_REVISION_RESULT ERROR_QUIET)
if(NOT CYMCP_REVISION_RESULT EQUAL 0 OR NOT CYMCP_ACTUAL_REVISION STREQUAL CYMCP_REQUIRED_REVISION)
    message(FATAL_ERROR "CyMCPSDK revision mismatch. Use the revision in native/CYMCP_REVISION.")
endif()
set(CYMCP_BUILD_HTTP ON CACHE BOOL "" FORCE)
set(CYMCP_BUILD_PLATFORM ON CACHE BOOL "" FORCE)
set(CYMCP_BUILD_MCP ON CACHE BOOL "" FORCE)
set(CYMCP_BUILD_AI ON CACHE BOOL "" FORCE)
set(CYMCP_BUILD_LOCAL OFF CACHE BOOL "" FORCE)
set(CYMCP_BUILD_CHAT_UI ON CACHE BOOL "" FORCE)
set(CYMCP_BUILD_CHAT_APP OFF CACHE BOOL "" FORCE)
set(CYMCP_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CYMCP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${CYMCP_ROOT}" "${CMAKE_BINARY_DIR}/cymcp")

# Localized MSVC /showIncludes output can leave Ninja with no header deps.
# Keep the application's own headers as explicit object dependencies.
file(GLOB CYWINTASK_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")
set_source_files_properties(
    src/main.cpp src/BridgeClient.cpp src/ProcessTrust.cpp src/AppUi.cpp
    src/AssistantPane.cpp src/McpAccess.cpp src/TaskService.cpp src/UiBridge.cpp src/UiOperations.cpp
    src/UiScale.cpp src/UiLocale.cpp src/UiKit.cpp src/ConsolePane.cpp src/SensorLog.cpp src/cli.cpp tests/AssistantIntegration.cpp tests/UiControlIntegration.cpp
    tests/ConsoleIntegration.cpp tests/HardwareInventoryIntegration.cpp src/HardwareInventory.cpp
    PROPERTIES OBJECT_DEPENDS "${CYWINTASK_HEADERS}")

add_executable(CyWinTask WIN32
    src/main.cpp src/BridgeClient.cpp src/ProcessTrust.cpp src/AppUi.cpp src/HardwareInventory.cpp src/AssistantPane.cpp src/McpAccess.cpp
    src/TaskService.cpp src/UiBridge.cpp src/UiOperations.cpp src/UiScale.cpp src/UiLocale.cpp src/UiKit.cpp src/ConsolePane.cpp src/SensorLog.cpp src/resources.rc
    "${IMGUI_DIR}/backends/imgui_impl_win32.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_dx11.cpp")
target_include_directories(CyWinTask PRIVATE "${IMGUI_DIR}" "${IMGUI_DIR}/backends")
target_compile_options(CyWinTask PRIVATE /W4 /utf-8)
target_compile_definitions(CyWinTask PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(CyWinTask PRIVATE CyUI::Core CyUI::Plots CyMCP::ChatUI CyMCP::MCP CyHWInfo::Cpu CyHWInfo::Gpu CyHWInfo::GpuTelemetry CyHWInfo::GpuProcesses CyHWInfo::Memory CyHWInfo::Motherboard CyHWInfo::LibreHardwareMonitor CyHWInfo::UI d3d11 dxgi d3dcompiler dwmapi shell32 gdi32 user32 advapi32 wintrust comdlg32)
cyhwinfo_deploy_libre_hardware_monitor(CyWinTask)

add_executable(CyWinTaskCli src/cli.cpp src/McpAccess.cpp src/TaskService.cpp src/BridgeClient.cpp)
target_compile_options(CyWinTaskCli PRIVATE /W4 /utf-8)
target_compile_definitions(CyWinTaskCli PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(CyWinTaskCli PRIVATE CyMCP::MCP)

add_executable(CyWinTaskAssistantChecks EXCLUDE_FROM_ALL
    tests/AssistantIntegration.cpp src/McpAccess.cpp src/TaskService.cpp src/BridgeClient.cpp)
target_compile_options(CyWinTaskAssistantChecks PRIVATE /W4 /utf-8)
target_compile_definitions(CyWinTaskAssistantChecks PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(CyWinTaskAssistantChecks PRIVATE CyMCP::ChatUI CyMCP::MCP)

add_executable(CyWinTaskUiChecks EXCLUDE_FROM_ALL
    tests/UiControlIntegration.cpp src/UiBridge.cpp src/UiOperations.cpp
    src/McpAccess.cpp src/TaskService.cpp src/BridgeClient.cpp)
target_compile_options(CyWinTaskUiChecks PRIVATE /W4 /utf-8)
target_compile_definitions(CyWinTaskUiChecks PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(CyWinTaskUiChecks PRIVATE CyMCP::MCP)

add_executable(CyWinTaskConsoleChecks EXCLUDE_FROM_ALL
    tests/ConsoleIntegration.cpp src/ConsolePane.cpp src/SensorLog.cpp src/UiLocale.cpp src/UiKit.cpp)
target_include_directories(CyWinTaskConsoleChecks PRIVATE "${IMGUI_DIR}")
target_compile_options(CyWinTaskConsoleChecks PRIVATE /W4 /utf-8)
target_compile_definitions(CyWinTaskConsoleChecks PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(CyWinTaskConsoleChecks PRIVATE CyUI::Core advapi32 comdlg32)

add_executable(CyWinTaskHardwareChecks EXCLUDE_FROM_ALL
    tests/HardwareInventoryIntegration.cpp src/HardwareInventory.cpp)
target_compile_options(CyWinTaskHardwareChecks PRIVATE /W4 /utf-8)
target_compile_definitions(CyWinTaskHardwareChecks PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(CyWinTaskHardwareChecks PRIVATE
    CyHWInfo::Cpu CyHWInfo::Gpu CyHWInfo::Memory CyHWInfo::Motherboard)

add_executable(CyWinTaskSensorChecks EXCLUDE_FROM_ALL tests/CyHWInfoIntegration.cpp)
target_compile_options(CyWinTaskSensorChecks PRIVATE /W4 /utf-8)
target_compile_definitions(CyWinTaskSensorChecks PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(CyWinTaskSensorChecks PRIVATE CyHWInfo::Cpu CyHWInfo::Gpu
    CyHWInfo::GpuTelemetry CyHWInfo::GpuProcesses CyHWInfo::Memory CyHWInfo::Motherboard CyHWInfo::LibreHardwareMonitor)
cyhwinfo_deploy_libre_hardware_monitor(CyWinTaskSensorChecks)
