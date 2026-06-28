/*
 * OpenAMP RPMsg "remote" bring-up for AP_HAL_Zephyr (Linux master <-> Zephyr
 * remote over the Microchip Mi-V IHC). Runs as its own thread so the
 * blocking master handshake never stalls ArduPilot boot.
 *
 * SPDX-License-Identifier: (GPL-2.0 OR MIT)
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/ipm.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/ring_buffer.h>

#include <openamp/open_amp.h>
#include <metal/sys.h>
#include <metal/io.h>
#include <resource_table.h>

#include "ap_rpmsg_serial.h"

LOG_MODULE_REGISTER(ap_rpmsg, LOG_LEVEL_INF);

#if !DT_HAS_CHOSEN(zephyr_ipc)
#error "rpmsg_remote.c needs chosen zephyr,ipc (the IHC IPM channel)"
#endif
#if !DT_HAS_CHOSEN(zephyr_ipc_shm)
#error "rpmsg_remote.c needs chosen zephyr,ipc_shm (rpmsg shared buffer pool)"
#endif

/* Microchip rproc lifecycle messages carried over the IHC (see Linux
 * mchp_ipc_sbi_remoteproc + the pic64gx example). */
#define MCHP_IHC_RPROC_READY  (0xFFFFFF00)
#define MCHP_IHC_RPROC_STOP   (0xFFFFFF02)

#define SHM_NODE        DT_CHOSEN(zephyr_ipc_shm)
#define SHM_START_ADDR  DT_REG_ADDR(SHM_NODE)
#define SHM_SIZE        DT_REG_SIZE(SHM_NODE)

#define RPMSG_THREAD_STACK_SIZE 4096
/* PREEMPTIBLE at prio 5 (== ap_io): above main (prio 8) so it can always
 * drain TX/RX; below the fast loop (ap_timer prio 2), which always preempts.
 * Yields k_sleep(1ms) each batch (rpmsg_mng_task) so it cannot starve main
 * under sustained DDS traffic. */
#define RPMSG_THREAD_PRIO       K_PRIO_PREEMPT(5)
#define RPROC_READY_TIMEOUT     K_SECONDS(15)

/* Byte-stream bridge buffers for AP_DDS (RPMsgUARTDriver rides these).
 * Single-producer/single-consumer per ring (rx: mng thread fills, DDS thread
 * drains; tx: reverse) -- no locking needed. */
#define RPMSG_RX_RING_SIZE      4096
#define RPMSG_TX_RING_SIZE      8192

static const struct device *const ipm_handle = DEVICE_DT_GET(DT_CHOSEN(zephyr_ipc));

static metal_phys_addr_t shm_physmap = SHM_START_ADDR;
static metal_phys_addr_t rsc_tab_physmap;
static struct metal_io_region shm_io_data;
static struct metal_io_region rsc_io_data;
static struct metal_io_region *shm_io = &shm_io_data;
static struct metal_io_region *rsc_io = &rsc_io_data;

static struct rpmsg_virtio_device rvdev;
static struct fw_resource_table *rsc_table;
static struct rpmsg_device *rpdev;

static struct rpmsg_endpoint echo_ept;

/* AP_DDS byte-stream endpoint (named "rpmsg-tty" so Linux's in-tree rpmsg_tty
 * driver creates /dev/ttyRPMSG0 for the micro-ROS agent). */
static struct rpmsg_endpoint dds_ept;
RING_BUF_DECLARE(rpmsg_rx_ring, RPMSG_RX_RING_SIZE);
RING_BUF_DECLARE(rpmsg_tx_ring, RPMSG_TX_RING_SIZE);
static atomic_t dds_link_up;
/* max rpmsg payload per message (buffer size - header); set from the vdev. */
static int tx_chunk_max = 480;
static uint32_t rpmsg_rx_dropped;

static K_SEM_DEFINE(data_sem, 0, 1);

static K_THREAD_STACK_DEFINE(rpmsg_mng_stack, RPMSG_THREAD_STACK_SIZE);
static struct k_thread rpmsg_mng_thread;

