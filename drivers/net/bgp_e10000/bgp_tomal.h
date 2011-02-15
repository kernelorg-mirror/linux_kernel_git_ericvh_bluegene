/*
 * bgp_tomal.h: Definition of TOMAL device for BlueGene/P 10 GbE driver
 *
 * Copyright (c) 2007, 2010 International Business Machines
 * Author: Andrew Tauferner <ataufer@us.ibm.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 */

#ifndef _BGP_TOMAL_H
#define _BGP_TOMAL_H

#include <asm/io.h>
#include <asm/bluegene.h>
#include <linux/skbuff.h>
#include <linux/proc_fs.h>

#include "bgp_e10000.h"

#define TOMAL_MAX_CHANNELS 2


#define TOMAL_RX_MAX_FRAME_NUM  10
#define TOMAL_RX_MAX_TIMER      50


#define TOMAL_IRQ_GROUP  8
#define TOMAL_IRQ0_GINT  0
#define TOMAL_IRQ1_GINT  1
#define TOMAL_IRQ0 bic_hw_to_irq(TOMAL_IRQ_GROUP, TOMAL_IRQ0_GINT)
#define TOMAL_IRQ1 bic_hw_to_irq(TOMAL_IRQ_GROUP, TOMAL_IRQ1_GINT)


