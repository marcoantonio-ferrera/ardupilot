# AP_HAL_Zephyr

ArduPilot HAL for Zephyr RTOS.

Portable across Zephyr targets — DT aliases and Kconfig drive the HAL, per-board silicon lives under `boards/<vendor>/<board>/`, arch flags come from a short list in each board's `CMakeLists.txt`. No code in the generic HAL tree depends on which SoC you're building for.

Supported boards:

| Board | Arch | Notes |
|---|---|---|
| `beaglev_fire` | RISC-V 64 (MPFS U54) | Flies in AMP alongside Linux |

## Layout

```
libraries/AP_HAL_Zephyr/
  *.cpp *.h                           generic HAL
    HAL_Zephyr_Class.cpp                wires drivers into AP_HAL::HAL
    Scheduler.cpp                       k_timer / k_thread
    UARTDriver.cpp                      uart_*
    I2CDevice.cpp                       i2c_transfer
    SPIDevice.cpp                       spi_transceive
    GPIO.cpp                            1-5 banks, Kconfig-sized
    Storage.cpp                         ZMS and/or FAT32-on-SD
    AnalogIn.cpp                        adc_read
    RCInput.cpp                         SBUS via AP_RCProtocol
    RCOutput.cpp                        pwm_set
    Semaphores.cpp                      k_mutex / k_sem
  Kconfig                             HAL-specific tunables
  include/ap_hal_zephyr_compat.h      portability shims (force-included)
  drivers/                            out-of-tree Zephyr drivers
    spi/spi_mchp_mss_cs.c               forked MSS SPI (DT-gated)
    pwm/pwm_fabric_servo.c              BVF fabric PWM (DT-gated)
  dts/bindings/                       HAL-wide DT bindings
  boards/<vendor>/<board>/            per-board silicon + ABI flags
    CMakeLists.txt                      AP_BOARD_ABI_CFLAGS + init sources
    <board>_<soc>.overlay               DT aliases, peripheral enables
    <board>_<soc>.conf                  Zephyr Kconfig fragment
  hwdef/<board>/                      ArduPilot-side board config
    hwdef.dat                           registers board with WAF
    hwdef.h                             sensor probes, feature gates
  app/                                Zephyr cmake "application"
    CMakeLists.txt                      WAF-hybrid glue
    prj.conf                            empty (board .conf holds all)
    Kconfig                             sources HAL Kconfig
    main.cpp                            main() → ardupilot_entry()
```

## Build

```bash
export CROSS_COMPILE=riscv-none-elf-           # or arm-zephyr-eabi-, xtensa-zephyr-elf-
export PATH=/path/to/toolchain/bin:$PATH
./waf configure --board beaglev_fire
./waf arducopter                                # or arduplane, ardurover, ardusub
```

Output: `build/<board>/zephyr_build/zephyr/zephyr.elf`.

### How the build connects WAF and Zephyr

WAF compiles ArduPilot into static `.a` libraries, Zephyr's cmake links those with the kernel into `zephyr.elf`.

```
./waf configure --board <board>
    class zephyr.configure_toolchain — CROSS_COMPILE → cfg.env.TOOLCHAIN
    zephyr.py configure              — ZEPHYR_BASE, board HAL dir discovery
    force-includes                   — ap_hal_zephyr_compat.h, hwdef.h

./waf arducopter
    pre_build
        cmake configure              — autoconf.h, DT headers
        cmake --build showinc        — Zephyr include dirs   → includes.list
        cmake --build showdefs       — Zephyr CONFIG_* defs  → compile_defs.list
        file(WRITE) at configure     — board ABI flags       → ap_board_abi.rsp
    load_generated_includes task
        prepend INCLUDES             ← includes.list
        prepend CFLAGS/CXXFLAGS (-D) ← compile_defs.list
        prepend CFLAGS/CXXFLAGS      ← @ap_board_abi.rsp  (GCC expands inline)
    WAF compiles ArduPilot          → libarducopter.a, libArduCopter_libs.a
    post-link: cmake --build        — kernel + WAF libs     → zephyr.elf
```

Three cmake-generated files feed WAF:

- `includes.list` — Zephyr's include paths (`INTERFACE_INCLUDE_DIRECTORIES` on `zephyr_interface`)
- `compile_defs.list` — Zephyr's `CONFIG_*` definitions (`INTERFACE_COMPILE_DEFINITIONS`)
- `ap_board_abi.rsp` — ABI flags declared by the board's `CMakeLists.txt`, written directly by cmake

