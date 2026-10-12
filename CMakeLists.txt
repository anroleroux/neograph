cmake_minimum_required(VERSION 3.13...3.27)

include(pico_sdk_import.cmake)

project(twitcher)

pico_sdk_init()

#/* Reserve 4KB at the end of flash for settings */
# __flash_settings_start = ORIGIN(flash) + LENGTH(flash) - 0x1000;

add_executable(twitcher
    src/main.c
    src/wifi.c
    src/tcpserver.c
    src/websockets.c
    src/config.c
    src/config/config_iterator.c
    src/hwman.c
    src/networking/dhcpserver.c
    src/networking/ws_frame.c
)

target_compile_definitions(twitcher PRIVATE
    CYW43_COUNTRY_CODE=CYW43_COUNTRY_SOUTH_AFRICA
)

target_include_directories(twitcher PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${CMAKE_CURRENT_LIST_DIR}/src
)

target_link_libraries(twitcher
    pico_stdlib
    hardware_adc
    hardware_pwm
    pico_mbedtls
    pico_multicore
    pico_cyw43_arch_lwip_poll
)

# Enable USB output, disable UART output
pico_enable_stdio_usb(twitcher 1)
pico_enable_stdio_uart(twitcher 0)

pico_add_extra_outputs(twitcher)