cmake_minimum_required(VERSION 3.13)
project(jsonparser C)

#set(CMAKE_C_FLAGS "-DLWJSON_IGNORE_USER_OPTS")
add_compile_definitions(-DLWJSON_IGNORE_USER_OPTS PRId64="lld" PRIu64="llu")

add_library(${PROJECT_NAME} src/jsonparser.c)
add_subdirectory(lwjson)

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/lwjson/lwjson/src/include
)

target_link_libraries(jsonparser PRIVATE lwjson)