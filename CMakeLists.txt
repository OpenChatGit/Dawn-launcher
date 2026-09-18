cmake_minimum_required(VERSION 3.20)
project(app C CXX)

set(APP_VERSION "" CACHE STRING "Launcher version")
if(APP_VERSION STREQUAL "")
    file(STRINGS "${CMAKE_SOURCE_DIR}/VERSION" APP_VERSION LIMIT_COUNT 1)
endif()
string(STRIP "${APP_VERSION}" APP_VERSION)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00)
else()
    add_compile_definitions(_GNU_SOURCE)
endif()

set(APP_LOGIC_SRC
    src/modules/app.c
    src/modules/state/state.c
    src/modules/ui/ui.c
    src/modules/ui/titlebar.c
    src/modules/ui/login_modal.c
    src/modules/ui/settings_modal.c
    src/modules/ui/modal_skin.c
    src/modules/ui/install_button.c
    src/modules/theme/theme_manager.c
    src/modules/theme/theme_json.c
    src/modules/theme/theme_ui.c
    src/shared/os.c
)

set(HOST_SRC
    src/host/main.c
    src/host/debug_console.c
    src/host/watcher.c
    src/host/builder.c
    src/host/hot_reload.c
    src/host/install_job.c
    src/host/steam_auth.cpp
    src/shared/os.c
)

if(WIN32)
    list(APPEND APP_LOGIC_SRC
        src/modules/icons/icons.cpp
        src/modules/theme/theme_render.cpp
    )
    list(APPEND HOST_SRC
        src/host/window.c
        src/host/media.cpp
        src/host/ytstream.cpp
        src/host/video_bg.cpp
    )
else()
    list(APPEND APP_LOGIC_SRC
        src/modules/icons/icons_soft.cpp
        src/modules/theme/theme_render_soft.cpp
    )
    list(APPEND HOST_SRC
        src/host/window_linux.c
        src/host/media_linux.c
        src/host/ytstream_linux.c
        src/host/video_bg_linux.c
    )
endif()

add_library(app_logic SHARED ${APP_LOGIC_SRC})
target_include_directories(app_logic PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/modules
)
target_compile_definitions(app_logic PRIVATE APP_EXPORTS)
set_target_properties(app_logic PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    PREFIX ""
)

add_executable(host ${HOST_SRC})
target_include_directories(host PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src/host
)

get_filename_component(APP_CMAKE_DIR "${CMAKE_COMMAND}" DIRECTORY)
get_filename_component(APP_NINJA_DIR "${CMAKE_MAKE_PROGRAM}" DIRECTORY)
get_filename_component(APP_CC_DIR "${CMAKE_C_COMPILER}" DIRECTORY)

file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}" APP_PROJECT_ROOT)
file(TO_CMAKE_PATH "${CMAKE_BINARY_DIR}" APP_BUILD_DIR)
file(TO_CMAKE_PATH "${CMAKE_COMMAND}" APP_CMAKE_COMMAND)
file(TO_CMAKE_PATH "${APP_CMAKE_DIR}" APP_CMAKE_DIR)
file(TO_CMAKE_PATH "${APP_NINJA_DIR}" APP_NINJA_DIR)
file(TO_CMAKE_PATH "${APP_CC_DIR}" APP_CC_DIR)
if(WIN32)
    set(APP_TOOL_PATH "${APP_CMAKE_DIR};${APP_NINJA_DIR};${APP_CC_DIR}")
else()
    set(APP_TOOL_PATH "${APP_CMAKE_DIR}:${APP_NINJA_DIR}:${APP_CC_DIR}")
endif()

configure_file(
    ${CMAKE_SOURCE_DIR}/src/host/config.h.in
    ${CMAKE_BINARY_DIR}/generated/config.h
    @ONLY
)

target_include_directories(host PRIVATE ${CMAKE_BINARY_DIR}/generated)

if(WIN32)
    target_link_libraries(host PRIVATE user32 gdi32 dwmapi ole32 uuid shell32 mfplat mfplay mfreadwrite d3d11 dxgi winhttp ws2_32 version)
    target_link_libraries(app_logic PRIVATE user32 gdi32 gdiplus msimg32 d2d1 d3d11 dxgi uuid ole32 shell32 m)
    set_target_properties(host PROPERTIES WIN32_EXECUTABLE TRUE)
else()
    find_package(X11 REQUIRED)
    find_package(CURL)
    find_package(Threads REQUIRED)
    target_link_libraries(host PRIVATE X11::X11 Threads::Threads ${CMAKE_DL_LIBS} m)
    if(CURL_FOUND)
        target_link_libraries(host PRIVATE CURL::libcurl)
        target_compile_definitions(host PRIVATE APP_HAVE_CURL)
    endif()
    target_link_libraries(app_logic PRIVATE m)
    target_compile_options(host PRIVATE -pthread)
    target_compile_options(app_logic PRIVATE -fPIC)
    set_target_properties(host PROPERTIES OUTPUT_NAME "host")
    set_target_properties(app_logic PROPERTIES PREFIX "" SUFFIX ".so")
endif()

set_target_properties(host PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
