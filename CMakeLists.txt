cmake_minimum_required(VERSION 3.20)

# Default to the Arm cross toolchain so a plain `cmake -B build` works.
if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/cmake/arm-none-eabi.cmake")
endif()

project(mr2_dashboard C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(TARGET_NAME mr2-dashboard)
set(LINKER_SCRIPT "${CMAKE_CURRENT_SOURCE_DIR}/linker/STM32G0B1CBTX_FLASH.ld")

set(MCU_FLAGS -mcpu=cortex-m0plus -mthumb -mfloat-abi=soft)

add_executable(${TARGET_NAME}
    src/main.c
    src/board/board.c
    src/board/clock.c
    src/board/gpio.c
    src/board/systick.c
    src/board/spi.c
    src/board/debug_uart.c
    src/board/syscalls.c
    src/eve/eve.c
    src/eve/eve_hal_stm32.c
    src/eve/eve_panels.c
    src/app/dashboard.c
    src/app/demo_data.c
    src/app/fixmath.c
    src/app/logo.c
    third_party/cmsis_device_g0/Source/system_stm32g0xx.c
    third_party/cmsis_device_g0/Source/gcc/startup_stm32g0b1xx.s
)

set_target_properties(${TARGET_NAME} PROPERTIES SUFFIX ".elf")

target_include_directories(${TARGET_NAME} PRIVATE
    src
    src/board
    src/eve
    src/app
    third_party/cmsis_core/Include
    third_party/cmsis_device_g0/Include
)

target_compile_definitions(${TARGET_NAME} PRIVATE STM32G0B1xx)

target_compile_options(${TARGET_NAME} PRIVATE
    ${MCU_FLAGS}
    -ffunction-sections
    -fdata-sections
    -fno-common
    $<$<COMPILE_LANGUAGE:C>:-Wall -Wextra -Wshadow -Wdouble-promotion -Wno-unused-parameter>
    $<$<CONFIG:Debug>:-Og -g3>
    $<$<CONFIG:Release>:-Os -g>
    $<$<CONFIG:MinSizeRel>:-Os>
    $<$<CONFIG:RelWithDebInfo>:-O2 -g>
)

# The vendored ST/Arm sources are not ours to fix; keep their warnings quiet.
set_source_files_properties(
    third_party/cmsis_device_g0/Source/system_stm32g0xx.c
    PROPERTIES COMPILE_OPTIONS "-w"
)

target_link_options(${TARGET_NAME} PRIVATE
    ${MCU_FLAGS}
    -T${LINKER_SCRIPT}
    --specs=nano.specs
    -Wl,--gc-sections
    -Wl,-Map=$<TARGET_FILE_DIR:${TARGET_NAME}>/${TARGET_NAME}.map,--cref
    -Wl,--print-memory-usage
)

include(CheckLinkerFlag)
check_linker_flag(C "-Wl,--no-warn-rwx-segments" HAVE_NO_WARN_RWX_SEGMENTS)
if(HAVE_NO_WARN_RWX_SEGMENTS)
    target_link_options(${TARGET_NAME} PRIVATE -Wl,--no-warn-rwx-segments)
endif()

set_target_properties(${TARGET_NAME} PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})

add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex   $<TARGET_FILE:${TARGET_NAME}> ${TARGET_NAME}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${TARGET_NAME}> ${TARGET_NAME}.bin
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET_NAME}>
    WORKING_DIRECTORY $<TARGET_FILE_DIR:${TARGET_NAME}>
    COMMENT "Generating ${TARGET_NAME}.hex / .bin"
)

# `cmake --build build --target flash` programs the board over SWD via OpenOCD.
find_program(OPENOCD openocd)
if(OPENOCD)
    add_custom_target(flash
        COMMAND ${OPENOCD} -f interface/stlink.cfg -f target/stm32g0x.cfg
                -c "program $<TARGET_FILE:${TARGET_NAME}> verify reset exit"
        DEPENDS ${TARGET_NAME}
        USES_TERMINAL
        COMMENT "Flashing ${TARGET_NAME} with OpenOCD (ST-LINK)"
    )
endif()