## Prerequisites

- Zephyr toolchain for your target: `riscv-none-elf-`, `arm-zephyr-eabi-`, `xtensa-zephyr-elf-`.
- Python 3.10+ with `pykwalify` and `kconfiglib`, CMake ≥ 3.20, Ninja, `dtc`.
- `ardupilot/modules/zephyr` submodule initialised.
- `CROSS_COMPILE` env var set before running `./waf configure`.

Non-standard workspace layouts (e.g. a vendor HAL module outside the standard `ardupilot/modules/` tree) can be pointed to with `ZEPHYR_MODULES=<path>`.

## Adding a board

Worked example: BeagleBone AI-64 (TI J721E, running Zephyr on a Cortex-R5F in the MAIN_R5FSS0 cluster). The Zephyr board target is `beaglebone_ai64/j721e/main_r5f0_0`.

This board is a real constraint test: the R5F core gets a small slice of the J721E's memory map (tens of KB of TCM by default), so many ArduPilot features must be disabled or re-sized. It shows what the HAL port looks like on a tight ARM target.

### 1. hwdef.dat

```
define HAL_BOARD_NAME "BeagleBone-AI64"
define ZEPHYR_BOARD   beaglebone_ai64/j721e/main_r5f0_0
```

`libraries/AP_HAL_Zephyr/hwdef/<name>/hwdef.dat` registers the board with WAF. `ZEPHYR_BOARD` must match a board target under `modules/zephyr/boards/`.

### 2. Board HAL directory

Pick a vendor subdirectory (tidying convention, not enforced):

```
libraries/AP_HAL_Zephyr/boards/beagle/beaglebone_ai64/
    CMakeLists.txt
    beaglebone_ai64_j721e_main_r5f0_0.overlay
    beaglebone_ai64_j721e_main_r5f0_0.conf
```

`CMakeLists.txt`:

```cmake
# ABI flags — must match what Zephyr's cmake/compiler/gcc/target_arm.cmake
# resolves for this SoC, or WAF's .o files won't link against the kernel.
# Cortex-R5F: VFPv3 with 16 d-registers, hard-float ABI.
set(AP_BOARD_ABI_CFLAGS
    -mcpu=cortex-r5
    -mfpu=vfpv3-d16
    -mfloat-abi=hard
    PARENT_SCOPE)

# Optional: silicon-specific init sources.
# zephyr_library_sources(${CMAKE_CURRENT_LIST_DIR}/j721e_mcu_init.c)

# Optional: shared out-of-tree drivers from libraries/AP_HAL_Zephyr/drivers/.
# set(_HAL_DRIVERS ${CMAKE_CURRENT_LIST_DIR}/../../../drivers)
# if(CONFIG_DT_HAS_<your-driver-compatible>_ENABLED)
#     zephyr_library_sources(${_HAL_DRIVERS}/<driver>/<source>.c)
# endif()
```

Keep `AP_BOARD_ABI_CFLAGS` minimal. Only flags that affect `.o` link compatibility belong here — arch, ABI, FPU mode. Warning sets, `-fno-strict-aliasing`, `-std=c17`, and section flags are set for WAF builds in `Tools/ardupilotwaf/boards.py`.

`beaglebone_ai64_j721e_main_r5f0_0.overlay`:

```dts
/ {
    chosen {
        zephyr,console = &main_uart0;
    };

    aliases {
        ardupilot-serial0 = &main_uart0;   /* MAVLink — shares wire with console */
        ardupilot-serial1 = &main_uart2;   /* GPS */

        ardupilot-i2c0 = &main_i2c0;
        ardupilot-spi0 = &main_mcspi0;
        ardupilot-pwm0 = &main_pwm0;

        ardupilot-gpio0 = &main_gpio0;
        ardupilot-gpio1 = &main_gpio1;

        /* Optional */
        /* ardupilot-adc0    = &adc0;        */
        /* ardupilot-rcinput = &main_uart3;  */
    };
};

&main_uart0 { status = "okay"; current-speed = <115200>; };
&main_uart2 { status = "okay"; current-speed = <9600>;   };
&main_i2c0  { status = "okay"; };
&main_mcspi0{ status = "okay"; };
&main_pwm0  { status = "okay"; };
```

