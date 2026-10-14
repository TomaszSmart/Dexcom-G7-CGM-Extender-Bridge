cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(ble_bridge)

target_sources(app PRIVATE
    src/main.c
    src/g7_protocol.c
    src/g7_config.c
    src/g7_shell.c
    src/g7_jpake.c
    src/g7_auth.c
    src/g7_history.c
    src/g7_cgms.c
    src/g7_ui.c
    src/g7_led.c
    src/g7_battery.c
    src/g7_certstore.c
)
