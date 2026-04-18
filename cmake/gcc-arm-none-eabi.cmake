# See COPYING.txt for license details.

set(CMAKE_SYSTEM_NAME               Generic)
set(CMAKE_SYSTEM_PROCESSOR          arm)

set(CMAKE_C_COMPILER_FORCED TRUE)
set(CMAKE_CXX_COMPILER_FORCED TRUE)
set(CMAKE_C_COMPILER_ID GNU)
set(CMAKE_CXX_COMPILER_ID GNU)

# Some default GCC settings
# Toolchain can come from PATH, CC/CXX, or ARM_GCC_BIN.
set(TOOLCHAIN_PREFIX                arm-none-eabi-)

set(_arm_toolchain_hints)
if(DEFINED ENV{ARM_GCC_BIN} AND NOT "$ENV{ARM_GCC_BIN}" STREQUAL "")
    file(TO_CMAKE_PATH "$ENV{ARM_GCC_BIN}" _arm_gcc_bin)
    list(APPEND _arm_toolchain_hints "${_arm_gcc_bin}" "${_arm_gcc_bin}/bin")
endif()

if(NOT CMAKE_C_COMPILER AND DEFINED ENV{CC} AND NOT "$ENV{CC}" STREQUAL "")
    set(CMAKE_C_COMPILER "$ENV{CC}")
endif()

if(NOT CMAKE_CXX_COMPILER AND DEFINED ENV{CXX} AND NOT "$ENV{CXX}" STREQUAL "")
    set(CMAKE_CXX_COMPILER "$ENV{CXX}")
endif()

function(resolve_arm_tool out_var fallback_name current_value)
    if(current_value)
        if(IS_ABSOLUTE "${current_value}" AND EXISTS "${current_value}")
            set(${out_var} "${current_value}" PARENT_SCOPE)
            return()
        endif()
        find_program(_resolved_tool NAMES "${current_value}" HINTS ${_arm_toolchain_hints})
    else()
        find_program(_resolved_tool NAMES "${fallback_name}" HINTS ${_arm_toolchain_hints})
    endif()

    if(_resolved_tool)
        set(${out_var} "${_resolved_tool}" PARENT_SCOPE)
    else()
        set(${out_var} "" PARENT_SCOPE)
    endif()
endfunction()

resolve_arm_tool(_resolved_c_compiler   "${TOOLCHAIN_PREFIX}gcc"     "${CMAKE_C_COMPILER}")
resolve_arm_tool(_resolved_cxx_compiler "${TOOLCHAIN_PREFIX}g++"     "${CMAKE_CXX_COMPILER}")
resolve_arm_tool(_resolved_objcopy      "${TOOLCHAIN_PREFIX}objcopy" "${CMAKE_OBJCOPY}")
resolve_arm_tool(_resolved_objdump      "${TOOLCHAIN_PREFIX}objdump" "${CMAKE_OBJDUMP}")
resolve_arm_tool(_resolved_size         "${TOOLCHAIN_PREFIX}size"    "${CMAKE_SIZE}")

if(NOT _resolved_c_compiler OR NOT _resolved_cxx_compiler)
    message(FATAL_ERROR
        "ARM GCC toolchain not found. Install arm-none-eabi-gcc and add it to PATH, "
        "or set ARM_GCC_BIN to the directory containing arm-none-eabi-gcc/arm-none-eabi-g++.")
endif()

if(NOT _resolved_objcopy OR NOT _resolved_objdump OR NOT _resolved_size)
    message(FATAL_ERROR
        "ARM binutils were not found alongside the ARM GCC toolchain. "
        "Check that arm-none-eabi-objcopy, arm-none-eabi-objdump, and arm-none-eabi-size "
        "are installed and reachable via PATH or ARM_GCC_BIN.")
endif()

set(CMAKE_C_COMPILER                "${_resolved_c_compiler}")
set(CMAKE_ASM_COMPILER              "${CMAKE_C_COMPILER}")
set(CMAKE_CXX_COMPILER              "${_resolved_cxx_compiler}")
set(CMAKE_LINKER                    "${CMAKE_CXX_COMPILER}")
set(CMAKE_OBJCOPY                   "${_resolved_objcopy}")
set(CMAKE_OBJDUMP                   "${_resolved_objdump}")
set(CMAKE_SIZE                      "${_resolved_size}")

set(CMAKE_EXECUTABLE_SUFFIX_ASM     ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_C       ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_CXX     ".elf")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# MCU specific flags
set(TARGET_FLAGS "-mcpu=cortex-m33 -mfpu=fpv5-sp-d16 -mfloat-abi=hard ")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${TARGET_FLAGS}")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wpedantic -fdata-sections -ffunction-sections -Wno-overlength-strings -Wno-stringop-overflow")
if(CMAKE_BUILD_TYPE MATCHES Debug)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O0 -g3")
endif()
if(CMAKE_BUILD_TYPE MATCHES Release)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Os -g0")
endif()

set(CMAKE_ASM_FLAGS "${CMAKE_C_FLAGS} -x assembler-with-cpp -MMD -MP")
set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -fno-rtti -fno-exceptions -fno-threadsafe-statics")

set(CMAKE_C_LINK_FLAGS "${TARGET_FLAGS}")
set(CMAKE_C_LINK_FLAGS "${CMAKE_C_LINK_FLAGS} -T \"${CMAKE_SOURCE_DIR}/STM32H573VITX_FLASH.ld\"")
#set(CMAKE_C_LINK_FLAGS "${CMAKE_C_LINK_FLAGS} --specs=nano.specs")
set(CMAKE_C_LINK_FLAGS "${CMAKE_C_LINK_FLAGS} --specs=nosys.specs")
#set(CMAKE_C_LINK_FLAGS "${CMAKE_C_LINK_FLAGS} -Wl,-Map=${CMAKE_PROJECT_NAME}.map -Wl,--gc-sections")
set(CMAKE_C_LINK_FLAGS "${CMAKE_C_LINK_FLAGS} -Wl,-Map=${CMAKE_PROJECT_NAME}.map -Wl,--gc-sections -static --specs=nano.specs")
set(CMAKE_C_LINK_FLAGS "${CMAKE_C_LINK_FLAGS} -Wl,--start-group -lc -lm -Wl,--end-group")
set(CMAKE_C_LINK_FLAGS "${CMAKE_C_LINK_FLAGS} -Wl,--print-memory-usage")

set(CMAKE_CXX_LINK_FLAGS "${CMAKE_C_LINK_FLAGS} -Wl,--start-group -lstdc++ -lsupc++ -Wl,--end-group")