/* IHC RX: master poked us. Wake the management loop to drain the vring. */
static void platform_ipm_callback(const struct device *dev, void *context,
				  uint32_t id, volatile void *data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(context);
	ARG_UNUSED(id);
	k_sem_give(&data_sem);
}

/* OpenAMP kick: notify the master that vring `id` has work. The IHC channel
 * holds only one outstanding message (-EBUSY if unconsumed); retry with a
 * yield rather than busy-spin, up to ~4s so a dead Linux peer can't block the
 * FC (link just stays idle until it returns -- rpmsg-virtio kicks coalesce). */
static int mailbox_notify(void *priv, uint32_t id)
{
	ARG_UNUSED(priv);

	for (int i = 0; i < 4000; i++) {        /* ~4 s ceiling @ 1 ms tick */
		/* wait=0: fire-and-forget -- wait=1 busy-waits for Linux's ACK
		 * while holding the channel mutex at prio 5, which can starve
		 * main and freeze the FC. */
		if (ipm_send(ipm_handle, 0, id, &id, sizeof(id)) != -EBUSY) {
			return 0;   /* kicked */
		}
		k_usleep(50);
	}
	return 0;
}

extern void mchp_ihc_dbg_dump(const struct device *dev);

static int platform_rproc_ready(void)
{
	uint32_t msg = MCHP_IHC_RPROC_READY;

	LOG_INF("rpmsg: RPROC_READY -> master; waiting for kick (<=15s)");
	ipm_send(ipm_handle, 0, 0, &msg, sizeof(msg));

	/* Poll once a second: re-announce (master may attach after our first,
	 * early send) and dump IHC regs to diagnose if it never arrives. */
	for (int i = 0; i < 15; i++) {
		if (k_sem_take(&data_sem, K_SECONDS(1)) == 0) {
			LOG_INF("rpmsg: master kicked back (after %d s)", i);
			return 0;
		}
		mchp_ihc_dbg_dump(ipm_handle);
		ipm_send(ipm_handle, 0, 0, &msg, sizeof(msg));
	}
	LOG_ERR("rpmsg: no master kick within timeout; link disabled");
	return -1;
}

static int platform_init(void)
{
	struct metal_init_params metal_params = METAL_INIT_DEFAULTS;
	int rsc_size;
	int status;

	status = metal_init(&metal_params);
	if (status) {
		LOG_ERR("metal_init failed: %d", status);
		return -1;
	}

	metal_io_init(shm_io, (void *)SHM_START_ADDR, &shm_physmap,
		      SHM_SIZE, -1, 0, NULL);

	/* CONFIG_OPENAMP_COPY_RSC_TABLE: copies the table to the zephyr,ipc-
	 * rsc-table address (0x91d50000) so the Linux master finds it there. */
	rsc_table_get((void **)&rsc_table, &rsc_size);
	rsc_tab_physmap = (uintptr_t)rsc_table;
	metal_io_init(rsc_io, rsc_table, &rsc_tab_physmap, rsc_size, -1, 0, NULL);

	if (!device_is_ready(ipm_handle)) {
		LOG_ERR("IHC IPM device not ready");
		return -1;
	}
	ipm_register_callback(ipm_handle, platform_ipm_callback, NULL);
	status = ipm_set_enabled(ipm_handle, 1);
	if (status) {
		LOG_ERR("ipm_set_enabled failed: %d", status);
		return -1;
	}
	LOG_INF("rpmsg: platform_init OK (shm/rsc-table/IHC ready)");
	return 0;
}

static void ns_bind_cb(struct rpmsg_device *rdev, const char *name, uint32_t src)
{
	ARG_UNUSED(rdev);
	ARG_UNUSED(src);
	LOG_WRN("unexpected name-service announce: %s", name);
}

static struct rpmsg_device *platform_create_rpmsg_vdev(void)
{
	struct fw_rsc_vdev_vring *vring_rsc;
	struct virtio_device *vdev;
	uintptr_t virt_addr;
	int ret;

	vdev = rproc_virtio_create_vdev(VIRTIO_DEV_DEVICE, VDEV_ID,
					rsc_table_to_vdev(rsc_table),
					rsc_io, NULL, mailbox_notify, NULL);
	if (!vdev) {
		LOG_ERR("rproc_virtio_create_vdev failed");
		return NULL;
	}
	LOG_INF("rpmsg: vdev created");

