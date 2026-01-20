cmake_minimum_required(VERSION 3.10)
project(. C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror -pthread -D_POSIX_C_SOURCE=200809L")
set(CMAKE_C_FLAGS_DEBUG "-g -O0")
set(CMAKE_C_FLAGS_RELEASE "-O2")

set(SERVER_DIR ${CMAKE_SOURCE_DIR}/server)
set(CLIENT_DIR ${CMAKE_SOURCE_DIR}/client)
set(PROTOCOL_DIR ${CMAKE_SOURCE_DIR}/protocol)

add_library(common STATIC
    ${PROTOCOL_DIR}/resp.c
    ${PROTOCOL_DIR}/resp.h
)

target_include_directories(common PUBLIC ${PROTOCOL_DIR})

file(GLOB_RECURSE SERVER_SOURCES
    ${SERVER_DIR}/app/*.c
    ${SERVER_DIR}/model/*.c
    ${SERVER_DIR}/service/*.c
    ${SERVER_DIR}/adapter/in/*.c
    ${SERVER_DIR}/adapter/out/*.c
    ${SERVER_DIR}/logger/*.c
)

add_executable(repa
    ${SERVER_DIR}/main.c
    ${SERVER_SOURCES}
)

target_include_directories(repa PRIVATE
    ${SERVER_DIR}
    ${SERVER_DIR}/app
    ${SERVER_DIR}/model
    ${SERVER_DIR}/service
    ${SERVER_DIR}/adapter/in
    ${SERVER_DIR}/adapter/out
    ${SERVER_DIR}/logger
    ${PROTOCOL_DIR}
)

target_link_libraries(repa
    common
    pthread
)

set_target_properties(repa PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)

file(GLOB_RECURSE CLIENT_SOURCES
    ${CLIENT_DIR}/app/*.c
    ${CLIENT_DIR}/model/*.c
    ${CLIENT_DIR}/service/*.c
    ${CLIENT_DIR}/adapter/in/*.c
    ${CLIENT_DIR}/adapter/out/*.c
)

add_executable(repactl
    ${CLIENT_DIR}/main.c
    ${CLIENT_SOURCES}
)

target_include_directories(repactl PRIVATE
    ${CLIENT_DIR}
    ${CLIENT_DIR}/app
    ${CLIENT_DIR}/model
    ${CLIENT_DIR}/service
    ${CLIENT_DIR}/adapter/in
    ${CLIENT_DIR}/adapter/out
    ${PROTOCOL_DIR}
)

target_link_libraries(repactl
    common
    pthread
)

if(APPLE)
    target_link_libraries(repactl edit)
else()
    find_library(READLINE_LIBRARY readline)
    find_library(HISTORY_LIBRARY history)
    if(READLINE_LIBRARY)
        target_link_libraries(repactl ${READLINE_LIBRARY})
        if(HISTORY_LIBRARY)
            target_link_libraries(repactl ${HISTORY_LIBRARY})
        endif()
    else()
        message(WARNING "readline library not found. History support will be limited.")
    endif()
endif()

set_target_properties(repactl PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin
)

file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)

configure_file(
    ${CMAKE_SOURCE_DIR}/repa.conf
    ${CMAKE_SOURCE_DIR}/bin/repa.conf
    COPYONLY
)

add_custom_target(server
    DEPENDS repa
)

add_custom_target(client
    DEPENDS repactl
)

message(STATUS "===========================================")
message(STATUS "Repa In-Memory Key-Value Store")
message(STATUS "===========================================")
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C Compiler: ${CMAKE_C_COMPILER}")
message(STATUS "C Flags: ${CMAKE_C_FLAGS}")
message(STATUS "Server sources: ${SERVER_DIR}")
message(STATUS "Client sources: ${CLIENT_DIR}")
message(STATUS "Protocol sources: ${PROTOCOL_DIR}")
message(STATUS "Output directory: ${CMAKE_SOURCE_DIR}/bin")
message(STATUS "===========================================")
