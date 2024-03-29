cmake_minimum_required(VERSION 3.20)


#################### Build config settings ####################

set(BUILD_PLATFORM "stm32f1"    CACHE STRING "Target platform")
set(BUILD_BOARD    "none"       CACHE STRING "Target board")
set(LINKER_SCRIPT  ""           CACHE STRING "Linker script for board")
option(USE_TINYUSB        "Enable Tinyusb driver"           OFF)
option(USE_NEWLIB_NANO    "Enable Newlib nano C library"    OFF)
option(USE_MINIMAL_TASKS  "Reduce core task set to minimum" OFF)
option(USE_FILESYSTEM     "Enable EVFS filesystem"          OFF)
option(USE_AUDIO          "Enable Audio driver"             OFF)
option(USE_LVGL           "Enable LVGL GUI"                 OFF)

string(TIMESTAMP BUILD_TIME "%Y-%m-%dT%H:%M:%S")

set(PLATFORM_HOSTED   OFF)
set(PLATFORM_STM32    OFF)
set(PLATFORM_STM32F1  OFF)
set(PLATFORM_STM32F4  OFF)

set(DEVICE_STM32F401  OFF)
set(DEVICE_STM32F429  OFF)

set(BOARD_MAPLE_MINI        OFF)
set(BOARD_STM32F429I_DISC1  OFF)
set(BOARD_STM32F429N_EVAL   OFF)
set(BOARD_STM32F401_BLACK_PILL  OFF)
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
  set(DEVICE_STM32F429  ON)
elseif(BUILD_BOARD MATCHES "stm32f429n_eval")
  set(BOARD_STM32F429N_EVAL ON)
  set(DEVICE_STM32F429  ON)
elseif(BUILD_BOARD MATCHES "stm32f401_black_pill")
  set(BOARD_STM32F401_BLACK_PILL ON)
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

execute_process(
  COMMAND
    git rev-parse --short=8 HEAD
  OUTPUT_VARIABLE APP_GIT_SHA
)

configure_file (
  include/lib_cfg/build_config.h.in
  include/lib_cfg/build_config.h
)

configure_file (
  include/lib_cfg/build_time.h.in
  include/lib_cfg/build_time.h
)



#################### Target config ####################
if(PLATFORM_STM32F1)
  include(scripts/stm32f1_cfg.cmake)
elseif(BOARD_STM32F429I_DISC1)
  include(scripts/stm32f4_cfg.cmake)
elseif(BOARD_STM32F429N_EVAL)
  include(scripts/stm32f429N_cfg.cmake)
elseif(BOARD_STM32F401_BLACK_PILL)
  include(scripts/stm32f401_cfg.cmake)
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
    "build/include/lib_cfg"
)

target_compile_definitions(tinyusb
  PUBLIC
    ${DEVICE_FAMILY_UC}
    ${DEVICE_MODEL_UC}
)

endif(USE_TINYUSB)

#################### cstone ####################

add_subdirectory(libraries/cstone)


#################### SDL ####################

if(PLATFORM_HOSTED AND USE_AUDIO)
  find_package(SDL2 REQUIRED SDL2)
endif()


#################### liblvgl (static) ####################

file(GLOB_RECURSE LVGL_SOURCE
  "libraries/lvgl/src/*.c"
)

if(PLATFORM_HOSTED)
  file(GLOB_RECURSE LV_DRIVER_SOURCE
    "libraries/lv_drivers/*.c"
  )
  list(APPEND LVGL_SOURCE ${LV_DRIVER_SOURCE})

  find_package(SDL2 REQUIRED SDL2)
endif()

# Deactivate warnings caused by -Wextra that prevent compiling lib
set_source_files_properties(${LVGL_SOURCE}
  PROPERTIES
    COMPILE_FLAGS "-Wno-undef -Wno-format -Wno-redundant-decls -Wno-unused-variable"
)

set_source_files_properties("libraries/lvgl/src/extra/libs/qrcode/qrcodegen.c"
  PROPERTIES
    COMPILE_FLAGS "-Wno-type-limits -Wno-sign-compare"
)


add_library(lvgl STATIC ${LVGL_SOURCE})

# Have LVGL look for lv_conf.h in our base include dir
target_compile_definitions(lvgl
  PUBLIC
    STM32F4
    LV_CONF_INCLUDE_SIMPLE
#    LV_DEMO_CONF_INCLUDE_SIMPLE
)

