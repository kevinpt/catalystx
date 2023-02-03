cmake_minimum_required(VERSION 3.20)

# ACTIONS:
#
# Configure:
#  cmake . -B build -DCMAKE_INSTALL_PREFIX=/usr/local
#
# Build:
#  cmake --build build --target elf-patch
#
# Install:
#  sudo cmake --build build --target install


#################### Build config settings ####################



project(elf_patch
  VERSION 1.0.0
  LANGUAGES C
)

configure_file (
  src/build_config.h.in
  src/build_config.h
)

####################               ####################
#################### BUILD TARGETS ####################
####################               ####################

#################### elf-patch ####################

set(CSTONE_DIR  "../../libraries/cstone")
set(CSTONE_UTIL  "${CSTONE_DIR}/src/util")


set(APP_SOURCE_COMMON
    src/elf_patch.c
    ${CSTONE_UTIL}/range_strings.c
    ${CSTONE_UTIL}/intmath.c
    ${CSTONE_UTIL}/glob.c
    ${CSTONE_UTIL}/getopt_r.c
    ${CSTONE_UTIL}/crc16.c
    ${CSTONE_UTIL}/crc32.c
    ${CSTONE_UTIL}/hex_dump.c
)

add_executable(elf-patch  ${APP_SOURCE_COMMON})

target_include_directories(elf-patch
  PUBLIC
    ${CSTONE_DIR}/include
    ${CSTONE_DIR}/include/util
    ${CMAKE_BINARY_DIR}/src
)

target_link_libraries(elf-patch
  PRIVATE
    elf
)


#################### Installation ####################

install(TARGETS elf-patch
  DESTINATION "bin"
)