Node names above are illustrative — check your actual DTS under `modules/zephyr/boards/beagle/beaglebone_ai64/` and the J721E SoC files for the real labels.

Overlay basename convention: Zephyr board target with `/` replaced by `_`. `zephyr.py` globs for exactly one `*.overlay` in the board directory, so the exact filename doesn't matter as long as there's only one.

`beaglebone_ai64_j721e_main_r5f0_0.conf`:

```
CONFIG_CPP=y
CONFIG_STD_CPP17=y
CONFIG_REQUIRES_FULL_LIBCPP=y

# Must match -mfloat-abi=hard in AP_BOARD_ABI_CFLAGS.
CONFIG_FPU=y
CONFIG_FPU_SHARING=y

CONFIG_SPEED_OPTIMIZATIONS=y
CONFIG_SYS_CLOCK_TICKS_PER_SEC=1000
CONFIG_MAIN_THREAD_PRIORITY=8
CONFIG_THREAD_NAME=y

# J721E MAIN_R5FSS0_CORE0 runs out of TCM by default — tens of KB.
# EKF3 wants 6-10 MB active heap and won't fit; disable in hwdef.h
# (HAL_NAVEKF3_AVAILABLE 0) and rely on EKF2 or DCM.
CONFIG_MAIN_STACK_SIZE=8192
CONFIG_HEAP_MEM_POOL_SIZE=16384

CONFIG_UART_INTERRUPT_DRIVEN=y
CONFIG_RING_BUFFER=y

CONFIG_I2C=y
CONFIG_SPI=y
CONFIG_GPIO=y
CONFIG_PWM=y

# Reduce UART TX rings on small-RAM targets.
CONFIG_AP_HAL_UART_TX_BUF_SIZE=4096

# Storage: pick ONE backend (or leave disabled for RAM-only parameters).
# CONFIG_ZMS=y
# CONFIG_FLASH=y
# CONFIG_FLASH_MAP=y
```

### 3. hwdef.h

`libraries/AP_HAL_Zephyr/hwdef/beaglebone_ai64/hwdef.h`:

```c
#pragma once

/* SPIDevice.cpp always expands this macro; empty is fine if no SPI sensors. */
#define HAL_ZEPHYR_SPI_DEVICES

#define HAL_INS_PROBE_LIST \
    ADD_BACKEND(AP_InertialSensor_Invensense::probe(*this, \
        hal.i2c_mgr->get_device(0, 0x68), ROTATION_NONE))

#define HAL_BARO_PROBE_LIST \
    ADD_BACKEND(AP_Baro_BMP280::probe(*this, \
        std::move(hal.i2c_mgr->get_device(0, 0x76))))

#define HAL_MAG_PROBE_LIST \
    ADD_BACKEND(DRIVER_HMC5843, \
        AP_Compass_HMC5843::probe(hal.i2c_mgr->get_device(0, 0x1e)))

#define DEFAULT_SERIAL0_PROTOCOL SerialProtocol_MAVLink2
#define DEFAULT_SERIAL0_BAUD     115
#define DEFAULT_SERIAL1_PROTOCOL SerialProtocol_GPS
#define DEFAULT_SERIAL1_BAUD     9

/* Small-RAM target — shed anything non-essential. */
#define HAL_NAVEKF3_AVAILABLE 0
#define HAL_MOUNT_ENABLED     0
#define AP_WINCH_ENABLED      0
#define AP_GPS_SBF_ENABLED    0

/* GyroFFT needs AP_HAL::DSP; AP_HAL_Zephyr ships Empty::DSP. Disable
 * GyroFFT before the AP_Math/AP_Mission/AP_InternalError pre-include
 * dance below (those pull AP_Vehicle → AP_GyroFFT transitively). */
#ifndef HAL_GYROFFT_ENABLED
#define HAL_GYROFFT_ENABLED 0
#endif

#undef  AP_MAV_DEFAULT_STREAM_RATE_PARAMS
#define AP_MAV_DEFAULT_STREAM_RATE_PARAMS 10

/* ARRAY_SIZE / MAX dance. Must come after any feature gates above. */
#undef ARRAY_SIZE
#define ARRAY_SIZE(_arr) (sizeof(_arr) / sizeof(_arr[0]))
#include <zephyr/sys/cbprintf.h>
#ifdef __cplusplus
#include <AP_InternalError/AP_InternalError.h>
#include <AP_Math/AP_Math.h>
#include <AP_Mission/AP_Mission.h>
#endif
#undef ARRAY_SIZE
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
```

