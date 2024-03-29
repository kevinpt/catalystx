# Common build script for STM32

#################### Global build options ####################

set(CMAKE_C_STANDARD            11)
set(CMAKE_C_STANDARD_REQUIRED   ON)

set(CMAKE_CXX_STANDARD          17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)


set(CMAKE_C_FLAGS_DEBUG             "-O0 -g -gdwarf-3 -gstrict-dwarf")
set(CMAKE_CXX_FLAGS_DEBUG           "-O0 -g -gdwarf-3 -gstrict-dwarf")

set(CMAKE_C_FLAGS_RELEASE           "-Os -DNDEBUG")
set(CMAKE_CXX_FLAGS_RELEASE         "-Os -DNDEBUG")

set(CMAKE_C_FLAGS_RELWITHDEBINFO    "-Os -DNDEBUG -g -gdwarf-3 -gstrict-dwarf")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO  "-Os -DNDEBUG -g -gdwarf-3 -gstrict-dwarf")


# No change to default flags for Release and MinSizeRel


add_compile_options(
  -Dnewlib
  $<$<BOOL:${PLATFORM_STM32F4}>:-mcpu=cortex-m4>
  $<$<BOOL:${PLATFORM_STM32F1}>:-mcpu=cortex-m3>
  -mthumb
  -mlittle-endian
  $<$<BOOL:${PLATFORM_STM32F4}>:-mfloat-abi=hard>
  $<$<BOOL:${PLATFORM_STM32F4}>:-mfpu=fpv4-sp-d16>
  $<$<BOOL:${PLATFORM_STM32F1}>:-mfloat-abi=soft>
  -fno-exceptions
  $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
  -funsigned-char
  -funsigned-bitfields
  -fno-common
  -fno-math-errno
  -fdata-sections
  -ffunction-sections
  $<$<COMPILE_LANGUAGE:CXX>:-fno-use-cxa-atexit>
  -Wall
  -Wextra
  -Wno-unused-parameter
  -Wno-missing-field-initializers
  -Wshadow
  -Werror
#  -Wfatal-errors
#  -Wpedantic
#  -pedantic-errors
  -Wredundant-decls
  $<$<COMPILE_LANGUAGE:C>:-Wmissing-prototypes>
  -Wdouble-promotion
  -Wundef
)


#################### Functions ####################

## Wrapper to add exec along with utility targets
function(add_stm32_executable EXEC_NAME)
  cmake_parse_arguments(
    PARSE_ARGV 1 "ARG"
    "" "LINKER_SCRIPT" "SOURCE"
  )

#  message(STATUS "EXEC_NAME ${EXEC_NAME}")
#  message(STATUS "LINKER_SCRIPT ${ARG_LINKER_SCRIPT}")
#  message(STATUS "SOURCE ${ARG_SOURCE}")

  if(NOT ARG_LINKER_SCRIPT)
    message(FATAL_ERROR "Target '${EXEC_NAME}' is missing linker script")
  endif()

  if(NOT ARG_SOURCE)
    message(FATAL_ERROR "Target '${EXEC_NAME}' is missing source list")
  endif()


  # Add all project source
  add_executable(${EXEC_NAME}.elf EXCLUDE_FROM_ALL
    ${ARG_SOURCE}
  )

  target_link_options(${EXEC_NAME}.elf
    PRIVATE
      $<$<BOOL:${PLATFORM_STM32F4}>:-mcpu=cortex-m4>
      $<$<BOOL:${PLATFORM_STM32F1}>:-mcpu=cortex-m3>
      -mthumb
      -mlittle-endian
      $<$<BOOL:${PLATFORM_STM32F4}>:-mfloat-abi=hard>
      $<$<BOOL:${PLATFORM_STM32F4}>:-mfpu=fpv4-sp-d16>
      $<$<BOOL:${PLATFORM_STM32F1}>:-mfloat-abi=soft>
      -ffreestanding
      -fno-use-cxa-atexit
      $<$<NOT:$<BOOL:${USE_NEWLIB_NANO}>>:--specs=nosys.specs>
      $<$<BOOL:${USE_NEWLIB_NANO}>:--specs=nano.specs>
      "LINKER:--gc-sections"
#      "LINKER:--print-gc-sections"
      "LINKER:-Map,${EXEC_NAME}.map"
      "LINKER:--cref"
      "LINKER:--print-memory-usage"
      "LINKER:-T${ARG_LINKER_SCRIPT}"
  )

# Generate detailed size report:
#     arm-none-eabi-size -A app.elf
# Generate list of largest symbols:
#     arm-none-eabi-nm --print-size --size-sort --radix=d app.elf | tail -30

  set_target_properties(${EXEC_NAME}.elf
    PROPERTIES
      LINK_DEPENDS ${ARG_LINKER_SCRIPT}
  )


  #################### Generate images ####################
  add_custom_command( OUTPUT ${EXEC_NAME}.hex
    COMMAND
      ${ARM_OBJCOPY} -O ihex ${EXEC_NAME}.elf ${EXEC_NAME}.hex
    COMMAND
      ${ARM_SIZE} -B ${EXEC_NAME}.elf
    DEPENDS ${EXEC_NAME}.elf
  )

  add_custom_command( OUTPUT ${EXEC_NAME}.bin
    COMMAND
      ${ARM_OBJCOPY} -O binary ${EXEC_NAME}.elf ${EXEC_NAME}.bin
    COMMAND
      elf_patch -i ${EXEC_NAME}.elf
    DEPENDS ${EXEC_NAME}.elf
  )


  add_custom_command( OUTPUT ${EXEC_NAME}.lst
    COMMAND
      ${ARM_OBJDUMP} -h -S ${EXEC_NAME}.elf > ${EXEC_NAME}.lst
    DEPENDS ${EXEC_NAME}.elf
  )


  #################### Build target ####################
  add_custom_target(${EXEC_NAME}
    ALL
      DEPENDS ${EXEC_NAME}.bin ${EXEC_NAME}.hex ${EXEC_NAME}.lst
  )

  add_custom_target( upload_${EXEC_NAME}
# FIXME: Make this configurable for different boards
    openocd -f /usr/local/share/openocd/scripts/board/stm32f429disc1.cfg -c "program ${EXEC_NAME}.elf verify reset exit"
    DEPENDS ${EXEC_NAME}.elf
  )

  # Alternate upload with st-flash
  # Always does mass erase. Storage data is not preserved
  add_custom_target( upload2_${EXEC_NAME}
    st-flash write ${EXEC_NAME}.bin 0x8000000
    DEPENDS ${EXEC_NAME}.bin
  )

  #################### Utility ####################
  set_target_properties(${EXEC_NAME}
    PROPERTIES
      OUTPUT_NAME ${EXEC_NAME}.elf
      ADDITIONAL_CLEAN_FILES "${EXEC_NAME}.map"
  )

endfunction(add_stm32_executable)

