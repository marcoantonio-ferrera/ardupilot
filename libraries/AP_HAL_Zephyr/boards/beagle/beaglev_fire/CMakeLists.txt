# BeagleV-Fire (MPFS U54) board sources, pulled in from app/CMakeLists.txt.

# must match target_riscv.cmake's -march/-mabi for this SoC, or linking
# ArduPilot's objects against the Zephyr kernel fails
set(AP_BOARD_ABI_CFLAGS
    -march=rv64imafdc_zicsr_zifencei
    -mabi=lp64d
    -mcmodel=medany
    PARENT_SCOPE)

zephyr_library_sources(${CMAKE_CURRENT_LIST_DIR}/mss_ahb_div.c)

set(_HAL_DRIVERS ${CMAKE_CURRENT_LIST_DIR}/../../../drivers)

# forked mpfs-mss SPI for GPIO chip-select support (SPI1 is shared with an ADC)
if(CONFIG_DT_HAS_ARDUPILOT_MPFS_MSS_SPI_ENABLED)
    zephyr_library_sources(${_HAL_DRIVERS}/spi/spi_mchp_mss_cs.c)
    # spi_context.h is private to Zephyr's in-tree SPI dir
    zephyr_library_include_directories(${ZEPHYR_BASE}/drivers/spi)
endif()

# fabric CoreI2C driver — not yet wired in, kept disabled (see cape_i2c in the overlay).
#if(CONFIG_DT_HAS_ARDUPILOT_MPFS_COREI2C_ENABLED)
#    zephyr_library_sources(${_HAL_DRIVERS}/i2c/i2c_mchp_corei2c.c)
#endif()

# no DT-gate needed: DT_INST_FOREACH_STATUS_OKAY inside self-gates, so a
# missing DT node is a link error, not silence
zephyr_library_sources(${_HAL_DRIVERS}/pwm/pwm_fabric_servo.c)

if(CONFIG_DT_HAS_MICROCHIP_MIV_IHC_RTL_V2_ENABLED)
    zephyr_library_sources(${_HAL_DRIVERS}/ipm/ipm_mchp_ihc.c)
endif()

if(CONFIG_OPENAMP)
    # resource table needs its own metal device region alongside shm
    zephyr_compile_definitions(METAL_MAX_DEVICE_REGIONS=2)
    zephyr_library_sources(${_HAL_DRIVERS}/../ipc/rpmsg_remote.c)
    # must land in .rodata, not past the heap — see resource_table.ld
    zephyr_linker_sources(RODATA ${CMAKE_CURRENT_LIST_DIR}/resource_table.ld)
endif()
