cmake_minimum_required(VERSION 3.13)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
project(mertani_firmware C ASM)

set(LWESP_SYS_PORT "threadx")

set(APP "app_ewspens_receiver" CACHE STRING "Target Application to build")

set(VALID_APPS "app_ewspens_sender" 
    "app_ewspens_receiver" 
    "app_testing_filemanager" 
    "app_testing_lwmqtt"
    "app_testing_modbusclient"
    "app_testing_sensor"
    "app_testing_esp32"
    "app_testing_awl_2_5")

list(FIND VALID_APPS "${APP}" APP_INDEX)
if(APP_INDEX EQUAL -1)
    message(FATAL_ERROR "Invalid app '${APP}' selected! Valid option are: ${VALID_APPS}")
endif()

message(STATUS ">>> Configuring build for app: ${APP} <<<")

set(APP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/app/${APP}")

if(NOT EXISTS "${APP_DIR}")
    message(FATAL_ERROR "Directory for app '${APP}' not found at: ${APP_DIR}")
endif()

add_subdirectory(middlewares/filex)
add_subdirectory(middlewares/threadx)
add_subdirectory(middlewares/log)
add_subdirectory(middlewares/jsonparser)
add_subdirectory(middlewares/http)
add_subdirectory(drivers/esp32/lwesp)

target_include_directories(lwesp BEFORE PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/drivers/esp32)
target_include_directories(lwesp_api BEFORE PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/drivers/esp32)
target_include_directories(lwesp_apps BEFORE PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/drivers/esp32)

target_link_libraries(lwesp PUBLIC threadx)
target_link_libraries(lwesp_apps PUBLIC threadx)
target_link_libraries(lwesp_api PUBLIC threadx)

set(COMPILE_SOURCES
main.c
threads/rtos.c
threads/FileManager/FileManager.c
threads/Logger/Logger.c
threads/FileManager/filex.c
threads/ModbusClient/modbusclient.c
threads/NetworkManager/networkmanager.c
threads/Clock/clock.c
sources/sensor.c
utils/utils.c
drivers/esp32/esp32.c
drivers/esp32/esp32_http_transport.c
drivers/esp32/esp32_net_interface.c
drivers/esp32/lwmqtt.c)

file(GLOB SENSOR_SOURCES "io/*.c")
file(GLOB APP_SOURCES "${APP_DIR}/*.c")

list(APPEND COMPILE_SOURCES ${SENSOR_SOURCES} ${APP_SOURCES} app/App.c)

include_directories(
    app
    bsp
    includes
    utils
)

if(CMAKE_SYSTEM_NAME STREQUAL "Generic")
    message(STATUS "Building for Mertani Board 2.5")
    add_compile_definitions(USE_HAL_DRIVER STM32L4P5xx)

    file(GLOB MERTANI_SOURCES "targets/mertani-2.5/src/*.c" 
    "targets/mertani-2.5/src/*.s" 
    "targets/mertani-2.5/src/*.S"
    "targets/mertani-2.5/src/lwesp/*.c"
    "targets/mertani-2.5/src/filex/*.c")

    file(GLOB THREADX_PORTS_CORTEX_M4_SOURCES "middlewares/threadx/ports/cortex_m4/gnu/src/*.s")

    set(HAL_SOURCES
        targets/mertani-2.5/drivers/cmsis-device-l4/Source/Templates/system_stm32l4xx.c

        targets/mertani-2.5/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal.c 
        targets/mertani-2.5/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_cortex.c
        targets/mertani-2.5/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_gpio.c
        targets/mertani-2.5/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_rcc.c
        targets/mertani-2.5/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_rcc_ex.c
        targets/mertani-2.5/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_uart.c
        targets/mertani-2.5/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_uart_ex.c
        targets/mertani-2.5/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_dma.c
        targets/mertani-2.5/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_dma_ex.c
        targets/mertani-2.5/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_pwr.c
        targets/mertani-2.5/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_pwr_ex.c
        targets/mertani-2.5/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_sd.c
        targets/mertani-2.5/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_sd_ex.c
        targets/mertani-2.5/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_ll_sdmmc.c
        targets/mertani-2.5/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_spi.c
        targets/mertani-2.5/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_spi_ex.c
        targets/mertani-2.5/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_adc.c
        targets/mertani-2.5/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_adc_ex.c
    )

    list(APPEND COMPILE_SOURCES ${HAL_SOURCES} ${MERTANI_SOURCES} ${THREADX_PORTS_CORTEX_M4_SOURCES})

    add_executable(firmware.elf ${COMPILE_SOURCES})

    target_compile_definitions(firmware.elf PRIVATE 
        -DSELECTED_APP_NAME=${APP}
        -DBUILD_APP_${APP}=1 
        -DSTM32L4P5xx
        LWJSON_IGNORE_USER_OPTS)

    target_include_directories(firmware.elf PRIVATE
        ${APP_DIR}
        targets/mertani-2.5/drivers/CMSIS_6/CMSIS/Core/Include
        targets/mertani-2.5/drivers/cmsis-device-l4/Include
        targets/mertani-2.5/drivers/stm32l4xx-hal-driver/Inc)

    target_link_options(firmware.elf PRIVATE
        -T${CMAKE_CURRENT_SOURCE_DIR}/targets/mertani-2.5/linker.ld
        -Wl,--gc-sections
        "-specs=nosys.specs"  
        -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/firmware.map
        -u_printf_float  
    )

    target_link_libraries(firmware.elf PRIVATE threadx filex log jsonparser lwesp_apps lwesp_api lwesp http)

    add_custom_command(TARGET firmware.elf POST_BUILD 
        COMMAND arm-none-eabi-objdump -S -d $<TARGET_FILE:firmware.elf> > 
        ${CMAKE_CURRENT_BINARY_DIR}/firmware.objdump
        COMMENT "Generating disassembly file: firmware.objdump"
    )

    add_custom_command(TARGET firmware.elf POST_BUILD
        COMMAND arm-none-eabi-objcopy -O binary $<TARGET_FILE:firmware.elf>
        ${CMAKE_CURRENT_BINARY_DIR}/firmware.bin
        COMMENT "Generating binary file: firmware.bin"
    )
elseif(CMAKE_SYSTEM_NAME STREQUAL "Generic_HAL")
    message(STATUS "Building for Mertani Board 2.5 Using HALLLL")
    add_compile_definitions(USE_HAL_DRIVER STM32L4P5xx)

    file(GLOB MERTANI_SOURCES "targets/mertani-2.5_HAL/src/*.c" 
    "targets/mertani-2.5_HAL/src/*.s" 
    "targets/mertani-2.5_HAL/src/*.S"
    "targets/mertani-2.5_HAL/src/lwesp/*.c"
    "targets/mertani-2.5_HAL/src/filex/*.c")

    file(GLOB THREADX_PORTS_CORTEX_M4_SOURCES "middlewares/threadx/ports/cortex_m4/gnu/src/*.s")

    set(HAL_SOURCES
        targets/mertani-2.5_HAL/drivers/cmsis-device-l4/Source/Templates/system_stm32l4xx.c

        targets/mertani-2.5_HAL/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal.c 
        targets/mertani-2.5_HAL/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_cortex.c
        targets/mertani-2.5_HAL/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_gpio.c
        targets/mertani-2.5_HAL/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_rcc.c
        targets/mertani-2.5_HAL/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_rcc_ex.c
        targets/mertani-2.5_HAL/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_uart.c
        targets/mertani-2.5_HAL/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_uart_ex.c
        targets/mertani-2.5_HAL/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_dma.c
        targets/mertani-2.5_HAL/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_dma_ex.c
        targets/mertani-2.5_HAL/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_pwr.c
        targets/mertani-2.5_HAL/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_pwr_ex.c
        targets/mertani-2.5_HAL/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_sd.c
        targets/mertani-2.5_HAL/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_sd_ex.c
        targets/mertani-2.5_HAL/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_ll_sdmmc.c
        targets/mertani-2.5_HAL/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_spi.c
        targets/mertani-2.5_HAL/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_spi_ex.c
        targets/mertani-2.5_HAL/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_adc.c
        targets/mertani-2.5_HAL/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_adc_ex.c
        targets/mertani-2.5_HAL/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_iwdg.c
        targets/mertani-2.5_HAL/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_tim.c
        targets/mertani-2.5_HAL/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_tim_ex.c
        targets/mertani-2.5_HAL/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_rtc.c
        targets/mertani-2.5_HAL/drivers/stm32l4xx-hal-driver/Src/stm32l4xx_hal_rtc_ex.c
        
    )

    list(APPEND COMPILE_SOURCES ${HAL_SOURCES} ${MERTANI_SOURCES} ${THREADX_PORTS_CORTEX_M4_SOURCES})

    add_executable(firmware.elf ${COMPILE_SOURCES})

    target_compile_definitions(firmware.elf PRIVATE 
        -DSELECTED_APP_NAME=${APP}
        -DBUILD_APP_${APP}=1 
        -DSTM32L4P5xx
        LWJSON_IGNORE_USER_OPTS)

    target_include_directories(firmware.elf PRIVATE
        ${APP_DIR}
        targets/mertani-2.5_HAL/drivers/CMSIS_6/CMSIS/Core/Include
        targets/mertani-2.5_HAL/drivers/cmsis-device-l4/Include
        targets/mertani-2.5_HAL/drivers/stm32l4xx-hal-driver/Inc)

    target_link_options(firmware.elf PRIVATE
        -T${CMAKE_CURRENT_SOURCE_DIR}/targets/mertani-2.5_HAL/linker.ld
        -Wl,--gc-sections
        "-specs=nosys.specs"  
        -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/firmware.map
        -u_printf_float  
    )

    target_link_libraries(firmware.elf PRIVATE threadx filex log jsonparser lwesp_apps lwesp_api lwesp http)

    add_custom_command(TARGET firmware.elf POST_BUILD 
        COMMAND arm-none-eabi-objdump -S -d $<TARGET_FILE:firmware.elf> > 
        ${CMAKE_CURRENT_BINARY_DIR}/firmware.objdump
        COMMENT "Generating disassembly file: firmware.objdump"
    )

    add_custom_command(TARGET firmware.elf POST_BUILD
        COMMAND arm-none-eabi-objcopy -O binary $<TARGET_FILE:firmware.elf>
        ${CMAKE_CURRENT_BINARY_DIR}/firmware.bin
        COMMENT "Generating binary file: firmware.bin"
    )
else()
    message(STATUS "Building for simulation")

    file(GLOB LINUX_SOURCES "targets/linux/src/*.c" "targets/linux/src/*.s" 
    "targets/linux/src/filex/*.c"
    "targets/linux/src/vport/*.c"
    "targets/linux/src/lwesp/*.c"
    )
    
    file(GLOB THREADX_PORTS_LINUX_SOURCES "middlewares/threadx/ports/linux/gnu/src/*.c")

    list(APPEND COMPILE_SOURCES ${LINUX_SOURCES})

    add_executable(linux_sim ${COMPILE_SOURCES})

    target_compile_definitions(linux_sim PRIVATE 
        -DSELECTED_APP_NAME="${APP}"
        -DBUILD_APP_${APP}=1 
        LWJSON_IGNORE_USER_OPTS)

    target_include_directories(linux_sim PRIVATE
        ${APP_DIR}         
        targets/linux/include
    )

    target_link_options(linux_sim PRIVATE 
        -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/linux_sim.map
    )

    target_link_libraries(linux_sim PRIVATE threadx filex log jsonparser lwesp_apps lwesp_api lwesp http)

    add_custom_command(TARGET linux_sim POST_BUILD
        COMMAND objdump -S -d $<TARGET_FILE:linux_sim> > ${CMAKE_CURRENT_BINARY_DIR}/linux_sim.objdump
        COMMENT "Generating disassembly file: linux_sim.objdump"
    )
endif()