#define TOMAL_BASE_ADDRESS 0x720000000ULL
typedef volatile struct _TOMALRegs {
	U32 configurationCtrl;                   /*  0000 configuration control */
#define TOMAL_CFG_CTRL_RX_MAC0			0x00800000
#define TOMAL_CFG_CTRL_RX_MAC1			0x00400000
#define TOMAL_CFG_CTRL_TX_MAC0			0x00200000
#define TOMAL_CFG_CTRL_TX_MAC1			0x00100000
#define TOMAL_CFG_CTRL_PLB_FREQ_250		0x00000000
#define TOMAL_CFG_CTRL_PLB_FREQ_300		0x00040000
#define TOMAL_CFG_CTRL_PLB_FREQ_350		0x00080000
#define TOMAL_CFG_CTRL_PLB_FREQ_400		0x000c0000
#define TOMAL_CFG_CTRL_PLB_M_POWER		0x00000080
#define TOMAL_CFG_CTRL_SLEEP			0x00000002
#define TOMAL_CFG_CTRL_SOFT_RESET		0x00000001
	U32 reserved1[23];                       /*  0004 */
	U32 revisionID;                          /*  0060 revision id */
	U32 reserved2[103];                      /*  0064 */
	U32 consumerMemoryBaseAddr;              /*  0200 consumer memory base address */
	U32 reserved3[127];                      /*  0204 */
	U32 packetDataEngineCtrl;                /*  0400 packet data engine control */
#define TOMAL_PDE_CTRL_RX_PREFETCH8		0x00000030
#define TOMAL_PDE_CTRL_RX_PREFETCH1		0x00000000
#define TOMAL_PDE_CTRL_TX_PREFETCH8		0x00000003
#define TOMAL_PDE_CTRL_TX_PREFETCH1		0x00000000
	U32 reserved4[127];                      /*  0404 */
	U32 txNotificationCtrl;                  /*  0600 TX notification control */
#define TOMAL_TX_NOTIFY_CTRL_COUNTER_START0	0x00000020
#define TOMAL_TX_NOTIFY_CTRL_COUNTER_START1	0x00000010
	U32 reserved5[3];                        /*  0604 */
	U32 txMinTimer;                          /*  0610 TX min timer */
	U32 reserved6[3];                        /*  0614 */
	U32 txMaxTimer;                          /*  0620 TX max timer */
	U32 reserved7[11];                       /*  0624 */
	U32 txFramePerServiceCtrl;               /*  0650 TX frame / service control */
	U32 reserved8[3];                        /*  0654 */
	U32 txHWCurrentDescriptorAddrH;          /*  0660 TX HW current desc. addr. High */
	U32 reserved9[3];                        /*  0664 */
	U32 txHWCurrentDescriptorAddrL;          /*  0670 TX HW current desc. addr. Low */
	U32 reserved10[7];                       /*  0674 */
	U32 txPendingFrameCount;                 /*  0690 TX pending frame count */
#define TOMAL_MAX_TX_PENDING_FRAMES 216
	U32 reserved11[3];                       /*  0694 */
	U32 txAddPostedFrames;                   /*  06A0 TX add posted frames */
	U32 reserved12[3];                       /*  06A4 */
	U32 txNumberOfTransmittedFrames;         /*  06B0 TX number transmitted frames */
	U32 reserved13[3];                       /*  06B4 */
        U32 txMaxFrameNum;                       /*  06C0 TX max frame number */
        U32 reserved14[3];                       /*  06C4 */
        U32 txMinFrameNum;                       /*  06D0 TX min frame number */
        U32 reserved15[3];                       /*  06D4 */
        U32 txEventStatus;                       /*  06E0 TX event status */
#define TOMAL_TX_EVENT				0x00000001
        U32 reserved16[3];                       /*  06E4 */
        U32 txEventMask;                         /*  06F0 TX event mask */
        U32 reserved17[515];                     /*  06F4 */
        U32 rxNotificationCtrl;                  /*  0F00 RX notification control */
#define TOMAL_RX_NOTIFY_CTRL_COUNTER_START0     0x00000080
#define TOMAL_RX_NOTIFY_CTRL_COUNTER_START1     0x00000040
        U32 reserved18[3];                       /*  0F04 */
        U32 rxMinTimer;                          /*  0F10 RX minimum timer */
        U32 reserved19[3];                       /*  0F14 */
        U32 rxMaxTimer;                          /*  0F20 RX maximum timer */
        U32 reserved20[63];                      /*  0F24 */
        U32 rxHWCurrentDescriptorAddrH;          /*  1020 RX HW current desc. addr. High */
        U32 reserved21[3];                       /*  1024 */
        U32 rxHWCurrentDescriptorAddrL;          /*  1030 RX HW current desc. addr. Low */
        U32 reserved22[3];                       /*  1034 */
        U32 rxAddFreeBytes;                      /*  1040 num bytes in RX buffers posted */
        U32 reserved23[3];                       /*  1044 */
        U32 rxTotalBufferSize;                   /*  1050 total size of buffers */
#define TOMAL_RX_TOTAL_BUFFER_SIZE_MAX		0x00100000
        U32 reserved24[3];                       /*  1054 */
        U32 rxNumberOfReceivedFrames;            /*  1060 total frames received */
        U32 reserved25[3];                       /*  1064 */
        U32 rxDroppedFramesCount;                /*  1070 total frames dropped */
        U32 reserved26[3];                       /*  1074 */
        U32 rxMaxFrameNum;                       /*  1080 num frames RX to interrupt */
        U32 reserved27[3];                       /*  1084 */
        U32 rxMinFrameNum;                       /*  1090 num frames RX to int w/timer */
        U32 reserved28[3];                       /*  1094 */
        U32 rxEventStatus;                       /*  10A0 RX status of */
#define TOMAL_RX_EVENT				0x00000001
        U32 reserved29[3];                       /*  10A4 */
        U32 rxEventMask;                         /*  10B0 RX event mask of */
        U32 reserved30[467];                     /*  10B4 */
        U32 swNonCriticalErrorsStatus;           /*  1800 software noncritical error status */
#define TOMAL_SW_NONCRIT_ERRORS_TPDBC		0x00000010
#define TOMAL_SW_NONCRIT_ERRORS_RTSDB		0x00000001
        U32 reserved31[3];                       /*  1804 */
        U32 swNonCriticalErrorsEnable;           /*  1810 software noncritical error enable */
        U32 reserved32[3];                       /*  1814 */
        U32 swNonCriticalErrorsMask;             /*  1820 software noncritical error mask */
        U32 reserved33[55];                      /*  1824 */
        U32 rxDataBufferFreeSpace;               /*  1900 number free entries in RX buffer */
        U32 reserved34[3];                       /*  1904 */
        U32 txDataBuffer0FreeSpace;              /*  1910 num free entries in TX buffer */
        U32 reserved35[3];                       /*  1914 */
        U32 txDataBuffer1FreeSpace;              /*  1920 num free entries in TX buffer */
        U32 reserved36[127];                     /*  1924 */
        U32 rxMACStatus;                         /*  1B20 status from MAC for RX packets */
#define TOMAL_RX_MAC_CODE_ERROR			0x00001000	 /*  XEMAC */
#define TOMAL_RX_MAC_PARITY_ERROR		0x00000400	 /*  XEMAC/EMAC4 */
#define TOMAL_RX_MAC_OVERRUN			0x00000200	 /*  XEMAC/EMAC4 */
#define TOMAL_RX_MAC_PAUSE_FRAME		0x00000100	 /*  XEMAC/EMAC4 */
#define TOMAL_RX_MAC_BAD_FRAME			0x00000080	 /*  XEMAC/EMAC4 */
#define TOMAL_RX_MAC_RUNT_FRAME			0x00000040	 /*  XEMAC/EMAC4 */
#define TOMAL_RX_MAC_SHORT_EVENT		0x00000020	 /*  EMAC4 */
#define TOMAL_RX_MAC_ALIGN_ERROR		0x00000010	 /*  EMAC4 */
#define TOMAL_RX_MAC_BAD_FCS			0x00000008	 /*  XEMAC/EMAC4 */
#define TOMAL_RX_MAC_FRAME_TOO_LONG		0x00000004	 /*  XEMAC/EMAC4 */
#define TOMAL_RX_MAC_OUT_RANGE_ERROR		0x00000002	 /*  XEMAC/EMAC4 */
#define TOMAL_RX_MAC_IN_RANGE_ERROR		0x00000001	 /*  XEMAC/EMAC4 */
#define TOMAL_RX_MAC_XEMAC_MASK (TOMAL_RX_MAC_CODE_ERROR | \
	TOMAL_RX_MAC_PARITY_ERROR | TOMAL_RX_MAC_OVERRUN | \
	TOMAL_RX_MAC_PAUSE_FRAME | TOMAL_RX_MAC_BAD_FRAME | \
	TOMAL_RX_MAC_RUNT_FRAME | TOMAL_RX_MAC_BAD_FCS | \
	TOMAL_RX_MAC_FRAME_TOO_LONG | TOMAL_RX_MAC_OUT_RANGE_ERROR | \
	TOMAL_RX_MAC_IN_RANGE_ERROR)
        U32 reserved37[3];                       /*  1B24 */
        U32 rxMACStatusEnable;                   /*  1B30 enable bits in rxMACStatus */
        U32 reserved38[3];                       /*  1B34 */
        U32 rxMACStatusMask;                     /*  1B40 mask bits in rxMACStatus */
        U32 reserved39[3];                       /*  1B44 */
        U32 txMACStatus;                         /*  1B50 status from MAC for TX packets */
