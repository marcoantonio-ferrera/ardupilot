cmake_minimum_required(VERSION 3.20)

if((NOT DEFINED CROSS_COMPILE_TOOLCHAIN_PATH OR CROSS_COMPILE_TOOLCHAIN_PATH STREQUAL "") AND DEFINED CROSS_COMPILE AND NOT CROSS_COMPILE STREQUAL "")
    cmake_path(GET CROSS_COMPILE PARENT_PATH CROSS_COMPILE_BIN_DIR)
    cmake_path(SET CROSS_COMPILE_TOOLCHAIN_PATH NORMALIZE "${CROSS_COMPILE_BIN_DIR}/..")
    set(CROSS_COMPILE_TOOLCHAIN_PATH "${CROSS_COMPILE_TOOLCHAIN_PATH}" CACHE PATH "Cross Compile toolchain directory")
elseif((NOT DEFINED CROSS_COMPILE_TOOLCHAIN_PATH OR CROSS_COMPILE_TOOLCHAIN_PATH STREQUAL "") AND DEFINED ENV{CROSS_COMPILE} AND NOT "$ENV{CROSS_COMPILE}" STREQUAL "")
    set(CROSS_COMPILE "$ENV{CROSS_COMPILE}")
    cmake_path(GET CROSS_COMPILE PARENT_PATH CROSS_COMPILE_BIN_DIR)
    cmake_path(SET CROSS_COMPILE_TOOLCHAIN_PATH NORMALIZE "${CROSS_COMPILE_BIN_DIR}/..")
    set(CROSS_COMPILE_TOOLCHAIN_PATH "${CROSS_COMPILE_TOOLCHAIN_PATH}" CACHE PATH "Cross Compile toolchain directory")
endif()

# DTS roots — must be set before find_package(Zephyr).
if(DEFINED ZEPHYR_BOARD_HAL_DIR AND NOT ZEPHYR_BOARD_HAL_DIR STREQUAL "")
    list(APPEND DTS_ROOT "${ZEPHYR_BOARD_HAL_DIR}")
endif()
set(AP_HAL_ZEPHYR_DIR ${CMAKE_CURRENT_LIST_DIR}/..)
list(APPEND DTS_ROOT "${AP_HAL_ZEPHYR_DIR}")

find_package(Zephyr REQUIRED HINTS ${ZEPHYR_BASE} $ENV{ZEPHYR_BASE})
project(ardupilot-zephyr)

set(ARDUPILOT_DIR ${CMAKE_CURRENT_LIST_DIR}/../../..)

# per-board CMakeLists registers out-of-tree drivers and AP_BOARD_ABI_CFLAGS
if(DEFINED ZEPHYR_BOARD_HAL_DIR AND NOT ZEPHYR_BOARD_HAL_DIR STREQUAL ""
   AND EXISTS "${ZEPHYR_BOARD_HAL_DIR}/CMakeLists.txt")
    add_subdirectory(${ZEPHYR_BOARD_HAL_DIR} board_hal_build)
endif()

# POSIX stub headers newlib/picolibc don't ship
zephyr_include_directories(${ARDUPILOT_DIR}/libraries/AP_HAL_Zephyr/include)

# showinc / showdefs: export Zephyr include dirs and compile defs for WAF
add_custom_target(showinc
    COMMAND echo
        "$<TARGET_PROPERTY:zephyr_interface,INTERFACE_INCLUDE_DIRECTORIES>"
        > ${CMAKE_BINARY_DIR}/includes.list
    VERBATIM
    BYPRODUCTS ${CMAKE_BINARY_DIR}/includes.list
    COMMAND_EXPAND_LISTS
)
add_dependencies(showinc zephyr_generated_headers)

add_custom_target(showdefs
    COMMAND echo
        "$<TARGET_PROPERTY:zephyr_interface,INTERFACE_COMPILE_DEFINITIONS>"
        > ${CMAKE_BINARY_DIR}/compile_defs.list
    VERBATIM
    BYPRODUCTS ${CMAKE_BINARY_DIR}/compile_defs.list
    COMMAND_EXPAND_LISTS
)
add_dependencies(showdefs zephyr_generated_headers)

# Write AP_BOARD_ABI_CFLAGS to a GCC response file; WAF passes @<rsp>
# on every compile so kernel and ArduPilot objects link cleanly.
if(DEFINED AP_BOARD_ABI_CFLAGS)
    string(REPLACE ";" "\n" _ap_rsp "${AP_BOARD_ABI_CFLAGS}")
