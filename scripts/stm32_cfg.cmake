#################### Target device settings ####################

set(DEVICE_FAMILY "stm32f4")
set(DEVICE_MODEL  "${DEVICE_FAMILY}29")
set(BOARD_NAME "STM32F429I-Discovery")

string(TOUPPER ${DEVICE_FAMILY} DEVICE_FAMILY_UC)
string(TOUPPER ${DEVICE_MODEL} DEVICE_MODEL_UC)


####################           ####################
#################### LIBRARIES ####################
####################           ####################

#################### Library paths ####################

set(CMSIS_ROOT  "${CMAKE_SOURCE_DIR}/libraries/STM32CubeF4/Drivers/CMSIS")
set(BSP_ROOT    "${CMAKE_SOURCE_DIR}/libraries/STM32CubeF4/Drivers/BSP")
set(HAL_ROOT    "${CMAKE_SOURCE_DIR}/libraries/STM32CubeF4/Drivers/${DEVICE_FAMILY_UC}xx_HAL_Driver")
set(USB_ROOT    "${CMAKE_SOURCE_DIR}/libraries/STM32CubeF4/Middlewares/ST/STM32_USB_Device_Library")

#################### libstm32 (static) ####################

# Use globs to take in all library code from STM32CubeF4
file(GLOB BSP_SOURCE
   "${BSP_ROOT}/${BOARD_NAME}/*.c"
)

file(GLOB_RECURSE BSP_COMP_SOURCE
   "${BSP_ROOT}/Components/*.c"
)


file(GLOB HAL_SOURCE
   "${HAL_ROOT}/Src/*.c"
)

# Glob ingests templates we can't compile
list(FILTER HAL_SOURCE
  EXCLUDE REGEX ".*template.c$"
)


# Combine all lib code from STM32CubeF4
set(STM32_SOURCE
  ${BSP_SOURCE}
  ${BSP_COMP_SOURCE}
  ${HAL_SOURCE}
  "${CMSIS_ROOT}/Lib/GCC/libarm_cortexM4lf_math.a"
  "${CMSIS_ROOT}/Device/ST/${DEVICE_FAMILY_UC}xx/Source/Templates/gcc/startup_${DEVICE_MODEL}xx.s"
  "libraries/STM32CubeF4/Projects/${BOARD_NAME}/Templates/Src/system_${DEVICE_FAMILY}xx.c"
)

# Deactivate warnings caused by -Wextra that prevent compiling stm32 lib
set_source_files_properties(${STM32_SOURCE}
  PROPERTIES
    COMPILE_FLAGS "-Wno-unused-parameter -Wno-shadow -Wno-sign-compare -Wno-missing-field-initializers -Wno-redundant-decls"
)


add_library(stm32 STATIC ${STM32_SOURCE})

target_compile_definitions(stm32
  PUBLIC
    ${DEVICE_FAMILY_UC}
    ${DEVICE_MODEL_UC}xx
#    USE_FULL_ASSERT
    USE_HAL_DRIVER
    USE_FULL_LL_DRIVER
    HSE_VALUE=8000000
    HSI_VALUE=16000000
)

target_include_directories(stm32
  PUBLIC
    "${CMSIS_ROOT}/Include"
    "${CMSIS_ROOT}/Device/ST/${DEVICE_FAMILY_UC}xx/Include"
    "${CMSIS_ROOT}/Core/Include"
    "${HAL_ROOT}/Inc"
    "${BSP_ROOT}/${BOARD_NAME}"
    "include/stm32"
)


#################### libstm32usb ####################

file(GLOB USB_CORE_SOURCE
  ${USB_ROOT}/Core/Src/*.c
)

list(FILTER USB_CORE_SOURCE
  EXCLUDE REGEX ".*template.c$"
)

file(GLOB USB_CDC_SOURCE
  ${USB_ROOT}/Class/CDC/Src/*.c
)

list(FILTER USB_CDC_SOURCE
  EXCLUDE REGEX ".*template.c$"
)


set(USB_SOURCE
  ${USB_CORE_SOURCE}
  ${USB_CDC_SOURCE}
)


add_library(stm32usb STATIC ${USB_SOURCE})

target_compile_definitions(stm32usb
  PUBLIC
    ${DEVICE_FAMILY_UC}
    ${DEVICE_MODEL_UC}xx
#    USE_HAL_DRIVER
#    USE_FULL_LL_DRIVER
#    HSE_VALUE=8000000
#    HSI_VALUE=16000000
)

target_include_directories(stm32usb
  PUBLIC
    ${USB_ROOT}/Core/Inc
    ${USB_ROOT}/Class/CDC/Inc
    ${USB_ROOT}/Class/HID/Inc
    "${CMSIS_ROOT}/Include"
    "${CMSIS_ROOT}/Device/ST/${DEVICE_FAMILY_UC}xx/Include"
    "${CMSIS_ROOT}/Core/Include"
    "${HAL_ROOT}/Inc"
    "include/st_micro"
)