#define TOMAL_TX_MAC_LOCAL_FAULT		0x00001000	 /*  XEMAC */
#define TOMAL_TX_MAC_REMOTE_FAULT	0x00000800	 /*  XEMAC */
#define TOMAL_TX_MAC_BAD_FCS		0x00000200	 /*  EMAC4 */
#define TOMAL_TX_MAC_PARITY_ERROR	0x00000100	 /*  XEMAC */
#define TOMAL_TX_MAC_LOST_CARRIER	0x00000080	 /*  EMAC4 */
#define TOMAL_TX_MAC_EXCESSIVE_DEFERRAL	0x00000040	 /*  EMAC4 */
#define TOMAL_TX_MAC_EXCESSIVE_COLLISION	0x00000020	 /*  EMAC4 */
#define TOMAL_TX_MAC_LATE_COLLISION	0x00000010	 /*  EMAC4 */
#define TOMAL_TX_MAC_UNDERRUN		0x00000002	 /*  XEMAC/EMAC4 */
#define TOMAL_TX_MAC_SQE			0x00000001	 /*  EMAC4 */
#define TOMAL_TX_MAC_XEMAC_MASK (TOMAL_TX_MAC_LOCAL_FAULT | \
	TOMAL_TX_MAC_REMOTE_FAULT | TOMAL_TX_MAC_PARITY_ERROR | \
	TOMAL_TX_MAC_UNDERRUN)
        U32 reserved40[3];                       /*  1B54 */
        U32 txMACStatusEnable;                   /*  1B60 enable bits in txMACStatus */
        U32 reserved41[3];                       /*  1B64 */
        U32 txMACStatusMask;                     /*  1B70 mask bits in txMACStatus */
        U32 reserved42[163];                     /*  1B74 */
        U32 hwErrorsStatus;                      /*  1E00 hardware error status */