	if (platform_rproc_ready() != 0) {
		goto fail;
	}
	LOG_INF("rpmsg: waiting for remote virtio ready...");
	rproc_virtio_wait_remote_ready(vdev);
	LOG_INF("rpmsg: remote virtio ready; initialising vrings");

	/* vring addresses are filled in by the Linux master (da was -1) */
	vring_rsc = rsc_table_get_vring0(rsc_table);
	virt_addr = vring_rsc->da;
	ret = rproc_virtio_init_vring(vdev, 0, vring_rsc->notifyid,
				      (void *)virt_addr, rsc_io,
				      vring_rsc->num, vring_rsc->align);
	if (ret) {
		LOG_ERR("init vring0 failed: %d", ret);
		goto fail;
	}

	vring_rsc = rsc_table_get_vring1(rsc_table);
	virt_addr = vring_rsc->da;
	ret = rproc_virtio_init_vring(vdev, 1, vring_rsc->notifyid,
				      (void *)virt_addr, rsc_io,
				      vring_rsc->num, vring_rsc->align);
	if (ret) {
		LOG_ERR("init vring1 failed: %d", ret);
		goto fail;
	}

	ret = rpmsg_init_vdev(&rvdev, vdev, ns_bind_cb, shm_io, NULL);
	if (ret) {
		LOG_ERR("rpmsg_init_vdev failed: %d", ret);
		goto fail;
	}
	return rpmsg_virtio_get_rpmsg_device(&rvdev);

fail:
	rproc_virtio_remove_vdev(vdev);
	return NULL;
}

/* Mirrors Linux's `rpmsg_client_sample` module; kept alongside the DDS
 * endpoint as a zero-cost link-liveness check. */
static int echo_cb(struct rpmsg_endpoint *ept, void *data, size_t len,
		   uint32_t src, void *priv)
{
	ARG_UNUSED(priv);
	(void)rpmsg_send(ept, data, len);
	return RPMSG_SUCCESS;
}

/* AP_DDS byte-stream bridge (see ap_rpmsg_serial.h). */

/* RX from Linux: copy into the RX ring (drop on overflow -- XRCE retransmits).
 * Runs in the mng-thread context. */
static int dds_ept_cb(struct rpmsg_endpoint *ept, void *data, size_t len,
		      uint32_t src, void *priv)
{
	ARG_UNUSED(src);
	ARG_UNUSED(priv);

	uint32_t put = ring_buf_put(&rpmsg_rx_ring, data, len);
	if (put < len) {
		rpmsg_rx_dropped += (uint32_t)len - put;
	}
	/* DBG: storms at DDS rates. First inbound binds ept->dest_addr (until
	 * then the FC cannot transmit). */
	LOG_DBG("rpmsg-tty RX %u B (dest=0x%08x)", (unsigned)len, ept->dest_addr);
	return RPMSG_SUCCESS;
}

/* Mng-thread only (single consumer). Never blocks: on a full pool it leaves
 * data queued and returns, retried on the next kick or _write. */
static void rpmsg_tx_drain(void)
{
	if (!atomic_get(&dds_link_up)) {
		return;
	}
	while (!ring_buf_is_empty(&rpmsg_tx_ring)) {
		uint8_t *p;
		uint32_t claimed = ring_buf_get_claim(&rpmsg_tx_ring, &p,
						      (uint32_t)tx_chunk_max);
		if (claimed == 0) {
			break;
		}
		/* RPMSG_ADDR_ANY dest (no inbound from Linux yet) or a full pool
		 * both make rpmsg_trysend return negative -> keep data queued. */
		int ret = rpmsg_trysend(&dds_ept, p, claimed);
		if (ret == (int)claimed) {
			ring_buf_get_finish(&rpmsg_tx_ring, claimed);
		} else {
			/* DBG only -- fires per send under backpressure. */
			ring_buf_get_finish(&rpmsg_tx_ring, 0);
			LOG_DBG("rpmsg-tty TX stalled: trysend=%d claimed=%u dest=0x%08x",
				ret, (unsigned)claimed, dds_ept.dest_addr);
			break;
		}
	}
}