target_include_directories(lvgl
PRIVATE
  "include/lib_cfg"
  "${CMAKE_BINARY_DIR}/include/lib_cfg"
PUBLIC
  "libraries"
  "${CMSIS_ROOT}/Include"
  "${CMSIS_ROOT}/Device/ST/${DEVICE_FAMILY_UC}xx/Include"
  "${CMSIS_ROOT}/Core/Include"
  $<$<BOOL:${PLATFORM_HOSTED}>:${SDL2_INCLUDE_DIRS}>
)

if(PLATFORM_HOSTED)
  target_link_libraries(lvgl
    PRIVATE
      SDL2
  )
endif()



####################               ####################
#################### BUILD TARGETS ####################
####################               ####################

#################### catalyst app ####################

m4_template("libraries/cstone/template/cstone/iqueue.h.m4"  "int16_t")
m4_template("libraries/cstone/template/cstone/iqueue.c.m4"  "int16_t")


set(APP_SOURCE_COMMON
    src/app_main.c
    src/app_tasks.c
    src/app_cmds.c
    src/build_info.c
    $<$<BOOL:${USE_AUDIO}>:src/sample_device.c>
    $<$<BOOL:${USE_AUDIO}>:src/audio_synth.c>
    $<$<BOOL:${USE_FILESYSTEM}>:src/cmds_filesys.c>
    $<$<BOOL:${USE_FILESYSTEM}>:src/log_evfs.c>
    ${CMAKE_BINARY_DIR}/template/cstone/iqueue_int16_t.h
    ${CMAKE_BINARY_DIR}/template/cstone/iqueue_int16_t.c
)

# GUI implementation when LVGL is enabled
set(APP_SOURCE_GUI
  src/disco_ui.cpp # FIXME
  src/ui_panel.c
  src/app_ui.c
  src/ui_units.c
  ui/images/cursor_v.c
  ui/images/cal_target.c
  ui/images/gear_pos.c
  ui/images/battery_icon.c
  ui/images/temp_icon.c
  ui/images/checked_icon.c
  ui/images/fuel_icon.c
  ui/images/speedo_icon.c
  ui/images/speedo_avg_icon.c
  ui/images/triumph_logo.c
  ui/images/sprint_st_40.c
  ui/images/sprint_st_side_stand_40.c
  ui/fonts/rubik_med_italic_24.c
  ui/fonts/rubik_med_italic_48.c
)

set(APP_SOURCE_BUILD_HOSTED
  $<$<BOOL:${USE_AUDIO}>:src/sample_device_sdl.c>
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
    $<$<BOOL:${USE_AUDIO}>:src/sample_device_i2s.c>
    $<$<AND:$<BOOL:${USE_AUDIO}>,$<STREQUAL:${DEVICE_MODEL},stm32f429>>:src/sample_device_dac.c>
    src/debounce.c
)


if(PLATFORM_STM32)
add_stm32_executable(catalyst
  LINKER_SCRIPT
    "${CMAKE_SOURCE_DIR}/${LINKER_SCRIPT}"
  SOURCE
    ${APP_SOURCE_COMMON}
    ${APP_SOURCE_BUILD_EMBEDDED}
    $<$<BOOL:${USE_LVGL}>:${APP_SOURCE_GUI}>
)

# Force rebuild of build_info.c with updated APP_BUILD_TIME macro
add_custom_command(
  TARGET catalyst.elf
  PRE_BUILD
  COMMAND
    rm -f ${CMAKE_BINARY_DIR}/include/lib_cfg/build_time.h
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
    $<$<BOOL:${USE_LVGL}>:lvgl>
    freertos
    cstone  # Contains weak symbols, keep at end
    stm32
    heatshrink
)

target_include_directories(catalyst.elf
  PRIVATE
    "include"
    "${CMAKE_BINARY_DIR}/include"
    "${CMAKE_BINARY_DIR}/template"
)

endif(PLATFORM_STM32)


#################### catalyst hosted simulator ####################

if(PLATFORM_HOSTED)
add_pc_executable(catalyst
  SOURCE
    ${APP_SOURCE_COMMON}
    ${APP_SOURCE_BUILD_HOSTED}
    $<$<BOOL:${USE_LVGL}>:${APP_SOURCE_GUI}>
)

target_link_libraries(catalyst
  PRIVATE
    freertos
    pthread
    cstone
    heatshrink
    $<$<BOOL:${USE_FILESYSTEM}>:evfs>
    $<$<BOOL:${USE_AUDIO}>:SDL2>
    $<$<BOOL:${USE_LVGL}>:lvgl>
)

target_include_directories(catalyst
  PRIVATE
    "include"
    "${CMAKE_BINARY_DIR}/include"
    "${CMAKE_BINARY_DIR}/template"
    $<$<BOOL:${USE_AUDIO}>:${SDL2_INCLUDE_DIRS}>
)

endif(PLATFORM_HOSTED)

