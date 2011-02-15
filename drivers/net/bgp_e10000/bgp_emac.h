/*
 * bgp_emac.h: XEMAC definition for BlueGene/P 10 GbE driver
 *
 * Copyright (c) 2007, 2010 International Business Machines
 * Author: Andrew Tauferner <ataufer@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 */

#ifndef _BGP_EMAC_H
#define _BGP_EMAC_H

#include <linux/netdevice.h>
#include <linux/mii.h>
#include <linux/delay.h>
#include <linux/crc32.h>
#include <linux/proc_fs.h>
#include <asm/bluegene.h>
#include <asm/bluegene_ras.h>

#include "bgp_tomal.h"
#include "bgp_e10000.h"


#define XEMAC_IRQ_GROUP 9
#define XEMAC_IRQ_GINT   0
#define XEMAC_IRQ bic_hw_to_irq(XEMAC_IRQ_GROUP, XEMAC_IRQ_GINT)

#define XEMAC_BASE_ADDRESS 0x720004000ULL



typedef volatile struct _XEMACRegs {  /*  Offset Description */
	U32 mode0;			 /*  00 mode register 0 */
#define XEMAC_MODE0_RXIDL		0x80000000
#define XEMAC_MODE0_TXIDL		0x40000000
#define XEMAC_MODE0_SRST		0x20000000
#define XEMAC_MODE0_TXEN		0x10000000
#define XEMAC_MODE0_RXEN		0x08000000
#define XEMAC_MODE0_WUEN		0x04000000
        U32 mode1;                       /*  04 mode register 1 */
#define XEMAC_MODE1_SDR                 0x80000000
#define XEMAC_MODE1_LPEN		0x40000000
#define XEMAC_MODE1_VLEN		0x20000000
#define XEMAC_MODE1_IFEN		0x10000000
#define XEMAC_MODE1_PSEN		0x08000000
#define XEMAC_MODE1_RFS2K		0x00100000
#define XEMAC_MODE1_RFS4K		0x00180000
#define XEMAC_MODE1_RFS8K		0x00200000
#define XEMAC_MODE1_RFS16K		0x00280000
#define XEMAC_MODE1_RFS32K		0x00300000
#define XEMAC_MODE1_RFS64K		0x00380000
#define XEMAC_MODE1_TFS2K		0x00020000
#define XEMAC_MODE1_TFS4K		0x00030000
#define XEMAC_MODE1_TFS8K		0x00040000
#define XEMAC_MODE1_TFS16K		0x00050000
#define XEMAC_MODE1_TFS32K		0x00060000
#define XEMAC_MODE1_TRQ			0x00008000
#define XEMAC_MODE1_JBEN		0x00000800
#define XEMAC_MODE1_OPB66MHZ		0x00000008
#define XEMAC_MODE1_OPB83MHZ		0x00000010
#define XEMAC_MODE1_OPB100MHZ		0x00000018
#define XEMAC_MODE1_OPB133MHZ		0x00000020
        U32 txMode0;                     /*  08 TX mode register 0 */
#define XEMAC_TX_MODE0_GNP		0x80000000
#define XEMAC_TX_MODE0_TFAE_2_4		0x00000001
#define XEMAC_TX_MODE0_TFAE_4_8		0x00000002
#define XEMAC_TX_MODE0_TFAE_8_16	0x00000003
#define XEMAC_TX_MODE0_TFAE_16_32	0x00000004
#define XEMAC_TX_MODE0_TFAE_32_64	0x00000005
#define XEMAC_TX_MODE0_TFAE_64_128	0x00000006
#define XEMAC_TX_MODE0_TFAE_128_256	0x00000007
        U32 txMode1;                     /*  0C TX mode register 1 */
        U32 rxMode;                      /*  10 RX mode register */
#define XEMAC_RX_MODE_SPAD		0x80000000
#define XEMAC_RX_MODE_SFCS		0x40000000
#define XEMAC_RX_MODE_ARRF		0x20000000
#define XEMAC_RX_MODE_ARFE		0x10000000
#define XEMAC_RX_MODE_LFD		0x08000000
#define XEMAC_RX_MODE_ARIE		0x04000000
#define XEMAC_RX_MODE_PPF		0x02000000
#define XEMAC_RX_MODE_PME		0x01000000
#define XEMAC_RX_MODE_PMME		0x00800000
#define XEMAC_RX_MODE_IAE		0x00400000
#define XEMAC_RX_MODE_MIAE		0x00200000
#define XEMAC_RX_MODE_BAE		0x00100000
#define XEMAC_RX_MODE_MAE		0x00080000
#define XEMAC_RX_MODE_PUME		0x00040000
#define XEMAC_RX_MODE_SIAE		0x00020000
#define XEMAC_RX_MODE_RFAF_2_4		0x00000001
#define XEMAC_RX_MODE_RFAF_4_8		0x00000002
#define XEMAC_RX_MODE_RFAF_8_16		0x00000003
#define XEMAC_RX_MODE_RFAF_16_32		0x00000004
#define XEMAC_RX_MODE_RFAF_32_64		0x00000005
#define XEMAC_RX_MODE_RFAF_64_128	0x00000006
        U32 interruptStatus;             /*  14 interrupt status register */
#define XEMAC_IS_TXPE			0x20000000
#define XEMAC_IS_RXPE			0x10000000
#define XEMAC_IS_TFEI			0x08000000
#define XEMAC_IS_RFFI			0x04000000
#define XEMAC_IS_OVR			0x02000000
#define XEMAC_IS_PSF			0x01000000
#define XEMAC_IS_BDF			0x00800000
#define XEMAC_IS_RTF			0x00400000
#define XEMAC_IS_LF			0x00200000
#define XEMAC_IS_BFCS			0x00080000
#define XEMAC_IS_FTL			0x00040000
#define XEMAC_IS_ORE			0x00020000
#define XEMAC_IS_IRE			0x00010000
#define XEMAC_IS_DB			0x00000100
#define XEMAC_IS_TE			0x00000040
#define XEMAC_IS_MMS			0x00000002
#define XEMAC_IS_MMF			0x00000001
        U32 interruptStatusEnable;       /*  18 interrupt status enable register */
        U32 individualAddrH;             /*  1C bits 0-15 of main station unique address */
        U32 individualAddrL;             /*  20 bits 16-47 of main station unique address */
        U32 vlanTPID;                    /*  24 VLAN tag ID */
        U32 vlanTCI;                     /*  28 VLAN TCI register */
        U32 pauseTimerValue;             /*  2C pause timer register */
        U32 individualAddrHashTable[4];  /*  30 individual addr. hash registers */
        U32 groupAddrHashTable[4];       /*  40 group addr. hash register 1 */
        U32 lastSourceAddrH;             /*  50 bits 0-15 of last source address */
        U32 lastSourceAddrL;             /*  54 bits 16-47 of last source address */
        U32 interPacketGapValue;         /*  58 inter packet gap register */
        U32 staCtrl;                     /*  5C STA control register */
#define XEMAC_STAC_MGO			0x00008000
#define XEMAC_STAC_PHE			0x00004000
#define XEMAC_STAC_IM			0x00002000
#define XEMAC_STAC_MII_READ		0x00001000
#define XEMAC_STAC_MII_WRITE		0x00000800
#define XEMAC_STAC_MDIO_ADDRESS		0x00002000
#define XEMAC_STAC_MDIO_WRITE		0x00002800
#define XEMAC_STAC_MDIO_READ		0x00003800
#define XEMAC_STAC_MDIO_READ_INC	0x00003000
        U32 txRequestThreshold;          /*  60 TX request threshold register */
#define XEMAC_TRT_64			0x00000000
#define XEMAC_TRT_128			0x01000000
#define XEMAC_TRT_192			0x02000000
#define XEMAC_TRT_256			0x03000000
        U32 rxLowHighWaterMark;          /*  64 RX high/low water mark register */
        U32 sopCommandMode;              /*  68 SOP command mode register */
        U32 secondaryIndividualAddrH;    /*  6C bits 0-15 of sec. individual addr. reg */
        U32 secondaryIndividualAddrL;    /*  70 bits 16-47 of sec. individual addr. reg */
        U32 txOctetsCounter1;            /*  74 bits 0-31 of total TX octets (read first) */
        U32 txOctetsCounter2;            /*  78 bits 32-63 of total TX octets (read last) */
        U32 rxOctetsCounter1;            /*  7C bits 0-31 of total RX octets (read first) */
        U32 rxOctetsCounter2;            /*  80 bits 32-63 of total RX octets (read last) */
        U32 revisionID;                  /*  84 revision ID */
        U32 hwDbg;                       /*  88 hardware debug register */
} XEMACRegs;