#define TOMAL_HW_ERRORS_IRAPE			0x00000008
#define TOMAL_HW_ERRORS_ORAPE			0x00000004
#define TOMAL_HW_ERRORS_IDBPE			0x00000002
#define TOMAL_HW_ERRORS_ODBPE			0x00000001
        U32 reserved43[3];                       /*  1E04 */
        U32 hwErrorsEnable;                      /*  1E10 enable bits in hwErrorsStatus */
        U32 reserved44[3];                       /*  1E14 */
        U32 hwErrorsMask;                        /*  1E20 mask bits in hwErrorsStatus */
        U32 reserved45[55];                      /*  1E24 */
        U32 swCriticalErrorsStatus;              /*  1F00 software critical error status */
#define TOMAL_SW_CRIT_ERRORS_TDBC		0x00000002
#define TOMAL_SW_CRIT_ERRORS_RDBC		0x00000001
        U32 reserved46[3];                       /*  1F04 */
        U32 swCriticalErrorsEnable;              /*  1F10 enable bits in swCriticalErrorsStatus */
        U32 reserved47[3];                       /*  1F14 */
        U32 swCriticalErrorsMask;                /*  1F20 mask bits in swCriticalErrorsStatus */
        U32 reserved48[3];                       /*  1F24 */
        U32 rxDescriptorBadCodeFEC;              /*  1F30 RX channel w/bad code descriptor */
        U32 reserved49[3];                       /*  1F34 */
        U32 txDescriptorBadCodeFEC;              /*  1F40 TX channel w/bad code descriptor */
        U32 reserved50[15];                      /*  1F44 */
        U32 interruptStatus;                     /*  1F80 interrupt status register */
#define TOMAL_INTERRUPT_TX1                     0x00020000
#define TOMAL_INTERRUPT_TX0                     0x00010000
#define TOMAL_INTERRUPT_RX1                     0x00000200
#define TOMAL_INTERRUPT_RX0                     0x00000100
#define TOMAL_INTERRUPT_TX_MAC_ERROR1           0x00000080
#define TOMAL_INTERRUPT_TX_MAC_ERROR0           0x00000040
#define TOMAL_INTERRUPT_RX_MAC_ERROR1           0x00000020
#define TOMAL_INTERRUPT_RX_MAC_ERROR0           0x00000010
#define TOMAL_INTERRUPT_PLB_PARITY_ERROR        0x00000008
#define TOMAL_INTERRUPT_SW_NONCRITICAL_ERROR1   0x00000004
#define TOMAL_INTERRUPT_SW_NONCRITICAL_ERROR0   0x00000002
#define TOMAL_INTERRUPT_CRITICAL_ERROR          0x00000001
#define TOMAL_IRQ0_MASK (TOMAL_INTERRUPT_TX0 | TOMAL_INTERRUPT_RX0 | \
	TOMAL_INTERRUPT_TX_MAC_ERROR0 | TOMAL_INTERRUPT_RX_MAC_ERROR0 | \
	TOMAL_INTERRUPT_PLB_PARITY_ERROR | TOMAL_INTERRUPT_SW_NONCRITICAL_ERROR0 | \
	TOMAL_INTERRUPT_CRITICAL_ERROR)
#define TOMAL_IRQ1_MASK (TOMAL_INTERRUPT_TX1 | TOMAL_INTERRUPT_RX1 | \
	TOMAL_INTERRUPT_TX_MAC_ERROR1 |  TOMAL_INTERRUPT_RX_MAC_ERROR1 | \
	TOMAL_INTERRUPT_SW_NONCRITICAL_ERROR1)
        U32 reserved51[3];			 /*  1F84 */
        U32 interruptRoute;                      /*  1F90 interrupt line routing */
        U32 reserved52[51];                      /*  1F94 */
        U32 rxMACBadStatusCounter;               /*  2060 num frames with errors in MAC */
        U32 reserved53[999];                     /*  2064 */
        U32 debugVectorsCtrl;                    /*  3000 */
        U32 reserved54[3];                       /*  3004 */
        U32 debugVectorsReadData;                /*  3010 */
} TOMALRegs;

