cmake_minimum_required(VERSION 3.22)
project(StreamPulseVulkan LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# 1. Vulkan Headers & Volk (dynamic loader for vulkan-1.dll, no SDK install required)
FetchContent_Declare(
    vulkan_headers
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG        v1.3.296
    GIT_SHALLOW    TRUE
)
FetchContent_Declare(
    volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG        vulkan-sdk-1.3.296.0
    GIT_SHALLOW    TRUE
)
set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_WIN32_KHR CACHE STRING "" FORCE)

# 2. GLFW 3.4
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

# 3. Dear ImGui
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.5
    GIT_SHALLOW    TRUE
)

# 4. nlohmann/json
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(vulkan_headers volk glfw imgui nlohmann_json)

target_include_directories(volk PUBLIC ${vulkan_headers_SOURCE_DIR}/include)

# Build ImGui library with GLFW + Vulkan (Volk) backends
add_library(imgui_lib STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(imgui_lib PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)
target_compile_definitions(imgui_lib PUBLIC
    VK_NO_PROTOTYPES
    IMGUI_IMPL_VULKAN_USE_VOLK
    IMGUI_USE_WCHAR32
)
target_link_libraries(imgui_lib PUBLIC
    glfw
    volk
    Vulkan::Headers
)

# Main Application (WIN32 GUI subsystem, no console window)
add_executable(StreamPulse WIN32
    src/main.cpp
    assets/app_icon.rc
    src/third_party_impl.cpp
    src/vulkan/VulkanContext.cpp
    src/vulkan/VulkanTexture.cpp
    src/media/MediaPlayer.cpp
    src/library/LibraryManager.cpp
    src/library/PlatformSync.cpp
    src/library/StatsManager.cpp
    src/library/DiscordRPC.cpp
    src/ui/IconStore.cpp
    src/ui/Templates.cpp
    src/ui/MediaKeys.cpp
    src/ui/AppUI.cpp
)

target_include_directories(StreamPulse PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

target_compile_definitions(StreamPulse PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    UNICODE
    _UNICODE
    VK_NO_PROTOTYPES
    IMGUI_IMPL_VULKAN_USE_VOLK
)

target_link_libraries(StreamPulse PRIVATE
    imgui_lib
    nlohmann_json::nlohmann_json
    mfplat
    mf
    mfreadwrite
    mfuuid
    winhttp
    windowscodecs
    winmm
    dwmapi
    shlwapi
    ole32
    shell32
)

if (MSVC)
    target_link_options(StreamPulse PRIVATE "/ENTRY:mainCRTStartup")
endif()

# Copy assets & tools to output directory after build
add_custom_command(TARGET StreamPulse POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:StreamPulse>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/tools"
        "$<TARGET_FILE_DIR:StreamPulse>/tools"
)
