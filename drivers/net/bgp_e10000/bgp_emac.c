/*
 * bgp_emac.c: XEMAC device for BlueGene/P 10 GbE driver
 *
 * Copyright (c) 2007, 2010 International Business Machines
 * Author: Andrew Tauferner <ataufer@us.ibm.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#include "bgp_emac.h"
#include "bgp_e10000.h"


/*  XEMAC accessible through /proc/driver/e10000/xemac/hw. */
static E10000_PROC_ENTRY emac_hw_proc_entry[] = {
	{ "mode0",			(void*) 0x00,	NULL },
	{ "mode1",			(void*) 0x04,	NULL },
	{ "txMode0",			(void*) 0x08,	NULL },
	{ "txMode1",			(void*) 0x0c, 	NULL },
	{ "rxMode",			(void*) 0x10,	NULL },
	{ "interruptStatus",		(void*) 0x14,	NULL },
	{ "interruptStatusEnable",	(void*) 0x18, 	NULL },
	{ "individualAddrH",		(void*) 0x1c, 	NULL },
	{ "individualAddrL",		(void*) 0x20,	NULL },
	{ "vlanTPID",			(void*) 0x24,	NULL },
	{ "vlanTCI",			(void*) 0x28,	NULL },
	{ "pauseTimerValue",		(void*) 0x2c,	NULL },
	{ "individualAddrHashTable0",	(void*) 0x30,	NULL },
	{ "individualAddrHashTable1",   (void*) 0x34,   NULL },
	{ "individualAddrHashTable2",   (void*) 0x38,   NULL },
	{ "individualAddrHashTable3",   (void*) 0x3c,   NULL },
	{ "groupAddrHashTable0",	(void*) 0x40,	NULL },
	{ "groupAddrHashTable1",        (void*) 0x44,   NULL },
	{ "groupAddrHashTable2",        (void*) 0x48,   NULL },
	{ "groupAddrHashTable3",        (void*) 0x4c,   NULL },
	{ "lastSourceAddrH",		(void*) 0x50,	NULL },
	{ "lastSourceAddrL",		(void*) 0x54,	NULL },
	{ "interPacketGapValue",	(void*) 0x58,	NULL },
	{ "staCtrl",			(void*) 0x5c,	NULL },
	{ "txRequestThreshold",		(void*) 0x60,	NULL },
	{ "rxLowHighWaterMark",		(void*) 0x64,	NULL },
	{ "sopCommandMode",		(void*) 0x68, 	NULL },
	{ "secondaryIndividualAddrH",	(void*) 0x6c,	NULL },
	{ "secondaryIndividualAddrL",	(void*) 0x70,	NULL },
	{ "txOctetsCounter1",		(void*) 0x74,	NULL },
	{ "txOctetsCounter2",		(void*) 0x78,	NULL },
	{ "rxOctetsCounter1",		(void*) 0x7c,	NULL },
	{ "rxOctetsCounter2",		(void*) 0x80,	NULL },
	{ "revisionID",			(void*) 0x84,	NULL },
	{ "hwDebug",			(void*) 0x88,	NULL },
	{ NULL,				0,		NULL }
};



static irqreturn_t emac_irq(int irq,
			    void* data)
{
	struct net_device* netDev = (struct net_device*) data;
	EMAC* emac = (EMAC*) netdev_priv(netDev);
	U32 isr = in_be32(&emac->regs->interruptStatus);
	irqreturn_t rc = IRQ_NONE;

	if (irq == netDev->irq) {
		if ((isr & XEMAC_IS_TXPE) ||  (isr & XEMAC_IS_DB) || (isr & XEMAC_IS_TE)) {
			rc = IRQ_HANDLED;
			emac->stats.tx_errors++;
		}
		if (isr & XEMAC_IS_RXPE) {
			rc = IRQ_HANDLED;
			emac->stats.rx_errors++;
		}
		if (isr & XEMAC_IS_TFEI) {
			rc = IRQ_HANDLED;
			emac->stats.tx_errors++;
			emac->stats.tx_fifo_errors++;
		}
		if (isr & XEMAC_IS_RFFI) {
			rc = IRQ_HANDLED;
			emac->stats.rx_errors++;
			emac->stats.rx_over_errors++;
		}
		if (isr & XEMAC_IS_OVR) {
			rc = IRQ_HANDLED;
			emac->stats.rx_errors++;
			emac->stats.rx_over_errors++;
		}
		if ((isr & XEMAC_IS_PSF) || (isr & XEMAC_IS_RTF) || (isr & XEMAC_IS_IRE)) {   /*  pause or runt frame or in range error? */
			rc = IRQ_HANDLED;
		}
		if (isr & XEMAC_IS_BDF) {
			rc = IRQ_HANDLED;
			emac->stats.rx_errors++;
			emac->stats.rx_frame_errors++;
		}
		if (isr & XEMAC_IS_LF) {
			rc = IRQ_HANDLED;
			emac->stats.rx_errors++;
		}
		if (isr & XEMAC_IS_BFCS) {
			rc = IRQ_HANDLED;
			emac->stats.rx_errors++;
			emac->stats.rx_crc_errors++;
		}
		if ((isr & XEMAC_IS_FTL) || (isr & XEMAC_IS_ORE)) {
			rc = IRQ_HANDLED;
			emac->stats.rx_errors++;
			emac->stats.rx_length_errors++;
		}

		out_be32(&emac->regs->interruptStatus, isr);
	}

	if (rc != IRQ_HANDLED)
		e10000_printr(bg_subcomp_xemac, emac_ras_irq_unknown,
				"Spurious interrupt - irq=%d, isr=0x%08x.", irq, isr);

	return rc;
}

