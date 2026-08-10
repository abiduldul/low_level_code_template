cmake_minimum_required(VERSION 3.13)
project(modbus C)

add_library(${PROJECT_NAME} modbus.c)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)