set(CMAKE_SYSTEM_NAME Linux)
set(TARGET_NAME linux_sim)

set(CMAKE_C_COMPILER gcc)
set(CMAKE_ASM_COMPILER gcc)

set(THREADX_ARCH linux CACHE STRING "Threadx Architecture")
set(THREADX_TOOLCHAIN gnu CACHE STRING "ThreadX Toolchain")

set(CMAKE_C_FLAGS "-m32 -DTX_INCLUDE_USER_DEFINE_FILE -DTX_TIMER_TICKS_PER_SECOND=1000 -g -O0 -Wall -Wno-discarded-qualifiers" CACHE INTERNAL "C Compiler flags")
set(CMAKE_ASM_FLAGS "-m32 -DTX_INCLUDE_USER_DEFINE_FILE -DTX_TIMER_TICKS_PER_SECOND=1000 -g -Wno-discarded-qualifiers" CACHE INTERNAL "ASM Compiler flags")