See [`hwdef/beaglev_fire/hwdef.h`](hwdef/beaglev_fire/hwdef.h) for a complete real-world reference.

### 4. Build

```bash
export CROSS_COMPILE=arm-zephyr-eabi-
./waf configure --board beaglebone_ai64
./waf arducopter

cat build/beaglebone_ai64/zephyr_build/ap_board_abi.rsp
# -mcpu=cortex-r5
# -mfpu=vfpv3-d16
# -mfloat-abi=hard
```

First build usually fails on probe signatures or missing symbols — iterate on hwdef.h, overlay, and .conf.

No edits to `boards.py`, `zephyr.py`, any generic HAL `.cpp`/`.h`, or `app/CMakeLists.txt` are needed.

## DT aliases

The HAL resolves every peripheral through a standard alias. Missing aliases cause the HAL to return `nullptr` or no-op; nothing crashes.

| Alias | Node compatible | Purpose |
|---|---|---|
| `chosen { zephyr,console }` | `uart` | Console and `printk` output. Required. |
| `ardupilot-serial0` | `uart` | SERIAL0 (MAVLink). Falls back to `zephyr,console`. |
| `ardupilot-serial1..4` | `uart` | Additional SERIAL ports. |
| `ardupilot-i2c0..3` | `i2c` | I2C buses for sensors. |
| `ardupilot-spi0..1` | `spi` | SPI buses. Also needs `HAL_ZEPHYR_SPI_DEVICES` in hwdef.h. |
| `ardupilot-gpio0..4` | `gpio-controller` | Up to 5 banks × 32 pins. `CONFIG_AP_HAL_GPIO_NUM_CONTROLLERS` sets count. |
| `ardupilot-pwm0` | `pwm` | Servo output. Any Zephyr PWM driver. |
| `ardupilot-adc0` | `adc` | Analog inputs. |
| `ardupilot-rcinput` | `uart` | SBUS input, configured automatically at 100 kbaud 8E2. |
| `storage_partition` (nodelabel) | child of `fixed-partitions` | ZMS flash backing. Required only when `CONFIG_ZMS=y`. |

GPIO pin numbering: HAL pin = `bank * 32 + pin_within_bank`. `gpio2` pin 4 is HAL pin 68.

## Kconfig

Defined in [`Kconfig`](Kconfig).

| Symbol | Default | Purpose |
|---|---|---|
| `AP_HAL_I2C_FORCE_MSG_RESTART` | `y if SOC_POLARFIRE` | Append `I2C_MSG_RESTART` on write→read. MPFS needs it; STM32/nRF/ESP32 reject it. |
| `AP_HAL_I2C_FAST_THRESHOLD_HZ` | `400001 if SOC_POLARFIRE`, else `400000` | `bus_clock > threshold` selects FAST mode. MPFS PCLK/256 ≈ 585 kHz exceeds the 400 kHz fast-mode spec, so 400000 must stay in STANDARD. |
| `AP_HAL_UART_PLIC_REARM` | `y if SOC_POLARFIRE` | Re-arm PLIC source on every TX and via a scheduler watchdog. Works around OpenSBI's PLIC reset in AMP setups. Harmless on NVIC/Xtensa. |
| `AP_HAL_UART_TX_BUF_SIZE` | `32768` | Per-UART TX ring size. Five instances × 32 KiB = 160 KiB BSS. |
| `AP_HAL_UART_SERIAL0_MIN_BAUD` | `0` | If > 0, clamps SERIAL0 baud to this minimum. BVF sets 115200 to keep its hardwired MP link working even with a stale `SERIAL0_BAUD` param. |
| `AP_HAL_GPIO_NUM_CONTROLLERS` | `3` | Number of `ardupilot-gpio<N>` aliases to probe. Range 1-5. |
| `AP_HAL_ADC_BITS` | `12` | ADC resolution for raw-to-volts. |
| `AP_HAL_BOARD_VOLTAGE_MV` | `3300` | Supply voltage used by `AnalogIn::board_voltage()`. |
| `AP_HAL_STORAGE_SECTOR_SIZE` | `4096` | ZMS sector size. |
| `AP_HAL_STORAGE_SECTOR_COUNT` | `64` | ZMS sector count. |
| `AP_HAL_ZEPHYR_MOCK_SENSORS` | `n` | Stub IMU/baro for simulation. Never enable on hardware. |

