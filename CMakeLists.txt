cmake_minimum_required(VERSION 3.13)

set(BOREALIS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/library/borealis)
set(BOREALIS_LIBRARY ${BOREALIS_DIR}/library)

# Include Platform common options
include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)

# Build for Switch
set(PLATFORM_SWITCH ON)

# toolchain
include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

project(StremioNX)
set(VERSION_MAJOR "0")
set(VERSION_MINOR "3")
set(VERSION_REVISION "0")
set(PROJECT_AUTHOR "StremioNX")
set(PACKAGE_NAME com.stremionx.app)
set(APP_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_REVISION}")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/")

find_package(Threads REQUIRED)
list(APPEND APP_PLATFORM_LIB ${CMAKE_THREAD_LIBS_INIT})

find_package(PkgConfig REQUIRED)
pkg_search_module(MPV REQUIRED mpv)
list(APPEND APP_PLATFORM_INCLUDE ${MPV_INCLUDE_DIRS})
list(APPEND APP_PLATFORM_LIB ${MPV_STATIC_LIBRARIES})
link_directories(${MPV_LIBRARY_DIRS})

set(LIBROMFS_PROJECT_NAME ${PROJECT_NAME})
set(LIBROMFS_RESOURCE_LOCATION "${CMAKE_CURRENT_SOURCE_DIR}/resources")
# We will just use the borealis library (which includes lunasvg, glfw, etc)
add_subdirectory(library/borealis/library)

# Torrent engine (vendored from NX-torrent-player, Apache-2.0): streaming
# engine that downloads pieces on demand into a bounded RAM window and exposes
# a blocking byte-range read for mpv's torrent:// stream. No SD writes (RAM
# mode), no web server, no path resolution.
set(TORRENTFS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/vendor/torrentfs)
set(TORRENTFS_SRC
    ${TORRENTFS_DIR}/bencode.c
    ${TORRENTFS_DIR}/torrent.c
    ${TORRENTFS_DIR}/torrentfs.c
    ${TORRENTFS_DIR}/peer.c
    ${TORRENTFS_DIR}/swarm.c
    ${TORRENTFS_DIR}/magnet.c
    ${TORRENTFS_DIR}/udp_tracker.c
    ${TORRENTFS_DIR}/dht.c
    ${TORRENTFS_DIR}/dhtclient.c
    ${TORRENTFS_DIR}/udpdiag.c
    ${TORRENTFS_DIR}/stream.c
    ${TORRENTFS_DIR}/utp.cpp
    ${TORRENTFS_DIR}/utp_utils.cpp
    ${TORRENTFS_DIR}/utpbridge.cpp
)
# libutp needs -DPOSIX for its socket types (SOCKADDR_STORAGE etc).
set_source_files_properties(${TORRENTFS_SRC} PROPERTIES COMPILE_DEFINITIONS "POSIX")

# Our source files
file(GLOB_RECURSE MAIN_SRC source/*.cpp)
list(APPEND MAIN_SRC ${TORRENTFS_SRC})

# System service initialization functions required for libnx
list(APPEND MAIN_SRC ${BOREALIS_DIR}/library/lib/platforms/switch/switch_wrapper.c)

# Main Target
program_target(${PROJECT_NAME} "${MAIN_SRC}")
set_target_properties(${PROJECT_NAME} PROPERTIES
        CXX_STANDARD 17
)

# NRO building
set(BUILD_RESOURCES_DIR ${CMAKE_BINARY_DIR}/resources)
add_custom_target(${PROJECT_NAME}.nro
    DEPENDS ${PROJECT_NAME}
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${BOREALIS_DIR}/resources ${BUILD_RESOURCES_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/resources ${BUILD_RESOURCES_DIR}
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${BUILD_RESOURCES_DIR}/font
    COMMAND ${NX_NACPTOOL_EXE} --create ${PROJECT_NAME} ${PROJECT_AUTHOR} ${APP_VERSION} ${PROJECT_NAME}.nacp
    COMMAND ${NX_ELF2NRO_EXE} ${PROJECT_NAME}.elf ${PROJECT_NAME}.nro --icon=${CMAKE_CURRENT_SOURCE_DIR}/icon.jpg --nacp=${PROJECT_NAME}.nacp --romfsdir=${BUILD_RESOURCES_DIR}
    ALL
)

target_include_directories(${PROJECT_NAME} PRIVATE source ${APP_PLATFORM_INCLUDE} ${TORRENTFS_DIR} ${TORRENTFS_DIR}/include)
target_compile_definitions(${PROJECT_NAME} PRIVATE APP_NAME="${PROJECT_NAME}")
target_compile_options(${PROJECT_NAME} PRIVATE -ffunction-sections -fdata-sections -Wunused-variable)
target_link_libraries(${PROJECT_NAME} PRIVATE borealis curl z webp mbedtls mbedx509 mbedcrypto ${APP_PLATFORM_LIB})
