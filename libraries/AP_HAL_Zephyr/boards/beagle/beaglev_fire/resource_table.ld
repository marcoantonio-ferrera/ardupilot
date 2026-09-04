/*
 * OpenAMP resource table. Must stay in .rodata, not placed after the heap --
 * otherwise objcopy -O binary zero-fills the gap and zephyr.bin balloons
 * until HSS fails to load it.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR MIT)
 */

KEEP(*(.resource_table*))