typedef volatile struct _RxDesc {
        U16 code;
#define TOMAL_RX_DESC_CODE 0x6000
        U16 postedLength;
        U16 status;
#define TOMAL_RX_LAST                                   0x8000
#define TOMAL_RX_STATUS_ENCODE_MASK                     0x03f0
#define TOMAL_RX_STATUS_TCP_UDP_CHECKSUM_PASSED         0x0008
#define TOMAL_RX_STATUS_IP_CHECKSUM_PASSED              0x0004
#define TOMAL_RX_STATUS_CHECKSUM_VALID                  0x0002
        U16 totalFrameLength;
        U16 reserved;
        U16 buffHeadAddrH;       /*  bits 16-31 of data buffer address */
        U32 buffHeadAddrL;       /*  bits 32-63 of data buffer address */
} RxDesc;


typedef volatile struct _TxDesc {
        U8  code;
#define TOMAL_TX_DESC_CODE              0x60
#define TOMAL_TX_SIGNAL                 0x04
#define TOMAL_TX_NOTIFY_REQ             0x02
#define TOMAL_TX_LAST                   0x01
        U8  command;
#define TOMAL_TX_ENABLE_HW_CHECKSUM     0x40
#define TOMAL_TX_GENERATE_FCS           0x20
#define TOMAL_TX_GENERATE_PAD           0x30  /*  GENERATE_FCS must also be set */
#define TOMAL_TX_INSERT_SOURCE_ADDR     0x08
#define TOMAL_TX_REPLACE_SOURCE_ADDR    0x04
#define TOMAL_TX_INSERT_VLAN_TAG        0x02
#define TOMAL_TX_REPLACE_VLAN_TAG       0x01
        U16 postedLength;
        U32  wBStatus;
#define TOMAL_TX_STATUS_GOOD            0x00010000
        U16 reserved;
        U16 buffHeadAddrH;       /*  bits 16-31 of data buffer address */
        U32 buffHeadAddrL;       /*  bits 32-63 of data buffer address */
} TxDesc;


typedef volatile struct _BranchDesc {
        U64  code;
#define TOMAL_BRANCH_CODE       0x2000000000000000ULL
        U16 reserved;
        U16 nextDescAddrH;       /*  bits 16-31 of next descriptor address */
        U32 nextDescAddrL;       /*  bits 32-63 of next descriptor address (16 byte aligned) */
} BranchDesc;



typedef struct _RxDescSegment {
        RxDesc* desc;
	RxDesc* currDesc;
	struct sk_buff** skb;
	struct sk_buff** currSkb;
        dma_addr_t dmaHandle;
        size_t size;
        BranchDesc* branchDesc;
        struct _RxDescSegment* next;
} RxDescSegment;


typedef struct _TxDescSegment {
        TxDesc* desc;
	U32 oldIndex;
	U32 freeIndex;
	struct sk_buff** skb;
        dma_addr_t dmaHandle;
        size_t size;
        BranchDesc* branchDesc;
        struct _TxDescSegment* next;
} TxDescSegment;


typedef struct _TOMAL {
	 /*  Mapping of TOMAL's HW registers. */
        TOMALRegs* regs[TOMAL_MAX_CHANNELS];

	 /*  RX buffers, descriptors, and other data. */
        U32 maxRxBuffers[TOMAL_MAX_CHANNELS];
        U16 rxBufferSize[TOMAL_MAX_CHANNELS];
        RxDescSegment* rxDescSegment[TOMAL_MAX_CHANNELS];
        RxDescSegment* oldRxSegment[TOMAL_MAX_CHANNELS];  /*  oldest non-served RX desc segment */

	 /*  TX descriptors and other data. */
        U32 maxTxBuffers[TOMAL_MAX_CHANNELS];
	U32 pendingTxBuffers[TOMAL_MAX_CHANNELS];
	U32 numberOfTransmittedFrames[TOMAL_MAX_CHANNELS];
	U32 numberOfReceivedFrames[TOMAL_MAX_CHANNELS];
        TxDescSegment* txDescSegment[TOMAL_MAX_CHANNELS];
        TxDescSegment* oldTxSegment[TOMAL_MAX_CHANNELS]; /*  oldest non-served TX desc segment */
        TxDescSegment* freeTxSegment[TOMAL_MAX_CHANNELS];  /*  next free TX descriptor segment */

	struct net_device* netDev[TOMAL_MAX_CHANNELS];
	spinlock_t rxLock[TOMAL_MAX_CHANNELS];
	spinlock_t txLock[TOMAL_MAX_CHANNELS];
	struct napi_struct napi[TOMAL_MAX_CHANNELS] ; /* 2.6.27-ism for NAPI poll */
	int irq0;
	int irq1;
	int count_tx_checksum_type[4] ;
	struct proc_dir_entry* parentDir;
	struct proc_dir_entry* tomalDir;
	struct proc_dir_entry* hwDir;
	struct proc_dir_entry* swDir;
	U32 numberOfNetrxDrops ;
	U32 numberOfHwDrops0 ;
	U32 numberOfHwDrops1 ;
	U32 numberOfNotLast ;

} TOMAL;



