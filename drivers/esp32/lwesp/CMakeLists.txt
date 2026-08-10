cmake_minimum_required(VERSION 3.22)

# Setup project
project(LwLibPROJECT)

if(NOT PROJECT_IS_TOP_LEVEL)
    add_subdirectory(lwesp)
else()
    add_executable(${PROJECT_NAME})
    target_sources(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/dev/main.c

        # Development additional files
        ${CMAKE_CURRENT_LIST_DIR}/lwesp/src/system/lwesp_mem_lwmem.c
        ${CMAKE_CURRENT_LIST_DIR}/../lwmem/lwmem/src/lwmem/lwmem.c
        ${CMAKE_CURRENT_LIST_DIR}/../lwmem/lwmem/src/system/lwmem_sys_win32.c

        # Http server file system
        ${CMAKE_CURRENT_LIST_DIR}/lwesp/src/apps/http_server/lwesp_http_server_fs_win32.c

        # Port specific
        ${CMAKE_CURRENT_LIST_DIR}/lwesp/src/system/lwesp_ll_win32.c
    )
    target_include_directories(${PROJECT_NAME} PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/dev
        ${CMAKE_CURRENT_LIST_DIR}/../lwmem/lwmem/src/include
    )

    # Add subdir with lwesp and link to the project
    set(LWESP_OPTS_FILE ${CMAKE_CURRENT_LIST_DIR}/dev/lwesp_opts.h)
    set(LWESP_SYS_PORT "win32")
    add_subdirectory(lwesp)
    target_link_libraries(${PROJECT_NAME} PUBLIC lwesp_api)
    target_link_libraries(${PROJECT_NAME} PUBLIC lwesp_apps)
    target_link_libraries(${PROJECT_NAME} PUBLIC lwesp)
    add_subdirectory(snippets)
    target_link_libraries(${PROJECT_NAME} PUBLIC lwesp_snippets)

    # Add compile options to the library, which will propagate options to executable through public link
    target_compile_definitions(lwesp PUBLIC WIN32 _DEBUG CONSOLE LWESP_DEV)
    target_compile_options(lwesp PUBLIC -Wall -Wextra -Wpedantic)
endif()