bool ap_rpmsg_serial_up(void)
{
	return atomic_get(&dds_link_up) != 0;
}

uint32_t ap_rpmsg_serial_read(uint8_t *dst, uint32_t len)
{
	if (dst == NULL || len == 0) {
		return 0;
	}
	return ring_buf_get(&rpmsg_rx_ring, dst, len);
}

uint32_t ap_rpmsg_serial_rxavail(void)
{
	return ring_buf_size_get(&rpmsg_rx_ring);
}

uint32_t ap_rpmsg_serial_write(const uint8_t *src, uint32_t len)
{
	if (src == NULL || len == 0) {
		return 0;
	}
	uint32_t put = ring_buf_put(&rpmsg_tx_ring, src, len);
	/* DBG only: per-write log would storm at DDS rates. */
	LOG_DBG("ap_rpmsg_serial_write len=%u put=%u", (unsigned)len, (unsigned)put);
	if (put > 0) {
		k_sem_give(&data_sem);
	}
	return put;
}

uint32_t ap_rpmsg_serial_txspace(void)
{
	return ring_buf_space_get(&rpmsg_tx_ring);
}

bool ap_rpmsg_serial_tx_pending(void)
{
	return !ring_buf_is_empty(&rpmsg_tx_ring);
}

void ap_rpmsg_serial_discard_rx(void)
{
	/* consumer-side drain (SPSC-safe; do NOT ring_buf_reset -- the mng
	 * thread may be putting concurrently). */
	uint8_t tmp[64];
	while (ring_buf_get(&rpmsg_rx_ring, tmp, sizeof(tmp)) > 0) {
	}
}

static void rpmsg_mng_task(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	LOG_INF("rpmsg: bring-up thread started (preemptible prio %d)",
		RPMSG_THREAD_PRIO);

	if (platform_init() != 0) {
		LOG_ERR("rpmsg platform init failed; link disabled");
		return;
	}

	rpdev = platform_create_rpmsg_vdev();
	if (!rpdev) {
		LOG_ERR("rpmsg vdev creation failed; link disabled");
		return;
	}

	if (rpmsg_create_ept(&echo_ept, rpdev, "rpmsg-client-sample",
			     RPMSG_ADDR_ANY, RPMSG_ADDR_ANY, echo_cb, NULL)) {
		LOG_ERR("failed to create echo endpoint");
		return;
	}

	if (rpmsg_create_ept(&dds_ept, rpdev, "rpmsg-tty",
			     RPMSG_ADDR_ANY, RPMSG_ADDR_ANY, dds_ept_cb, NULL)) {
		LOG_ERR("failed to create dds (rpmsg-tty) endpoint");
		return;
	}

	tx_chunk_max = rpmsg_virtio_get_buffer_size(rpdev);
	if (tx_chunk_max <= 0 || tx_chunk_max > RPMSG_TX_RING_SIZE) {
		tx_chunk_max = 480;
	}
	atomic_set(&dds_link_up, 1);

	LOG_INF("rpmsg link up (echo + dds 'rpmsg-tty', mtu %d)", tx_chunk_max);

	while (1) {
		k_sem_take(&data_sem, K_FOREVER);
		/* delivers RX (fires dds_ept_cb / echo_cb) and frees returned
		 * TX buffers, then push any queued TX into the freed buffers. */
		rproc_virtio_notified(rvdev.vdev, VRING1_ID);
		rpmsg_tx_drain();
		/* k_sleep (not k_yield): must actually let lower-prio main run -- see
		 * RPMSG_THREAD_PRIO. */
		k_sleep(K_MSEC(1));
	}
}

static int ap_rpmsg_remote_start(void)
{
	k_thread_create(&rpmsg_mng_thread, rpmsg_mng_stack,
			K_THREAD_STACK_SIZEOF(rpmsg_mng_stack),
			rpmsg_mng_task, NULL, NULL, NULL,
			RPMSG_THREAD_PRIO, 0, K_NO_WAIT);
	k_thread_name_set(&rpmsg_mng_thread, "ap_rpmsg");
	return 0;
}

SYS_INIT(ap_rpmsg_remote_start, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
