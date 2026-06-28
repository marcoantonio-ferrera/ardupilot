/*
 * Byte-stream bridge between the OpenAMP rpmsg endpoint (rpmsg_remote.c) and
 * the AP_HAL_Zephyr RPMsgUARTDriver. The driver (C++, compiled by waf without
 * the OpenAMP include path) talks to the rpmsg layer (C, compiled into the
 * Zephyr image) only through this byte-buffer API -- no OpenAMP/libmetal types
 * cross the boundary.
 *
 * Threading contract:
 *  - ap_rpmsg_serial_write() runs in the AP_DDS thread; it is NON-BLOCKING and
 *    returns how many bytes were accepted into the TX ring (the rpmsg mng
 *    thread drains that ring into the 8x512B pool with rpmsg_trysend).
 *  - ap_rpmsg_serial_read()/_rxavail()/_discard_rx() run in the AP_DDS thread;
 *    the RX ring is filled by the rpmsg rx callback in the mng thread.
 * Both rings are strict single-producer/single-consumer (Zephyr ring_buf), so
 * no extra locking is required.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR MIT)
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

bool ap_rpmsg_serial_up(void);

uint32_t ap_rpmsg_serial_read(uint8_t *dst, uint32_t len);

uint32_t ap_rpmsg_serial_rxavail(void);

uint32_t ap_rpmsg_serial_write(const uint8_t *src, uint32_t len);

uint32_t ap_rpmsg_serial_txspace(void);

bool ap_rpmsg_serial_tx_pending(void);

void ap_rpmsg_serial_discard_rx(void);

#ifdef __cplusplus
}
#endif