typedef enum {
	tomal_ras_none 			= 0x00,
	tomal_ras_timeout		= 0x01,
	tomal_ras_alloc_error		= 0x02,
	tomal_ras_spurious_irq		= 0x03,
	tomal_ras_unknown_critical_int	= 0x04,
	tomal_ras_unknown_noncrit_int	= 0x05,
	tomal_ras_ioremap_error		= 0x06,
	tomal_ras_irq_unavailable	= 0x07,

	tomal_ras_max			= 0xff
} tomal_ras_id;


TOMAL* __init tomal_init(void* devMapAddr,
			struct net_device* netDev0,
			U32 rxTotalBufferSize0,
			U32 numTxBuffers0,
			struct net_device* netDev1,
			U32 rxTotalBufferSize1,
			U32 numTxBuffers1,
			int irq0,
			int irq1,
			struct proc_dir_entry* procDir);

int tomal_xmit_tx_buffer(TOMAL* tomal, U8 channel, struct sk_buff* skb);
int tomal_alloc_rx_buffers(TOMAL* tomal, U8 channel);
int tomal_free_rx_buffers(TOMAL* tomal, U8 channel);
#if defined(CONFIG_BGP_E10000_NAPI)
int tomal_poll_napi(struct napi_struct * napi, int budget);
#else
int tomal_poll(struct net_device *netDev, int budget);
#endif
int tomal_process_tx_buffers(TOMAL* tomal, U8 channel, U32 txNumTransmitDesc);
void tomal_free_rx_segments(TOMAL* tomal, U8 channel);
void tomal_free_tx_segments(TOMAL* tomal, U8 channel);
void tomal_free_tx_buffers(TOMAL* tomal, U8 channel);
int tomal_alloc_rx_segments(TOMAL* tomal, U8 channel, U32 numDescriptors);
int tomal_alloc_tx_segments(TOMAL* tomal, U8 channel, U32 numDescriptors);

int tomal_soft_reset(TOMAL* tomal);
int tomal_configure(TOMAL* tomal);


/*  Turns all RX & TX channels off. */
static inline void tomal_rx_tx_disable(TOMAL* tomal)
{
	U32 ccr = in_be32(&tomal->regs[0]->configurationCtrl);

	ccr &= ~(TOMAL_CFG_CTRL_RX_MAC0 | TOMAL_CFG_CTRL_RX_MAC1 | TOMAL_CFG_CTRL_TX_MAC0 |
                  TOMAL_CFG_CTRL_TX_MAC1);
        out_be32(&tomal->regs[0]->configurationCtrl, ccr);

	return;
}


/*  Turns all RX & TX channels on. */
static inline void tomal_rx_tx_enable(TOMAL* tomal)
{
	out_be32(&tomal->regs[0]->configurationCtrl, TOMAL_CFG_CTRL_RX_MAC0 |
		 TOMAL_CFG_CTRL_RX_MAC1 | TOMAL_CFG_CTRL_TX_MAC0 | TOMAL_CFG_CTRL_TX_MAC1);

	return;
}

void tomal_irq_enable(TOMAL* tomal, U8 channel);


void tomal_irq_disable(TOMAL* tomal, U8 channel);


int tomal_pending_rx_buffers(TOMAL* tomal, U8 channel);
int tomal_pending_tx_buffers(TOMAL* tomal, U8 channel);

void tomal_exit(TOMAL* tomal);


#endif
