# CMake toolchain file for the GNU Arm Embedded toolchain (arm-none-eabi-gcc).
#
# The toolchain's bin directory must be on PATH, or set ARM_TOOLCHAIN_DIR to
# it (e.g. -DARM_TOOLCHAIN_DIR=/opt/arm-gnu-toolchain/bin).

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(ARM_TOOLCHAIN_DIR "" CACHE PATH "Directory containing arm-none-eabi-gcc (empty = use PATH)")
if(ARM_TOOLCHAIN_DIR)
    set(_prefix "${ARM_TOOLCHAIN_DIR}/arm-none-eabi-")
else()
    set(_prefix "arm-none-eabi-")
endif()

set(CMAKE_C_COMPILER   "${_prefix}gcc")
set(CMAKE_ASM_COMPILER "${_prefix}gcc")
set(CMAKE_OBJCOPY      "${_prefix}objcopy" CACHE FILEPATH "")
set(CMAKE_SIZE         "${_prefix}size"    CACHE FILEPATH "")

# Cross-compiling: don't try to link a test executable for the host.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
