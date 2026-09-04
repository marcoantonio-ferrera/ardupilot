/*
 * Copyright (c) 2024 Microchip Technology Inc
 *
 * SPDX-License-Identifier: (GPL-2.0 OR MIT)
 */

#ifndef ZEPHYR_DRIVERS_IPM_MCHP_IHC_H_
#define ZEPHYR_DRIVERS_IPM_MCHP_IHC_H_

#include <zephyr/kernel.h>
#include <zephyr/drivers/ipm.h>
#include <zephyr/device.h>

 #define MCHP_IHC_REGS_IP_VERSION_OFFSET 	(0x0003BFC)

#define MCHP_IHCM_REGS_IRQ_MASK_OFFSET		(0U)
#define MCHP_IHCM_REGS_IRQ_STATUS_OFFSET	(0x8U)

#define MCHP_IHC_REGS_IRQ_STATUS_MP_OFFSET(remote_hart_id) \
	(remote_hart_id * 2)
#define MCHP_IHC_REGS_IRQ_STATUS_MP_NS_MASK    (1U)
/* MP bit for remote_hart_id in the shared per-module IRQ status reg; set on send, cleared by receiver */
#define MCHP_IHC_REGS_IRQ_STATUS_MP(remote_hart_id) \
	(MCHP_IHC_REGS_IRQ_STATUS_MP_NS_MASK << \
	 MCHP_IHC_REGS_IRQ_STATUS_MP_OFFSET(remote_hart_id))

#define MCHP_IHC_REGS_IRQ_STATUS_ACK_OFFSET(remote_hart_id) \
	(remote_hart_id * 2)
#define MCHP_IHC_REGS_IRQ_STATUS_ACK_NS_MASK    (2U)
/* ACK bit for remote_hart_id in the shared per-module IRQ status reg; set when receiver processes msg, cleared by sender */
#define MCHP_IHC_REGS_IRQ_STATUS_ACK(remote_hart_id) \
	(MCHP_IHC_REGS_IRQ_STATUS_ACK_NS_MASK << \
	 MCHP_IHC_REGS_IRQ_STATUS_ACK_OFFSET(remote_hart_id))

#define MCHP_IHC_REGS_IRQ_MASK_MP_OFFSET(remote_hart_id) \
	(remote_hart_id * 2)
#define MCHP_IHC_REGS_IRQ_MASK_MP_NS_MASK    (1U)
#define MCHP_IHC_REGS_IRQ_MASK_MP(remote_hart_id) \
	(MCHP_IHC_REGS_IRQ_MASK_MP_NS_MASK << \
	 MCHP_IHC_REGS_IRQ_MASK_MP_OFFSET(remote_hart_id))

#define MCHP_IHC_REGS_IRQ_MASK_ACK_OFFSET(remote_hart_id) \
	(remote_hart_id * 2)
#define MCHP_IHC_REGS_IRQ_MASK_ACK_NS_MASK    (2U)
#define MCHP_IHC_REGS_IRQ_MASK_ACK(remote_hart_id)\
	(MCHP_IHC_REGS_IRQ_MASK_ACK_NS_MASK << \
	 MCHP_IHC_REGS_IRQ_MASK_ACK_OFFSET(remote_hart_id))

#define MCHP_IHC_REGS_IRQ_DISABLE_MASK		(0x0U)

#define MCHP_IHCC_REGS_CTRL_OFFSET    (0U)
/* debug_id: despite the name, holds local+remote hart id (see HART_ID_* masks) */
#define MCHP_IHCC_REGS_DEBUG_ID_OFFSET    (0x4U)
/* message_depth: despite the name, holds max message size in bytes, fixed by the fabric build */
#define MCHP_IHCC_REGS_MSG_DEPTH_OFFSET    (0x8U)
#define MCHP_IHCC_REGS_MSG_IN_OFFSET    (0x20U)
#define MCHP_IHCC_REGS_MSG_OUT_OFFSET    (0x90U)

/* AACKIE in datasheet */
#define MCHP_IHC_REGS_CH_CTRL_ACKIE_OFFSET	(5U)
#define MCHP_IHC_REGS_CH_CTRL_ACKIE_NS_MASK	(BIT_MASK(1))
#define MCHP_IHC_REGS_CH_CTRL_ACKIE_MASK \
	(MCHP_IHC_REGS_CH_CTRL_ACKIE_NS_MASK << MCHP_IHC_REGS_CH_CTRL_ACKIE_OFFSET)

/* AMPACK in datasheet; set when remote acks our message, cleared by us after handling */
#define MCHP_IHC_REGS_CH_CTRL_ACKCLR_OFFSET	(4U)
#define MCHP_IHC_REGS_CH_CTRL_ACKCLR_NS_MASK	(BIT_MASK(1))
#define MCHP_IHC_REGS_CH_CTRL_ACKCLR_MASK \
	(MCHP_IHC_REGS_CH_CTRL_ACKCLR_NS_MASK << MCHP_IHC_REGS_CH_CTRL_ACKCLR_OFFSET)

/* BMPACK in datasheet; bit we set to send our ack to the remote */
#define MCHP_IHC_REGS_CH_CTRL_ACK_OFFSET	(3U)
#define MCHP_IHC_REGS_CH_CTRL_ACK_NS_MASK	(BIT_MASK(1))
#define MCHP_IHC_REGS_CH_CTRL_ACK_MASK \
	(MCHP_IHC_REGS_CH_CTRL_ACK_NS_MASK << MCHP_IHC_REGS_CH_CTRL_ACK_OFFSET)

