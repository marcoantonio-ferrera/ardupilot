# BeagleV-Fire (MPFS U54) board sources, pulled in from app/CMakeLists.txt.

# ABI flags WAF must pass to riscv-none-elf-g++ so its objects can link
# with the Zephyr kernel. Must match target_riscv.cmake for this SoC —
# mismatched -mabi or -march is a hard link error.
# app/CMakeLists.txt writes these into ap_board_abi.rsp.
set(AP_BOARD_ABI_CFLAGS
    -march=rv64imafdc_zicsr_zifencei
    -mabi=lp64d
    -mcmodel=medany
    PARENT_SCOPE)

zephyr_library_sources(${CMAKE_CURRENT_LIST_DIR}/mss_ahb_div.c)

# out-of-tree drivers live in the shared pool so other MPFS variants can
# reuse them, but registration is per-board.
set(_HAL_DRIVERS ${CMAKE_CURRENT_LIST_DIR}/../../../drivers)

# forked mpfs-mss SPI with GPIO CS — microSD-over-SPI1 shares the bus
# with an ADC and upstream has no GPIO CS handling. DT-gated.
if(CONFIG_DT_HAS_ARDUPILOT_MPFS_MSS_SPI_ENABLED)
    zephyr_library_sources(${_HAL_DRIVERS}/spi/spi_mchp_mss_cs.c)
    # spi_context.h is private to Zephyr's in-tree SPI dir
    zephyr_library_include_directories(${ZEPHYR_BASE}/drivers/spi)
endif()

# ROBOTICS cape fabric servo PWM. Self-gating via DT_INST_FOREACH_STATUS_OKAY —
# always compile so a missing DT node surfaces as a link error, not silence.
zephyr_library_sources(${_HAL_DRIVERS}/pwm/pwm_fabric_servo.c)
