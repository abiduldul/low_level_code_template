set(CMAKE_SYSTEM_NAME Generic_HAL)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(TARGET_NAME firmware.elf)

set(CMAKE_C_COMPILER arm-none-eabi-gcc)
set(CMAKE_ASM_COMPILER arm-none-eabi-gcc)

set(THREADX_ARCH cortex_m4 CACHE STRING "ThreadX Architecture")
set(THREADX_TOOLCHAIN gnu CACHE STRING "ThreadX Toolchain")

set(CMAKE_C_FLAGS "-DTX_INCLUDE_USER_DEFINE_FILE -DTX_TIMER_TICKS_PER_SECOND=1000 -g -ffunction-sections -fdata-sections -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -u _printf_float -O0 -Wall --specs=nano.specs -Wl,--gc-sections" CACHE INTERNAL "C Compiler flags")
set(CMAKE_ASM_FLAGS "-DTX_INCLUDE_USER_DEFINE_FILE -DTX_TIMER_TICKS_PER_SECOND=1000 -g -ffunction-sections -fdata-sections -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -u _printf_float -O0 --specs=nano.specs -Wl,--gc-sections -x assembler-with-cpp" CACHE INTERNAL "ASM Compiler flags")

