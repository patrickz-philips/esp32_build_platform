if(NOT DEFINED ENV{SELECTED_BOARD})
	message(FATAL_ERROR "Selected board component was not exported by the project")
endif()

idf_component_register(
	SRCS
		"app_hairdryer.c"
	PRIV_REQUIRES
		esp_driver_gpio
		freertos
		log
		$ENV{SELECTED_BOARD})
