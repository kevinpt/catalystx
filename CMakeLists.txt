cmake_minimum_required(VERSION 3.15)


#################### Build config settings ####################

set(BUILD_PLATFORM "stm32f1"    CACHE STRING "Target platform")
set(BUILD_BOARD    "none"       CACHE STRING "Target board")
set(LINKER_SCRIPT  ""           CACHE STRING "Linker script for board")
option(USE_TINYUSB        "Enable Tinyusb driver"           OFF)
option(USE_NEWLIB_NANO    "Enable Newlib nano C library"    OFF)
option(USE_MINIMAL_TASKS  "Reduce core task set to minimum" OFF)
option(USE_FILESYSTEM     "Enable EVFS filesystem"          OFF)

set(PLATFORM_HOSTED   OFF)
set(PLATFORM_STM32    OFF)
set(PLATFORM_STM32F1  OFF)
set(PLATFORM_STM32F4  OFF)

set(BOARD_MAPLE_MINI        OFF)
set(BOARD_STM32F429I_DISC1  OFF)
set(BOARD_UNKNOWN           OFF)

if(BUILD_PLATFORM MATCHES "stm32f1")
  set(PLATFORM_STM32   ON)
  set(PLATFORM_STM32F1 ON)
elseif(BUILD_PLATFORM MATCHES "stm32f4")
  set(PLATFORM_STM32   ON)
  set(PLATFORM_STM32F4 ON)
elseif(BUILD_PLATFORM MATCHES "hosted")
  set(PLATFORM_HOSTED  ON)
endif()

if(BUILD_BOARD MATCHES "maple_mini")
  set(BOARD_MAPLE_MINI ON)
elseif(BUILD_BOARD MATCHES "stm32f429i_disc1")
  set(BOARD_STM32F429I_DISC1 ON)
else()
  set(BOARD_UNKNOWN ON)
endif()

#message(STATUS "PLATFORM_STM32 ${PLATFORM_STM32}")
#message(STATUS "PLATFORM_HOSTED ${PLATFORM_HOSTED}")

if(PLATFORM_HOSTED)
  include(scripts/build_pc.cmake)
endif()

if(PLATFORM_STM32)
  include(scripts/build_stm32.cmake)
endif()

project(catalyst
  VERSION 0.1.0
  LANGUAGES C CXX ASM
)

configure_file (
  include/lib_cfg/build_config.h.in
  include/lib_cfg/build_config.h
)


#################### Target config ####################
if(PLATFORM_STM32F1)
  include(scripts/stm32f1_cfg.cmake)
#  set(LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/stm32/STM32F103CBTx_FLASH.ld")
elseif(PLATFORM_STM32F4)
  include(scripts/stm32f4_cfg.cmake)
#  set(LINKER_SCRIPT "${CMAKE_SOURCE_DIR}/scripts/stm32/STM32F429ZITx_FLASH.ld")
endif()


####################           ####################
#################### LIBRARIES ####################
####################           ####################


#################### libfreertos (static) ####################
set(FREERTOS_ROOT "${CMAKE_SOURCE_DIR}/libraries/FreeRTOS-Kernel")

set(FREERTOS_SOURCE
  "${FREERTOS_ROOT}/tasks.c"
  "${FREERTOS_ROOT}/queue.c"
  "${FREERTOS_ROOT}/list.c"
  "${FREERTOS_ROOT}/timers.c"
  "${FREERTOS_ROOT}/portable/MemMang/heap_4.c"
)

if(PLATFORM_HOSTED)
  list(APPEND FREERTOS_SOURCE
    "${FREERTOS_ROOT}/portable/ThirdParty/GCC/Posix/port.c"
    "${FREERTOS_ROOT}/portable/ThirdParty/GCC/Posix/utils/wait_for_event.c"
  )
  set(FREERTOS_PORT_INCLUDE "${FREERTOS_ROOT}/portable/ThirdParty/GCC/Posix")

elseif(PLATFORM_STM32F1)
  list(APPEND FREERTOS_SOURCE "${FREERTOS_ROOT}/portable/GCC/ARM_CM3/port.c")
  set(FREERTOS_PORT_INCLUDE "${FREERTOS_ROOT}/portable/GCC/ARM_CM3")

else()
  list(APPEND FREERTOS_SOURCE "${FREERTOS_ROOT}/portable/GCC/ARM_CM4F/port.c")
  set(FREERTOS_PORT_INCLUDE "${FREERTOS_ROOT}/portable/GCC/ARM_CM4F")
endif()

set(FREERTOS_CFG_INCLUDE "${CMAKE_SOURCE_DIR}/include")

add_library(freertos STATIC ${FREERTOS_SOURCE})

target_include_directories(freertos
  PUBLIC
    "${FREERTOS_ROOT}/include"
    ${FREERTOS_PORT_INCLUDE}
    "include/lib_cfg"
    "${CMAKE_BINARY_DIR}/include/lib_cfg"
  PRIVATE
    "libraries/cstone/include"
)