else()
    set(_ap_rsp "")
    message(WARNING "AP_BOARD_ABI_CFLAGS not set by ${ZEPHYR_BOARD_HAL_DIR}/CMakeLists.txt. "
                    "WAF-compiled ArduPilot objects will use only Tools/ardupilotwaf/boards.py "
                    "flags and may fail to link against the Zephyr kernel if ABI flags differ.")
endif()
file(WRITE ${CMAKE_BINARY_DIR}/ap_board_abi.rsp "${_ap_rsp}\n")

# WAF builds ArduPilot .a libs; we link them with the Zephyr kernel here.
# ARDUPILOT_LIB/ARDUPILOT_BIN come from zephyr.py's pre_build().
if(NOT DEFINED ARDUPILOT_LIB OR "${ARDUPILOT_LIB}" STREQUAL "")
    message(FATAL_ERROR
        "ARDUPILOT_LIB is not set. Run this CMake project via WAF "
        "(./waf arducopter --board <board>), not cmake directly.")
endif()

target_sources(app PRIVATE main.cpp)

if(NOT DEFINED ARDUPILOT_CMD)
    set(ARDUPILOT_CMD "copter")
endif()

message(STATUS "ARDUPILOT_CMD=${ARDUPILOT_CMD}")
message(STATUS "ARDUPILOT_LIB=${ARDUPILOT_LIB}")
message(STATUS "ARDUPILOT_BIN=${ARDUPILOT_BIN}")

# --start-group/--end-group so libkernel.a and ArduPilot archives can resolve
# each other's symbols across multiple passes.
set(_KERNEL_LIB "${CMAKE_CURRENT_BINARY_DIR}/zephyr/kernel/libkernel.a")
if(${ARDUPILOT_CMD} STREQUAL "arducopter" OR ${ARDUPILOT_CMD} STREQUAL "copter")
    target_link_libraries(app PRIVATE
        -Wl,--start-group
        "${ARDUPILOT_BIN}/libarducopter.a"
        "${ARDUPILOT_LIB}/libArduCopter_libs.a"
        "${_KERNEL_LIB}"
        -Wl,--end-group)
elseif(${ARDUPILOT_CMD} STREQUAL "arduplane" OR ${ARDUPILOT_CMD} STREQUAL "plane")
    target_link_libraries(app PRIVATE
        -Wl,--start-group
        "${ARDUPILOT_BIN}/libarduplane.a"
        "${ARDUPILOT_LIB}/libArduPlane_libs.a"
        "${_KERNEL_LIB}"
        -Wl,--end-group)
elseif(${ARDUPILOT_CMD} STREQUAL "ardurover" OR ${ARDUPILOT_CMD} STREQUAL "rover")
    target_link_libraries(app PRIVATE
        -Wl,--start-group
        "${ARDUPILOT_BIN}/libardurover.a"
        "${ARDUPILOT_LIB}/libRover_libs.a"
        "${_KERNEL_LIB}"
        -Wl,--end-group)
elseif(${ARDUPILOT_CMD} STREQUAL "ardusub" OR ${ARDUPILOT_CMD} STREQUAL "sub")
    target_link_libraries(app PRIVATE
        -Wl,--start-group
        "${ARDUPILOT_BIN}/libardusub.a"
        "${ARDUPILOT_LIB}/libArduSub_libs.a"
        "${_KERNEL_LIB}"
        -Wl,--end-group)
else()
    target_link_libraries(app PRIVATE
        -Wl,--start-group
        "${ARDUPILOT_LIB}/libap.a"
        "${_KERNEL_LIB}"
        -Wl,--end-group)
endif()

target_include_directories(app PRIVATE
    ${ARDUPILOT_DIR}
    ${ARDUPILOT_DIR}/libraries
    ${ARDUPILOT_DIR}/libraries/AP_Common/missing
)

target_compile_definitions(app PRIVATE
    CONFIG_HAL_BOARD=HAL_BOARD_ZEPHYR
    APM_BUILD_DIRECTORY=APM_BUILD_UNKNOWN
    __STDC_FORMAT_MACROS=1
    __AP_LINE__=__LINE__
)

target_compile_features(app PRIVATE cxx_std_17)