int __init emac_init(void* devMapAddr,
		     EMAC* emac,
		     U32 type,
		     TOMAL* tomal,
		     U8 channel,
		     struct net_device* netDev,
		     struct proc_dir_entry* procDir)
{
	int rc = -EINVAL;

	PRINTK(DBG_EMAC | DBG_LEVEL2, "entry - emac=%p, type=%d, tomal=%p, netDev=%p\n", emac, type,
		tomal, netDev);

	emac->type = type;
	switch (type) {
		case EMAC_TYPE_XEMAC: {
			emac->regs = (XEMACRegs*) devMapAddr;
			if (!emac->regs) {
				e10000_printr(bg_subcomp_xemac, emac_ras_ioremap_error,
						"Failure mapping XEMAC registers.");
				rc = -ENXIO;
				goto out;
			}

			 /*  Create /proc/driver/e10000/xemac/hw */
			if (procDir) {
				emac->parentDir = procDir;
				emac->emacDir = proc_mkdir("xemac", procDir);
				if (emac->emacDir) {
					emac->hwDir = proc_mkdir("hw", emac->emacDir);
					if (emac->hwDir) {
						E10000_PROC_ENTRY* entry = emac_hw_proc_entry;

						while (entry->name) {
							entry->entry = e10000_create_proc_entry(emac->hwDir, entry->name,
												(void*) ((U32) emac->regs + (U32) entry->addr));
							if (!entry->entry)
								printk(KERN_EMERG "Failure creating /proc entry %s\n", entry->name);

							entry++;
						}
					}
				}
			}
			break;
		}

		default:
			e10000_printr(bg_subcomp_xemac, e10000_ras_internal_error,
					"Invalid EMAC type [%d].", type);
			goto out;
	}

#ifndef CONFIG_BGP_E10000_EMAC_LOOPBACK
	 /*  Initialize the PHY. */
	emac->phy.phy_id = 0;
	emac->phy.full_duplex = 1;
	emac->phy.dev = netDev;
#endif

	 /*  Request IRQ. */
	rc = request_irq(netDev->irq, emac_irq, IRQF_DISABLED, "BGP EMAC IRQ", (void*) netDev);
	if (rc) {
		e10000_printr(bg_subcomp_xemac, emac_ras_irq_not_available,
				"Failure requesting IRQ [%d] - rc = %d", netDev->irq, rc);
		goto out;
	}

	emac->tomal = tomal;
	emac->channel = channel;
	emac->netDev = netDev;
	memset(&emac->stats, 0, sizeof(emac->stats));
	spin_lock_init(&emac->lock);
	emac->opened = 0;

	goto out;

out:
	PRINTK(DBG_EMAC | DBG_LEVEL2, "exit rc=%d\n", rc);

	return rc;
}


int emac_configure(EMAC* emac)
{
	int rc = 0;

	PRINTK(DBG_EMAC | DBG_LEVEL2, "entry - emac=%p\n", emac);

	switch (emac->type) {
		case EMAC_TYPE_XEMAC: {
			XEMACRegs* reg = (XEMACRegs*) emac->regs;
			U32 mode1 = XEMAC_MODE1_TRQ | XEMAC_MODE1_RFS8K |
                                    XEMAC_MODE1_TFS8K | XEMAC_MODE1_JBEN |
				    XEMAC_MODE1_PSEN | XEMAC_MODE1_IFEN |
                                    XEMAC_MODE1_OPB133MHZ | 0x00001000;
			U32 rxMode = XEMAC_RX_MODE_SPAD | XEMAC_RX_MODE_SFCS | XEMAC_RX_MODE_PMME |
				XEMAC_RX_MODE_MAE | XEMAC_RX_MODE_IAE | XEMAC_RX_MODE_BAE | XEMAC_RX_MODE_LFD |
				XEMAC_RX_MODE_RFAF_16_32;

			 /*  We must accept multicast frames so that pause frames aren't discarded. */
			 /*  This means that EMAC must have multicast mode enabled and promiscuous multicast  */
			 /*  mode enabled.  */
			if (emac->netDev->flags & IFF_PROMISC)
				rxMode |= XEMAC_RX_MODE_PME;
			out_be32(&reg->rxMode, rxMode);
			out_be32(&reg->rxLowHighWaterMark, 0x00800100);
			out_be32(&reg->pauseTimerValue, 0x1000);

#ifdef CONFIG_BGP_E10000_EMAC_LOOPBACK
			mode1 |= XEMAC_MODE1_LPEN;
#else
                        mode1 |= XEMAC_MODE1_SDR;
#endif
			out_be32(&reg->mode1, mode1);
			out_be32(&reg->txMode1, 0x02200240);
			out_be32(&reg->txRequestThreshold, 0x17000000);
			break;
		}
	}

	PRINTK(DBG_EMAC | DBG_LEVEL2, "exit - rc=%d\n", rc);

	return rc;
}

void emac_exit(EMAC* emac)
{
	PRINTK(DBG_EMAC | DBG_LEVEL2, "entry\n");

	 /*  Remove /proc entries. */
	if (emac->emacDir) {
		if (emac->hwDir) {
			E10000_PROC_ENTRY* entry = emac_hw_proc_entry;

			while (entry->name) {
				if (entry->entry) {
					remove_proc_entry(entry->entry->name, emac->emacDir);
					entry->entry = NULL;
				}
				entry++;
			}

			remove_proc_entry(emac->hwDir->name, emac->emacDir);
			emac->hwDir = NULL;
		}
		remove_proc_entry(emac->emacDir->name, emac->parentDir);
		emac->emacDir = NULL;
	}

	 /*  Free the IRQ. */
	free_irq(emac->netDev->irq, (void*) emac->netDev);

	PRINTK(DBG_EMAC | DBG_LEVEL2, "exit\n");

	return;
}
