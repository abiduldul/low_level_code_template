cmake_minimum_required(VERSION 3.13)
project(http C)

add_library(${PROJECT_NAME} http.c)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)