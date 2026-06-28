/*
 * Must stay in .rodata with the low, loadable sections: placed after the
 * heap region, objcopy -O binary zero-fills the gap and zephyr.bin balloons
 * to ~heap-size, which HSS then fails to load.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR MIT)
 */

KEEP(*(.resource_table*))
