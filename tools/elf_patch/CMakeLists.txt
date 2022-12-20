cmake_minimum_required(VERSION 3.20)


#################### Build config settings ####################



project(elf_patch
  VERSION 0.9.0
  LANGUAGES C
)

configure_file (
  src/build_config.h.in
  src/build_config.h
)

####################               ####################
#################### BUILD TARGETS ####################
####################               ####################

#################### elf_patch ####################

set(CSTONE_UTIL  "../libraries/cstone/src/util")

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

add_executable(elf_patch  ${APP_SOURCE_COMMON})

target_include_directories(elf_patch
  PUBLIC
    "../libraries/cstone/include"
    "../libraries/cstone/include/util"
)

target_link_libraries(elf_patch
  PRIVATE
    elf
)


#################### Installation ####################

install(TARGETS elf_patch
  DESTINATION "bin"
)

