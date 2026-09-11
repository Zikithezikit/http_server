idf_component_register(SRCS "app_main.c"
                            "wifi.c"
                            "tcp_server.c"
                            "http_server.c"
                            "ssd1306.c"
                    INCLUDE_DIRS "."
                                "include")

# Flash the contents of 'file-data' into the 'storage' partition
littlefs_create_partition_image(storage ../file-data FLASH_IN_PROJECT)

# WiFi credentials are read from the project .env file (see .env.example at
# the project root). Only WIFI_SSID and WIFI_PASSWORD are forwarded as compile
# definitions, so the file may hold other values without affecting the build.
set(WIFI_ENV_FILE "${CMAKE_CURRENT_LIST_DIR}/../.env")
if(EXISTS "${WIFI_ENV_FILE}")
    set_property(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}" APPEND
                 PROPERTY CMAKE_CONFIGURE_DEPENDS "${WIFI_ENV_FILE}")
    file(STRINGS "${WIFI_ENV_FILE}" WIFI_ENV_LINES)
    foreach(WIFI_ENV_LINE IN LISTS WIFI_ENV_LINES)
        string(STRIP "${WIFI_ENV_LINE}" WIFI_ENV_LINE)
        if(WIFI_ENV_LINE MATCHES "^(WIFI_SSID|WIFI_PASSWORD)=(.*)$")
            set(WIFI_KEY "${CMAKE_MATCH_1}")
            set(WIFI_VAL "${CMAKE_MATCH_2}")
            target_compile_definitions(${COMPONENT_LIB} PRIVATE
                                       "${WIFI_KEY}=\"${WIFI_VAL}\"")
        endif()
    endforeach()
else()
    message(WARNING "No .env found (${WIFI_ENV_FILE}). Copy ${CMAKE_CURRENT_LIST_DIR}/../.env.example to .env to configure WiFi.")
endif()
