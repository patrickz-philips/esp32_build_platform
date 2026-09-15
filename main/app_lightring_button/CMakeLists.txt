if(NOT DEFINED ENV{SELECTED_BOARD})
	message(FATAL_ERROR "Selected board component was not exported by the project")
endif()

idf_component_register(
	SRCS
		"app_lightring_button.c"
		"lightring/src/lightring.c"
		"lightring/src/lightring_effects.c"
		"lightring/src/lightring_palette.c"
		"lightring/src/ws2812_esp.c"
	INCLUDE_DIRS
		"lightring/include"
	PRIV_REQUIRES
		esp_driver_gpio
		esp_driver_rmt
		freertos
		log
		$ENV{SELECTED_BOARD})