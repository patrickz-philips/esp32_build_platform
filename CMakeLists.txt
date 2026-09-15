# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

# --- Multi-board / multi-app selection --------------------------------------
# The persisted files written by switch_board.sh are the source of truth:
#   priority = BOARD/LVGL_PROJECT environment variable > persisted file > default.
# The value is force-written to the cache on every configure, so switching takes
# effect even in a shared build directory (self-heals a stale cache). The files
# are configure dependencies, so editing them triggers a reconfigure.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/.board"
    "${CMAKE_CURRENT_LIST_DIR}/.lvgl_project")

# resolve_selection(<VAR> <file> <default>)
function(resolve_selection var file default)
    if(DEFINED ENV{${var}})
        set(want "$ENV{${var}}")
    elseif(EXISTS "${file}")
        file(READ "${file}" want)
        string(STRIP "${want}" want)
    else()
        set(want "${default}")
    endif()
    set(${var} "${want}" CACHE STRING "" FORCE)
endfunction()

set(SUPPORTED_BOARDS amoled_175 amoled_206 touch_lcd_1_28)
set(SUPPORTED_PROJECTS slide_player salary_cat acc_data battery_monitor lightring_button)

resolve_selection(BOARD "${CMAKE_CURRENT_LIST_DIR}/.board" amoled_175)
resolve_selection(LVGL_PROJECT "${CMAKE_CURRENT_LIST_DIR}/.lvgl_project" slide_player)

if(NOT BOARD IN_LIST SUPPORTED_BOARDS)
    message(FATAL_ERROR "Unknown BOARD='${BOARD}'. Supported boards: ${SUPPORTED_BOARDS}")
endif()
if(NOT LVGL_PROJECT IN_LIST SUPPORTED_PROJECTS)
    message(FATAL_ERROR "Unknown LVGL_PROJECT='${LVGL_PROJECT}'. Supported: ${SUPPORTED_PROJECTS}")
endif()

set(BOARD_DIR "${CMAKE_CURRENT_LIST_DIR}/boards/${BOARD}")
set(BOARD_SUPPORTED_APPS_FILE "${BOARD_DIR}/supported_apps.txt")
if(NOT EXISTS "${BOARD_SUPPORTED_APPS_FILE}")
    message(FATAL_ERROR "Missing board compatibility file: ${BOARD_SUPPORTED_APPS_FILE}")
endif()

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${BOARD_SUPPORTED_APPS_FILE}")
file(STRINGS "${BOARD_SUPPORTED_APPS_FILE}" BOARD_SUPPORTED_APP_LINES)
set(BOARD_SUPPORTED_APPS "")
foreach(app IN LISTS BOARD_SUPPORTED_APP_LINES)
    string(STRIP "${app}" app)
    if(NOT app STREQUAL "" AND NOT app MATCHES "^#")
        if(NOT app IN_LIST SUPPORTED_PROJECTS)
            message(FATAL_ERROR
                "Unknown app '${app}' in board compatibility file: ${BOARD_SUPPORTED_APPS_FILE}")
        endif()
        list(APPEND BOARD_SUPPORTED_APPS "${app}")
    endif()
endforeach()

if(NOT BOARD_SUPPORTED_APPS)
    message(FATAL_ERROR "Board '${BOARD}' has no supported apps in ${BOARD_SUPPORTED_APPS_FILE}")
endif()
if(NOT LVGL_PROJECT IN_LIST BOARD_SUPPORTED_APPS)
    string(JOIN ", " BOARD_SUPPORTED_APPS_DISPLAY ${BOARD_SUPPORTED_APPS})
    message(FATAL_ERROR
        "Incompatible board/app pair: BOARD='${BOARD}', LVGL_PROJECT='${LVGL_PROJECT}'. "
        "Supported apps for ${BOARD}: ${BOARD_SUPPORTED_APPS_DISPLAY}")
endif()

message(STATUS "Building for BOARD=${BOARD}")
message(STATUS "Building LVGL_PROJECT=${LVGL_PROJECT}")

if(BOARD STREQUAL "amoled_175")
    set(SELECTED_BSP_COMPONENT "waveshare__esp32_s3_touch_amoled_1_75")
elseif(BOARD STREQUAL "amoled_206")
    set(SELECTED_BSP_COMPONENT "waveshare__esp32_s3_touch_amoled_2_06")
elseif(BOARD STREQUAL "touch_lcd_1_28")
    # This board owns its BSP: the board component compiles bsp/ directly, so it
    # is its own BSP component (no vendor package on the ESP Component Registry).
    set(SELECTED_BSP_COMPONENT "touch_lcd_1_28")
endif()

# Export BOARD_HAS_SD so app components can drop SD-only sources and the sdmmc
# dependency for boards without a micro-SD slot. Default to 1 when absent.
file(READ "${BOARD_DIR}/board_config.h" BOARD_CONFIG_CONTENT)
if(BOARD_CONFIG_CONTENT MATCHES "BOARD_HAS_SD[^0-9]*([0-9]+)")
    set(ENV{BOARD_HAS_SD} "${CMAKE_MATCH_1}")
else()
    set(ENV{BOARD_HAS_SD} "1")
endif()

# Component requirement expansion runs in a child CMake process. Export the
# resolved selection so normal components can declare the selected BSP without
# depending on both board packages.
set(ENV{SELECTED_BOARD} "${BOARD}")
set(ENV{SELECTED_BSP_COMPONENT} "${SELECTED_BSP_COMPONENT}")
# ----------------------------------------------------------------------------

set(APP_DIR "${CMAKE_CURRENT_LIST_DIR}/main/app_${LVGL_PROJECT}")
if(NOT EXISTS "${APP_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Missing selected app component: ${APP_DIR}/CMakeLists.txt")
endif()

# Shared services with no selected consumer must not pull in board-specific
# dependencies during ESP-IDF's component dependency expansion pass.
if(LVGL_PROJECT STREQUAL "slide_player")
    list(APPEND EXCLUDE_COMPONENTS pmu_power)
endif()

# Register only the selected board and app components.
set(EXTRA_COMPONENT_DIRS "${BOARD_DIR}" "${APP_DIR}")

# Per-board generated config + layered defaults (board file overrides common).
set(SDKCONFIG "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.${BOARD}")
set(SDKCONFIG_DEFAULTS
    "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults;${BOARD_DIR}/sdkconfig.board")
if(EXISTS "${APP_DIR}/sdkconfig.app")
    list(APPEND SDKCONFIG_DEFAULTS "${APP_DIR}/sdkconfig.app")
endif()
# ----------------------------------------------------------------------------

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

add_compile_options(-Wno-format)
project(app_${BOARD}_${LVGL_PROJECT})