## File ownership

Editable:

- `ardupilot/libraries/AP_HAL_Zephyr/**`
- `ardupilot/Tools/ardupilotwaf/zephyr.py`
- The `class zephyr` section of `ardupilot/Tools/ardupilotwaf/boards.py`

Off-limits:

- `ardupilot/modules/zephyr/**` (Zephyr kernel source)
- Other `ardupilot/libraries/AP_HAL_*/` directories
- ArduPilot core libraries (`AP_HAL`, `AP_Math`, `AP_RCProtocol`, `AP_InertialSensor`, …)

A fix that seems to require touching off-limits code should instead use a Kconfig override, a hwdef.h feature gate, an out-of-tree driver under `drivers/`, or a shim in `include/ap_hal_zephyr_compat.h`. See [`drivers/spi/spi_mchp_mss_cs.c`](drivers/spi/spi_mchp_mss_cs.c) for a Zephyr driver fork kept inside our tree.

## Troubleshooting

**Link error: ABI mismatch (`mabi=lp64` vs `mabi=lp64d` and similar).**
`AP_BOARD_ABI_CFLAGS` in `boards/<v>/<b>/CMakeLists.txt` disagrees with what Zephyr resolved. Check `build/<board>/zephyr_build/ap_board_abi.rsp` against Zephyr's compiler invocations in `compile_commands.json` (enable with `CMAKE_EXPORT_COMPILE_COMMANDS=ON` in `app/CMakeLists.txt`).

**`CROSS_COMPILE env var must be set`.**
Export `CROSS_COMPILE=<toolchain-prefix>-` before `./waf configure`.

**`hwdef.dat not found`.**
Create `libraries/AP_HAL_Zephyr/hwdef/<board>/hwdef.dat` with `define ZEPHYR_BOARD <target>`.

**`expected exactly one boards/*/<board> directory, found 0`.**
Create the board HAL directory with at least one `.overlay` file.

**`AP_BOARD_ABI_CFLAGS not set by <dir>/CMakeLists.txt`.**
Warning from `app/CMakeLists.txt`. WAF builds without ABI flags, but the `.o` files won't link against the Zephyr kernel. Add `set(AP_BOARD_ABI_CFLAGS ... PARENT_SCOPE)` to the board's `CMakeLists.txt`.

**`EKF3 not enough memory`.**
`CONFIG_HEAP_MEM_POOL_SIZE` too small. EKF3 wants 6-10 MB active. On a 1 MB-RAM MCU, disable it (`#define HAL_NAVEKF3_AVAILABLE 0` in hwdef.h) and use EKF2 or DCM.

**`'FrequencyPeak' in 'class AP_HAL::DSP' does not name a type`.**
`HAL_GYROFFT_ENABLED` is on but `HAL_WITH_DSP` is off. Either `#define HAL_GYROFFT_ENABLED 0` in hwdef.h **before** the AP_Math pre-include dance, or implement `AP_HAL_Zephyr/DSP.*`.

**`Evaluation file to be written multiple times with different content`.**
Something inside `app/CMakeLists.txt` is using `file(GENERATE)` with `$<COMPILE_LANGUAGE:...>` expressions. Resolve generator expressions at configure time instead.

**Stale cmake cache: wrong board name in build log.**
`rm -rf build/<board>/zephyr_build && ./waf configure --board <board>`.

**I2C hangs with `-EAGAIN` on MPFS.**
PLIC source enable cleared during AMP boot. Check `CONFIG_MP_MAX_NUM_CPUS=1` in the board `.conf` (prevents `plic_irq_enable_set_state`'s CPU loop from clobbering the enable bit). UART has `CONFIG_AP_HAL_UART_PLIC_REARM=y` to recover automatically.

## References

- [`hwdef/beaglev_fire/hwdef.h`](hwdef/beaglev_fire/hwdef.h) — real board config.
- [`boards/beagle/beaglev_fire/`](boards/beagle/beaglev_fire/) — real board HAL directory.
- [Zephyr documentation](https://docs.zephyrproject.org/).
