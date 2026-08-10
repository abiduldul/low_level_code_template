cmake_minimum_required(VERSION 3.13)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
project(mertani_firmware C ASM)

set(APP_SOURCES
startup.s
syscalls.c
sysmem.c
main.c)

include_directories(after PUBLIC
    drivers/CMSIS_6/CMSIS/Core/Include
    drivers/cmsis-device-l4/Include
    UartProcess
    CircularBuffer
    include/

)

file(GLOB MERTANI_SOURCES "src/*.c")

list(APPEND APP_SOURCES ${MERTANI_SOURCES}
"CircularBuffer/CircularBuffer.c"
"UartProcess/UartProcess.c")

add_executable(firmware.elf ${APP_SOURCES})

target_link_options(firmware.elf PRIVATE
    -T${CMAKE_CURRENT_SOURCE_DIR}/linker.ld
    -Wl,--gc-sections
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/firmware.map)

add_custom_command(TARGET firmware.elf POST_BUILD 
    COMMAND arm-none-eabi-objdump -S -d $<TARGET_FILE:firmware.elf> > 
    ${CMAKE_CURRENT_BINARY_DIR}/firmware.objdump
    COMMENT "Generating disassembly file: firmware.objdump")

add_custom_command(TARGET firmware.elf POST_BUILD
    COMMAND arm-none-eabi-objcopy -O binary $<TARGET_FILE:firmware.elf>
    ${CMAKE_CURRENT_BINARY_DIR}/firmware.bin
    COMMENT "Generating binary file: firmware.bin")