#################### evfs ####################

if(USE_FILESYSTEM)
  if(PLATFORM_HOSTED)
    set(USE_PTHREADS ON  BOOL "Enable Pthreads for EVFS")
  endif()
  add_subdirectory(libraries/evfs)
endif()


#################### heatshrink ####################

set(HEATSHRINK_ROOT  "${CMAKE_SOURCE_DIR}/libraries/heatshrink")

set(HEATSHRINK_SOURCE
    ${HEATSHRINK_ROOT}/heatshrink_encoder.c
    ${HEATSHRINK_ROOT}/heatshrink_decoder.c
)

set_source_files_properties(${HEATSHRINK_SOURCE}
  PROPERTIES
    COMPILE_FLAGS "-Wno-implicit-fallthrough"
)


add_library(heatshrink STATIC ${HEATSHRINK_SOURCE})

target_include_directories(heatshrink
  PUBLIC
    ${HEATSHRINK_ROOT}
)


#################### tinyusb ####################

if(USE_TINYUSB)

file(GLOB_RECURSE TINYUSB_SOURCE
  "libraries/tinyusb/src/*.c"
)

set_source_files_properties(${TINYUSB_SOURCE}
  PROPERTIES
    COMPILE_FLAGS "-Wno-implicit-fallthrough -Wno-missing-prototypes -Wno-redundant-decls"
)


add_library(tinyusb STATIC ${TINYUSB_SOURCE})

target_include_directories(tinyusb
  PUBLIC
    "libraries/tinyusb/src"
    "include/lib_cfg"
  PRIVATE
    "${CMSIS_ROOT}/Device/ST/${DEVICE_FAMILY_UC}xx/Include"
    "${CMSIS_ROOT}/Core/Include"
    "${FREERTOS_ROOT}/include"
    ${FREERTOS_PORT_INCLUDE}
    "libraries/cstone/include"
)

target_compile_definitions(tinyusb
  PUBLIC
    ${DEVICE_FAMILY_UC}
    ${DEVICE_MODEL_UC}xx
)

endif(USE_TINYUSB)

#################### cstone ####################

add_subdirectory(libraries/cstone)



####################               ####################
#################### BUILD TARGETS ####################
####################               ####################

#################### catalyst app ####################

set(APP_SOURCE_COMMON
    src/app_main.c
#    src/app_tasks.c
    src/app_cmds.c
    src/build_info.c
    $<$<BOOL:${USE_FILESYSTEM}>:src/cmds_filesys.c>
    $<$<BOOL:${USE_FILESYSTEM}>:src/log_evfs.c>
)


# STM32 platform specific code
set(APP_SOURCE_BUILD_EMBEDDED
    $<$<BOOL:${PLATFORM_STM32F1}>:src/stm32/stm32f1xx_it.c>
    $<$<BOOL:${PLATFORM_STM32F1}>:src/stm32/app_stm32f1.c>
    $<$<BOOL:${PLATFORM_STM32F4}>:src/stm32/stm32f4xx_it.c>
    $<$<BOOL:${PLATFORM_STM32F4}>:src/stm32/app_stm32f4.c>
    src/newlib_syscalls.c
    $<$<BOOL:${USE_TINYUSB}>:src/usb.c>
    $<$<BOOL:${USE_TINYUSB}>:src/tusb/usb_descriptors.c>
)


if(PLATFORM_STM32)
add_stm32_executable(catalyst
  LINKER_SCRIPT
    "${CMAKE_SOURCE_DIR}/${LINKER_SCRIPT}"
  SOURCE
    ${APP_SOURCE_COMMON}
    ${APP_SOURCE_BUILD_EMBEDDED}
)

target_compile_definitions(catalyst.elf
  PUBLIC
    USE_USB_HS
    USE_USB_HS_IN_FS
)

target_link_libraries(catalyst.elf
  PRIVATE
    $<$<BOOL:${USE_TINYUSB}>:tinyusb>
    $<$<BOOL:${USE_FILESYSTEM}>:evfs>
    freertos
    cstone  # Contains weak symbols, keep at end
    stm32
    heatshrink
)

target_include_directories(catalyst.elf
  PRIVATE
    "include"
    "${CMAKE_BINARY_DIR}/include"
)

endif(PLATFORM_STM32)


#################### catalyst hosted simulator ####################

if(PLATFORM_HOSTED)
add_pc_executable(catalyst
  SOURCE
    ${APP_SOURCE_COMMON}
)

target_link_libraries(catalyst
  PRIVATE
    $<$<BOOL:${USE_FILESYSTEM}>:evfs>
    freertos
    pthread
    cstone
    heatshrink
)

target_include_directories(catalyst
  PRIVATE
    "include"
    "${CMAKE_BINARY_DIR}/include"
)

endif(PLATFORM_HOSTED)