typedef struct _EMAC {
	U32			type;
#define EMAC_TYPE_EMAC4		4
#define EMAC_TYPE_XEMAC		10
	XEMACRegs*		regs;
	TOMAL*			tomal;
	U8			channel;
	struct mii_if_info	phy;
	struct net_device*	netDev;
	struct net_device_stats stats;
	spinlock_t		lock;
	U8			opened;
	struct proc_dir_entry* 	parentDir;
	struct proc_dir_entry* 	emacDir;
	struct proc_dir_entry* 	hwDir;

} EMAC;


typedef enum {
	emac_ras_none			= 0x00,
	emac_ras_timeout		= 0x01,
	emac_ras_ioremap_error		= 0x02,
	emac_ras_irq_not_available	= 0x03,
	emac_ras_sta_addr_error		= 0x04,
	emac_ras_sta_read_error		= 0x05,
	emac_ras_sta_write_error	= 0x06,
	emac_ras_irq_unknown		= 0x07,

	emac_ras_internal_error		= 0xfe,
	emac_ras_max			= 0xff
} emac_ras_id;

typedef enum {
	phy_ras_none			= 0x00,
	phy_ras_timeout			= 0x01,
	phy_ras_not_found		= 0x02,

	phy_ras_max			= 0xff
} phy_ras_id;


