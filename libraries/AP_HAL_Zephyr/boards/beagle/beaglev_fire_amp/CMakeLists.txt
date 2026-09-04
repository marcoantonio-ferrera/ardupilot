# BeagleV-Fire (MPFS U54) board sources, pulled in from app/CMakeLists.txt.

# Must match target_riscv.cmake for this SoC or mismatched -mabi/-march is a
# hard link error.
set(AP_BOARD_ABI_CFLAGS
    -march=rv64imafdc_zicsr_zifencei
    -mabi=lp64d
    -mcmodel=medany
    PARENT_SCOPE)

# mss_ahb_div.c is intentionally not built; see that file.

set(_HAL_DRIVERS ${CMAKE_CURRENT_LIST_DIR}/../../../drivers)

# microSD-over-SPI1 shares the bus with an ADC; upstream mpfs-mss SPI has no
# GPIO CS handling.
if(CONFIG_DT_HAS_ARDUPILOT_MPFS_MSS_SPI_ENABLED)
    zephyr_library_sources(${_HAL_DRIVERS}/spi/spi_mchp_mss_cs.c)
    # spi_context.h is private to Zephyr's in-tree SPI dir
    zephyr_library_include_directories(${ZEPHYR_BASE}/drivers/spi)
endif()

if(CONFIG_DT_HAS_ARDUPILOT_MPFS_COREI2C_ENABLED)
    zephyr_library_sources(${_HAL_DRIVERS}/i2c/i2c_mchp_corei2c.c)
endif()

zephyr_library_sources(${_HAL_DRIVERS}/pwm/pwm_fabric_servo.c)

if(CONFIG_DT_HAS_MICROCHIP_MIV_IHC_RTL_V2_ENABLED)
    zephyr_library_sources(${_HAL_DRIVERS}/ipm/ipm_mchp_ihc.c)
endif()

if(CONFIG_OPENAMP)
    zephyr_compile_definitions(METAL_MAX_DEVICE_REGIONS=2)
    zephyr_library_sources(${_HAL_DRIVERS}/../ipc/rpmsg_remote.c)
    zephyr_linker_sources(RODATA ${CMAKE_CURRENT_LIST_DIR}/resource_table.ld)
endif()