/* AMPIE in datasheet */
#define MCHP_IHC_REGS_CH_CTRL_MPIE_OFFSET	(2U)
#define MCHP_IHC_REGS_CH_CTRL_MPIE_NS_MASK 	(BIT_MASK(1))
#define MCHP_IHC_REGS_CH_CTRL_MPIE_MASK \
	(MCHP_IHC_REGS_CH_CTRL_MPIE_NS_MASK << MCHP_IHC_REGS_CH_CTRL_MPIE_OFFSET)

/* AMP in datasheet; set by remote on new message, cleared by us (the receiver) */
#define MCHP_IHC_REGS_CH_CTRL_MP_OFFSET		(1U)
#define MCHP_IHC_REGS_CH_CTRL_MP_NS_MASK	(BIT_MASK(1))
#define MCHP_IHC_REGS_CH_CTRL_MP_MASK \
	(MCHP_IHC_REGS_CH_CTRL_MP_NS_MASK << MCHP_IHC_REGS_CH_CTRL_MP_OFFSET)

/* BMP in datasheet; set by us to signal a new message to the remote */
#define MCHP_IHC_REGS_CH_CTRL_RMP_OFFSET	(0U)
#define MCHP_IHC_REGS_CH_CTRL_RMP_NS_MASK	(BIT_MASK(1))
#define MCHP_IHC_REGS_CH_CTRL_RMP_MASK \
	(MCHP_IHC_REGS_CH_CTRL_RMP_NS_MASK << MCHP_IHC_REGS_CH_CTRL_RMP_OFFSET)

#define MCHP_IHC_REGS_LOCAL_HART_ID_OFFSET	(0U)
#define MCHP_IHC_REGS_LOCAL_HART_ID_NS_MASK	(BIT_MASK(8))
#define MCHP_IHC_REGS_LOCAL_HART_ID_MASK \
	(MCHP_IHC_REGS_LOCAL_HART_ID_NS_MASK << MCHP_IHC_REGS_LOCAL_HART_ID_OFFSET)

#define MCHP_IHC_REGS_REMOTE_HART_ID_OFFSET	(8U)
#define MCHP_IHC_REGS_REMOTE_HART_ID_NS_MASK	(BIT_MASK(8))
#define MCHP_IHC_REGS_REMOTE_HART_ID_MASK \
	(MCHP_IHC_REGS_REMOTE_HART_ID_NS_MASK << MCHP_IHC_REGS_REMOTE_HART_ID_OFFSET)

#define MCHP_IHC_REGS_MESSAGE_SIZE_MESSAGE_OUT_OFFSET	(0U)
#define MCHP_IHC_REGS_MESSAGE_SIZE_MESSAGE_OUT_NS_MASK	(BIT_MASK(8))
#define MCHP_IHC_REGS_MESSAGE_SIZE_MESSAGE_OUT_MASK \
	(MCHP_IHC_REGS_MESSAGE_SIZE_MESSAGE_OUT_NS_MASK << \
	 MCHP_IHC_REGS_MESSAGE_SIZE_MESSAGE_OUT_OFFSET)

#define MCHP_IHC_REGS_MESSAGE_SIZE_MESSAGE_IN_OFFSET	(8U)
#define MCHP_IHC_REGS_MESSAGE_SIZE_MESSAGE_IN_NS_MASK	(BIT_MASK(8))
#define MCHP_IHC_REGS_MESSAGE_SIZE_MESSAGE_IN_MASK \
	(MCHP_IHC_REGS_MESSAGE_SIZE_MESSAGE_IN_NS_MASK << \
	 MCHP_IHC_REGS_MESSAGE_SIZE_MESSAGE_IN_OFFSET)

struct mchp_ihcc_reg_map_t {
	volatile uint32_t ctrl;
	const volatile uint32_t debug_id;
	const volatile uint32_t message_depth;
	const volatile uint32_t reserved1[5U];
	volatile uint32_t msg_in[28];
	volatile uint32_t msg_out[28];
};

struct mchp_ihcm_reg_map_t {
	volatile uint32_t irq_mask;
	const volatile uint32_t reserved1;
	const volatile uint32_t irq_status;
	const volatile uint32_t reserved2[61];
};

struct mchp_ihc_config {
	uintptr_t ihc_regs;
	/* only DT status-okay children */
	const struct device ** ihcm_list;
	size_t num_ihcm;
};

struct mchp_ihcm_config {
	uintptr_t ihcm_regs;
	/* only DT status-okay children */
	const struct device ** ihcc_list;
	size_t num_ihcc;
	uint32_t irq_idx;
	int (*config_func)(void);
};

struct mchp_ihcm_data {
	/* refcounts channels with irq enabled; irq only disabled at 0 */
	uint32_t isr_counter;
	struct k_mutex *module_lock;
	ipm_callback_t *cb_list;
	void **cb_user_data_list;
	/* index i holds the remote hart id for cb_list[i]/cb_user_data_list[i] */
	uint32_t *cb_idx_list;
	size_t num_cb;
};

struct mchp_ihcc_config {
	const struct device *parent_node;
	const struct device *gparent_node;
	uintptr_t ihc_regs;
	uintptr_t ihcm_regs;
	uintptr_t ihcc_regs;
};

struct mchp_ihcc_data {
	bool enabled;
	struct k_mutex *channel_lock;
	atomic_t ack;
};

#endif /* ZEPHYR_DRIVERS_IPM_MCHP_IHC_H_ */