int __init emac_init(void* devMapAddr,
		     EMAC* emac,
		     U32 type,
		     TOMAL* tomal,
		     U8 channel,
		     struct net_device* netDev,
		     struct proc_dir_entry* procDir);

int emac_configure(EMAC* emac);




static inline int emac_soft_reset(EMAC* emac)
{
	int rc = 0;
	U32 i;

        PRINTK(DBG_EMAC | DBG_LEVEL2, "entry - emac=%p\n", emac);

	 /*  Set the reset bit and wait for it to clear. */
	out_be32(&emac->regs->mode0, XEMAC_MODE0_SRST);
	for (i = 200; (in_be32(&emac->regs->mode0) & XEMAC_MODE0_SRST) && i; i--)
		udelay(10000);
	if (!i) {
		e10000_printr(bg_subcomp_xemac, emac_ras_timeout,
				"XEMAC failed reset");
		rc = -ETIME;
	}

	return rc;
}



static inline int emac_rx_enable(EMAC* emac)
{
	U32 reg = in_be32(&emac->regs->mode0);

	out_be32(&emac->regs->mode0, reg | XEMAC_MODE0_RXEN);

	return 0;
}


static inline int emac_rx_disable(EMAC* emac)
{
	U32 reg = in_be32(&emac->regs->mode0);

        out_be32(&emac->regs->mode0, reg & ~XEMAC_MODE0_RXEN);

        return 0;
}


static inline int emac_tx_enable(EMAC* emac)
{
	U32 reg = in_be32(&emac->regs->mode0);

        out_be32(&emac->regs->mode0, reg | XEMAC_MODE0_TXEN);
	reg = in_be32(&emac->regs->txMode0);
	out_be32(&emac->regs->txMode0, reg | XEMAC_TX_MODE0_GNP);

        return 0;
}


static inline int emac_tx_disable(EMAC* emac)
{
        U32 reg = in_be32(&emac->regs->mode0);

        out_be32(&emac->regs->mode0, reg & ~XEMAC_MODE0_TXEN);

        return 0;
}

static inline int emac_irq_enable(EMAC* emac)
{
	out_be32(&emac->regs->interruptStatusEnable, XEMAC_IS_TXPE | XEMAC_IS_RXPE |
		 XEMAC_IS_TFEI | XEMAC_IS_RFFI | XEMAC_IS_OVR | XEMAC_IS_BDF |
		 XEMAC_IS_RTF | XEMAC_IS_LF | XEMAC_IS_BFCS | XEMAC_IS_FTL |
		 XEMAC_IS_ORE | XEMAC_IS_IRE | XEMAC_IS_DB | XEMAC_IS_TE);

	return 0;
}

static inline int emac_irq_disable(EMAC* emac)
{
	out_be32(&emac->regs->interruptStatusEnable, 0);

	return 0;
}

static inline int emac_set_mac_address(EMAC* emac)
{
        int rc = 0;

        PRINTK(DBG_EMAC | DBG_LEVEL2, "entry - emac=%p\n", emac);

        switch (emac->type) {
        	case EMAC_TYPE_XEMAC: {
                	XEMACRegs* reg = (XEMACRegs*) emac->regs;
                        struct net_device* netDev = emac->netDev;

                        out_be32(&reg->individualAddrH, netDev->dev_addr[0] << 8 |
                                 netDev->dev_addr[1]);
                        out_be32(&reg->individualAddrL, netDev->dev_addr[2] << 24 |
                                 netDev->dev_addr[3] << 16 | netDev->dev_addr[4] << 8 |
                                 netDev->dev_addr[5]);
                        break;
                }
        }

        PRINTK(DBG_EMAC | DBG_LEVEL2, "exit - rc=%d\n", rc);

        return rc;
}


static inline int emac_set_multicast_list(EMAC* emac)
{
	int rc = 0;
	XEMACRegs* regs = (XEMACRegs*) emac->regs;

	PRINTK(DBG_EMAC | DBG_LEVEL2, "entry - emac=%p\n", emac);

	if (emac->netDev->flags & IFF_MULTICAST &&
		emac->netDev->mc_count > 0) {
		U16 groupAddrHashTable[4] = {0, 0, 0, 0};
		struct dev_mc_list* dmi;

		for (dmi = emac->netDev->mc_list; dmi; dmi = dmi->next) {
			U32 crc = ether_crc(6, (char*) dmi->dmi_addr);
			U32 bit = 63 - (crc >> 26);

			groupAddrHashTable[bit >> 4] |=
				0x8000 >> (bit & 0x0f);
		}
		regs->groupAddrHashTable[0] = groupAddrHashTable[0];
                regs->groupAddrHashTable[1] = groupAddrHashTable[1];
                regs->groupAddrHashTable[2] = groupAddrHashTable[2];
                regs->groupAddrHashTable[3] = groupAddrHashTable[3];
	}

	PRINTK(DBG_EMAC | DBG_LEVEL2, "exit - rc=%d\n", rc);

	return rc;
}


void emac_exit(EMAC* emac);




#endif
