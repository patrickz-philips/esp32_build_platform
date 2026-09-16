set(SLIDE_PLAYER_UI_DIR "${CMAKE_SOURCE_DIR}/lvgl/slide_player")

if(NOT DEFINED ENV{SELECTED_BOARD})
	message(FATAL_ERROR "Selected board component was not exported by the project")
endif()
if(NOT DEFINED ENV{SELECTED_BSP_COMPONENT})
	message(FATAL_ERROR "Selected BSP component was not exported by the project")
endif()

idf_component_register(
	SRCS
		"app_slide_player.c"
		"${SLIDE_PLAYER_UI_DIR}/src/slide_player.c"
	INCLUDE_DIRS
		"."
		"${SLIDE_PLAYER_UI_DIR}/inc"
	PRIV_REQUIRES
		esp_driver_gpio
		esp_common
		esp_lv_decoder
		esp_timer
		freertos
		heap
		log
		lvgl__lvgl
		$ENV{SELECTED_BOARD}
		$ENV{SELECTED_BSP_COMPONENT})

target_compile_definitions(${COMPONENT_LIB} PRIVATE LVGL_IN_USED=1)

if("$ENV{SELECTED_BOARD}" STREQUAL "amoled_175")
	target_compile_definitions(${COMPONENT_LIB} PRIVATE SLIDE_PLAYER_BOARD_AMOLED_175=1)
endif()