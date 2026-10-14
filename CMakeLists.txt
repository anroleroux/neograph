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
    src/keyboard.c
    src/usb_descriptors.c
    src/networking/dhcpserver.c
    src/networking/ws_frame.c
)

target_compile_definitions(twitcher PRIVATE
    CYW43_COUNTRY_CODE=CYW43_COUNTRY_SOUTH_AFRICA
    # The app links TinyUSB itself for the keyboard (src/keyboard.c) and supplies the
    # descriptors (src/usb_descriptors.c); pico_stdio_usb still starts TinyUSB and
    # runs its task in the background, as it does on its own.
    PICO_STDIO_USB_ENABLE_TINYUSB_INIT=1
    PICO_STDIO_USB_ENABLE_IRQ_BACKGROUND_TASK=1
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
    tinyusb_device
    m
)

# Enable USB output, disable UART output. The USB port is also a keyboard.
pico_enable_stdio_usb(twitcher 1)
pico_enable_stdio_uart(twitcher 0)

pico_add_extra_outputs(twitcher)