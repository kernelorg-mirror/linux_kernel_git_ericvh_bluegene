/*
 * bgp_e10000_main.c: net_device source for BlueGene/P 10 GbE driver
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


#include <linux/module.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/if_ether.h>
#include <asm/reg_booke.h>
#include <linux/proc_fs.h>
#include <stdarg.h>
#include <asm/bluegene_ras.h>
#include <asm/bgp_personality.h>
#include <asm/bluegene.h>

#include "bgp_e10000.h"
#include "bgp_emac.h"
#include "bgp_tomal.h"


static int e10000_change_mtu(struct net_device*, int);
static int e10000_do_ioctl(struct net_device*, struct ifreq*, int);
static struct net_device_stats* e10000_get_stats(struct net_device*);
static int e10000_hard_start_xmit(struct sk_buff*, struct net_device*);
static int e10000_open(struct net_device*);
//static void e10000_set_multicast_list(struct net_device*);
static int e10000_stop(struct net_device*);
static void e10000_tx_timeout(struct net_device*);
static int e10000_set_mac_address(struct net_device* netDev, void* macAddr);
static void e10000_link_test(unsigned long);

static struct net_device* e10000NetDev;
static struct timer_list e10000LinkTimer;
static const struct net_device_ops e10000NetDevOps = {
        .ndo_open               = e10000_open,
        .ndo_stop               = e10000_stop,
        .ndo_start_xmit         = e10000_hard_start_xmit,
        .ndo_get_stats          = e10000_get_stats,
        .ndo_set_mac_address    = e10000_set_mac_address,
        .ndo_tx_timeout         = e10000_tx_timeout,
        .ndo_change_mtu         = e10000_change_mtu,
        .ndo_do_ioctl           = e10000_do_ioctl,
};

static BGP_Personality_t bgpers;
static void* e10000DevMapAddr;
static unsigned int e10000DevMapLen;

static int __init
	e10000_init(void)
{
        int rc = 0;
        TOMAL* tomal = NULL;
	EMAC* emac = NULL;
	struct proc_dir_entry* e10000Dir;

	PRINTK(DBG_E10000 | DBG_LEVEL2, "entry\n");

	 /*  Determine if Ethernet HW is present. */
	bluegene_getPersonality((void*) &bgpers, sizeof(bgpers));
	if (bgpers.Network_Config.RankInPSet) {   /*  No HW so exit. */
		rc = -ENODEV;
		goto end;
	}

	 /*  Allocate ethernet device(s). */
	e10000NetDev = alloc_etherdev(sizeof(EMAC));
        if (!e10000NetDev) {
		e10000_printr(bg_subcomp_linux, e10000_ras_netdev_alloc_failure,
			      "Failure allocating ethernet device.");
                rc = -ENOMEM;
                goto end;
        }

         /*  Create /proc directory. */
        e10000Dir = proc_mkdir("driver/e10000", NULL);

	 /*  Create mapping for TOMAL and XEMAC devices.  Since they are close in memory one mapping with */
	 /*  a small hole in between will cover both.  Tell CNS where XEMAC is mapped. */
	e10000DevMapLen = XEMAC_BASE_ADDRESS + sizeof(XEMACRegs) - TOMAL_BASE_ADDRESS;
	e10000DevMapAddr = ioremap(TOMAL_BASE_ADDRESS, e10000DevMapLen);
	if (!e10000DevMapAddr) {
		rc = -ENODEV;
		goto end;
	}
	rc = bluegene_mapXEMAC(e10000DevMapAddr+(XEMAC_BASE_ADDRESS - TOMAL_BASE_ADDRESS));
	if (rc) {
		e10000_printr(bg_subcomp_linux, 0xff, "Failure registering XEMAC mapping with CNS.");
		rc = -ENODEV;
		goto unmap_dev;
	}

         /*  Allocate and intialize TOMAL device. */
        tomal = tomal_init(e10000DevMapAddr, e10000NetDev, CONFIG_BGP_E10000_RXB, CONFIG_BGP_E10000_TXB, NULL,
			   0, 0, TOMAL_IRQ0, TOMAL_IRQ1, e10000Dir);
        if (IS_ERR(tomal)) {
                rc = (int) tomal;
                goto unmap_dev;
        }

	 /*  Initialize XEMAC. */
	e10000NetDev->irq = XEMAC_IRQ;
	emac = (EMAC*) netdev_priv(e10000NetDev);
        rc = emac_init((char*) e10000DevMapAddr + (XEMAC_BASE_ADDRESS - TOMAL_BASE_ADDRESS), emac, EMAC_TYPE_XEMAC,
			tomal, 0, e10000NetDev, e10000Dir);
        if (rc)
                goto free_tomal;

	 /*  Initialize network device operations. */
	e10000NetDev->netdev_ops = &e10000NetDevOps;

	 /*  Register the net_device. */
	rc = register_netdev(e10000NetDev);
	if (rc) {
		e10000_printr(bg_subcomp_linux, e10000_ras_netdev_reg_failure,
				"Failure registering net_device [%p].", e10000NetDev);
		goto exit_emac;
	}

         /*  Configure EMAC. */
        rc = emac_configure(emac);
        if (rc) {
		e10000_printr(bg_subcomp_e10000, e10000_ras_emac_config_error,
				"EMAC configuration error.   rc=%d", rc);
                goto exit_emac;
        }

	 /*  Initialize the timer. */
	e10000LinkTimer.function = e10000_link_test;
	e10000LinkTimer.data = (unsigned int) e10000NetDev;
	init_timer(&e10000LinkTimer);

	goto end;

exit_emac:
	emac_exit(emac);
free_tomal:
	tomal_exit(tomal);
unmap_dev:
	iounmap(e10000DevMapAddr);
	free_netdev(e10000NetDev);
end:

	PRINTK(DBG_E10000 | DBG_LEVEL2, "exit rc=0x%x\n", rc);

        return rc;
}



static int e10000_set_mac_address(struct net_device* netDev, void* macAddr)
{
	int rc = -EINVAL;
	struct sockaddr* sockAddr = (struct sockaddr*) macAddr;

	PRINTK(DBG_E10000 | DBG_LEVEL2, "entry - netDev=%p, macAddr=%p\n",
		netDev, macAddr);

	if (is_valid_ether_addr(((struct sockaddr*) macAddr)->sa_data)) {
		EMAC* emac = (EMAC*) netdev_priv(netDev);
		unsigned long flags;

		memcpy(netDev->dev_addr, sockAddr->sa_data, netDev->addr_len);

		spin_lock_irqsave(&emac->lock, flags);
		rc = emac_set_mac_address(emac);
		spin_unlock_irqrestore(&emac->lock, flags);
	} else
		rc = -EADDRNOTAVAIL;

	PRINTK(DBG_E10000 | DBG_LEVEL2, "exit - rc=%d\n", rc);

	return rc;
}



static int e10000_change_mtu(struct net_device* netDev,
			     int newMTU)
{
	int rc = 0;

	PRINTK(DBG_E10000 | DBG_LEVEL2, "entry - netDev=%p, newMTU=%d\n",
	       netDev, newMTU);

	if (newMTU < BGP_E10000_MIN_MTU || newMTU > BGP_E10000_MAX_MTU) {
		e10000_printr(bg_subcomp_e10000, e10000_ras_mtu_invalid,
				"Invalid MTU of [%d] specified. Valid MTU "
				"values are [%d,%d].\n", newMTU, BGP_E10000_MIN_MTU,
				BGP_E10000_MAX_MTU);
		rc = -EINVAL;
	} else if (netDev->mtu != newMTU && netif_running(netDev)) {
/* #ifdef CONFIG_BGP_E10000_NAPI */
/* 		netDev->weight = tomal->maxRxBuffers[channel]; */
/* #endif */
		netDev->mtu = newMTU;
	}

	PRINTK(DBG_E10000 | DBG_LEVEL2, "exit - rc=%d\n", rc);

	return rc;
}


static int e10000_do_ioctl(struct net_device* netDev,
			   struct ifreq* req,
			   int cmd)
{
	int rc = 0;

	PRINTK(DBG_E10000 | DBG_LEVEL2, "entry - netDev=%p, req=%p, cmd=0x%x\n",
	       netDev, req, cmd);

//	printk(KERN_CRIT "IOCTL not supported yet\n");

	PRINTK(DBG_E10000 | DBG_LEVEL2, "exit - rc=%d\n", rc);

	return rc;
}


static struct net_device_stats* e10000_get_stats(struct net_device* netDev)
{
	struct net_device_stats* stats = &((EMAC*) netdev_priv(netDev))->stats;

	PRINTK(DBG_E10000 | DBG_LEVEL2, "entry - netDev=%p\nexit - stats=%p\n",
		netDev, stats);

	return stats;
}
#ifdef CONFIG_BGP_E10000_DBG
int e10000_diag_count ;
/*  If the 'skb' has fragments ( is a scatter-gather one), display them all and the base element too */
static void diag_display_sk(struct sk_buff* skb)
{
	int nr_frags = skb_shinfo(skb)->nr_frags;
        if( skb->data_len >= 4096 ||
        		e10000_diag_count > 0)
        	{
        		int f ;
        		if( e10000_diag_count > 0 ) e10000_diag_count -= 1 ;
        		printk(KERN_INFO "diag_display_sk skb=%p nr_frags=%d skb->data=%p skb->len=0x%08x skb->data_len=0x%08x e10000_diag_count=%d\n",
        				skb,nr_frags,skb->data,skb->len,skb->data_len,e10000_diag_count) ;
        		for(f=0;f<nr_frags;f += 1)
        			{
        				struct skb_frag_struct* frag = &skb_shinfo(skb)->frags[f];
        				printk(KERN_INFO " frags[%d]->(page=%p, page_offset=0x%08x, size=0x%08x)\n",
        						f,frag->page,frag->page_offset,frag->size) ;
        			}
        	}
}
#endif
static int e10000_hard_start_xmit(struct sk_buff* skb,
				  struct net_device* netDev)
{
	int rc;
	unsigned long flags;
	EMAC* emac = netdev_priv(netDev);

	PRINTK(DBG_E10000 | DBG_LEVEL2, "entry - skb=%p, netDev=%p\n",
		skb, netDev);

#ifdef CONFIG_BGP_E10000_DBG
	if(DBG_SCATTERGATHER & CONFIG_BGP_E10000_DBG_LEVEL ) diag_display_sk(skb) ;
#endif

	spin_lock_irqsave(&emac->tomal->txLock[emac->channel], flags);
	rc = tomal_xmit_tx_buffer(emac->tomal, emac->channel, skb);
	if (likely(!rc)) {
		emac->stats.tx_packets++;
		emac->stats.tx_bytes += skb->len;
		rc = NETDEV_TX_OK;
		netDev->trans_start = jiffies;
	} else {
		netif_stop_queue(netDev);
		rc = NETDEV_TX_BUSY;
	}
	spin_unlock_irqrestore(&emac->tomal->txLock[emac->channel], flags);

	PRINTK(DBG_E10000 | DBG_LEVEL2, "exit - rc=%d\n", rc);

	return rc;
}



static int e10000_open(struct net_device* netDev)
{
	int rc = 0;
	EMAC* emac = (EMAC*) netdev_priv(netDev);

	PRINTK(DBG_E10000 | DBG_LEVEL2, "entry - netDev=%p\n", netDev);

	if (!emac->opened) {
		U32 linkTimer;
		U8 rxLink, txLink;
		struct sockaddr sockAddr;

                 /*  Set the MAC address for this interface. */
		memcpy(sockAddr.sa_data, bgpers.Ethernet_Config.EmacID, netDev->addr_len);
		e10000_set_mac_address(netDev, &sockAddr);

                 /*  Acquire locks for EMAC and TOMAL. */
                spin_lock(&emac->tomal->rxLock[emac->channel]);
                spin_lock(&emac->tomal->txLock[emac->channel]);
                spin_lock(&emac->lock);

		emac->opened = 1;

#ifndef CONFIG_BGP_E10000_EMAC_LOOPBACK
		 /*  Reset TOMAL */
		tomal_soft_reset(emac->tomal);

	         /*  PHY reset. */
		rc = bluegene_macResetPHY();
		if (rc) {
			e10000_printr(bg_subcomp_e10000, e10000_ras_phy_reset_error,
					"%s: PHY reset error.", netDev->name);
			spin_unlock(&emac->lock);
			spin_unlock(&emac->tomal->txLock[emac->channel]);
			spin_unlock(&emac->tomal->rxLock[emac->channel]);
			goto exit;
		}

		 /*  Wait for link to be ready.  We wait less time for a single ION so that */
		 /*  we timeout before the control system does. */
		linkTimer = 240;
		for (txLink = 0, rxLink = 0; linkTimer && (!txLink || !rxLink); linkTimer--) {
			txLink = bluegene_macTestTxLink();
			rxLink = bluegene_macTestRxLink();
			udelay(100000);
		}
		printk(KERN_NOTICE "%s: Link status [RX%c,TX%c]\n", netDev->name,
		       rxLink ? '+' : '-', txLink  ? '+' : '-');
		if (!linkTimer) {
                        e10000_printr(bg_subcomp_e10000, e10000_ras_link_error,
                                        "%s: No link detected.", netDev->name);
			spin_unlock(&emac->lock);
			spin_unlock(&emac->tomal->txLock[emac->channel]);
			spin_unlock(&emac->tomal->rxLock[emac->channel]);
                        goto exit;
		}
#endif

		 /*  Configure EMAC. */
		rc = emac_configure(emac);
		if (rc) {
			e10000_printr(bg_subcomp_e10000, e10000_ras_emac_config_error,
				      "EMAC configuration error.   rc=%d", rc);
			spin_unlock(&emac->lock);
			spin_unlock(&emac->tomal->txLock[emac->channel]);
			spin_unlock(&emac->tomal->rxLock[emac->channel]);
			goto exit;
		}

		 /*  Enable TX and RX for TOMAL and EMAC. */
		tomal_rx_tx_enable(emac->tomal);
		emac_rx_enable(emac);
		emac_tx_enable(emac);

		 /*  Enable IRQs. */
		tomal_irq_enable(emac->tomal, emac->channel);
		emac_irq_enable(emac);

		 /*  Release the locks. */
		spin_unlock(&emac->lock);
		spin_unlock(&emac->tomal->txLock[emac->channel]);
		spin_unlock(&emac->tomal->rxLock[emac->channel]);

		 /*  Start the queues. */
		netif_start_queue(netDev);

		 /*  Start link timer. */
		mod_timer(&e10000LinkTimer, jiffies + HZ);
	}
exit:
	PRINTK(DBG_E10000 | DBG_LEVEL2, "exit - rc=%d\n", rc);

	return rc;
}



static void e10000_link_test(unsigned long data)
{
	struct net_device* netDev = (struct net_device*) data;
	static unsigned int linkLossCount = 0;
	u8 txLink = bluegene_macTestTxLink();
	u8 rxLink = bluegene_macTestRxLink();

	if (!txLink || !rxLink) {
		 /*  Link gone.  Have we reached the threshold where we are going to send a fatal event? */
		if (linkLossCount == 30)
			e10000_printr(bg_subcomp_e10000, e10000_ras_link_error,
					"%s: Link error detected. Link status [RX%c,TX%c]\n", netDev->name,
                       			rxLink ? '+' : '-', txLink  ? '+' : '-');
		else if (linkLossCount == 0)
			 /*  Send non-fatal RAS when the link first disappears. */
			e10000_printr(bg_subcomp_e10000, e10000_ras_link_loss,
					"%s: Loss of link detected. Link status [RX%c,TX%c]\n", netDev->name,
                                        rxLink ? '+' : '-', txLink  ? '+' : '-');

		linkLossCount++;
	} else
		 /*  Link present.  Reset counter. */
		linkLossCount = 0;

	mod_timer(&e10000LinkTimer, jiffies + HZ);

	return;
}


//static void e10000_set_multicast_list(struct net_device* netDev)
//{
//	PRINTK(DBG_E10000 | DBG_LEVEL2, "entry - netDev=%p\n", netDev);
//
//	emac_set_multicast_list((EMAC*) netdev_priv(netDev));
//
//	PRINTK(DBG_E10000 | DBG_LEVEL2, "exit\n");
//
//	return;
//}


static int e10000_stop(struct net_device* netDev)
{
	int rc = 0;
	EMAC* emac = (EMAC*) netdev_priv(netDev);
	unsigned long tomalRxFlags;
	unsigned long tomalTxFlags;
	unsigned long emacFlags;

	PRINTK(DBG_E10000 | DBG_LEVEL2, "entry - netDev=%p\n", netDev);

         /*  Acquire locks for EMAC and TOMAL. */
        spin_lock_irqsave(&emac->tomal->rxLock[emac->channel], tomalRxFlags);
	spin_lock_irqsave(&emac->tomal->txLock[emac->channel], tomalTxFlags);
	spin_lock_irqsave(&emac->lock, emacFlags);

	local_bh_disable();
	del_timer_sync(&e10000LinkTimer);
	netif_stop_queue(netDev);

	emac->opened = 0;
	emac_rx_disable(emac);
	emac_tx_disable(emac);
	emac_irq_disable(emac);
	tomal_rx_tx_disable(emac->tomal);
	tomal_irq_disable(emac->tomal, emac->channel);

         /*  Release locks for EMAC and TOMAL. */
	spin_unlock_irqrestore(&emac->lock, emacFlags);
        spin_unlock_irqrestore(&emac->tomal->txLock[emac->channel], tomalTxFlags);
        spin_unlock_irqrestore(&emac->tomal->rxLock[emac->channel], tomalRxFlags);

        local_bh_enable();
	PRINTK(DBG_E10000 | DBG_LEVEL2, "exit - rc=%d\n", rc);

	return rc;
}


static void e10000_tx_timeout(struct net_device* netDev)
{
	EMAC* emac = (EMAC*) netdev_priv(netDev);

	PRINTK(DBG_E10000 | DBG_LEVEL2, "entry - netDev=%p\n", netDev);

	e10000_printr(bg_subcomp_e10000, e10000_ras_tx_timeout,
			"Transmission timeout at %u, elapsed time %u\n",
			(U32) jiffies, (U32)(jiffies - netDev->trans_start));
	emac->stats.tx_errors++;

	 /*  Attempt to reset the interface. */
	e10000_stop(netDev);
	e10000_open(netDev);

	PRINTK(DBG_E10000 | DBG_LEVEL2, "exit\n");

	return;
}


static void e10000_exit(void)
{
	EMAC* emac = netdev_priv(e10000NetDev);

        PRINTK(DBG_E10000 | DBG_LEVEL2, "entry\n");

	 /*  Allow the HW to clean up. */
	if (emac) {
		if (emac->tomal)
			tomal_exit(emac->tomal);
		emac_exit(emac);
	}

	 /*  Unmap HW. */
	if (e10000DevMapAddr)
		iounmap(e10000DevMapAddr);

	 /*  Unregister and free the net_device. */
	if (e10000NetDev) {
		unregister_netdev(e10000NetDev);
		free_netdev(e10000NetDev);
	}

        PRINTK(DBG_E10000 | DBG_LEVEL2, "exit\n");

        return;
}


extern int bgWriteRasStr(unsigned int component,
                          unsigned int subcomponent,
                          unsigned int errCode,
                          char*        str,
                          unsigned int strLen);

void e10000_printr(U16 subComponent,
            	   U16 id,
            	   char* format,
            	   ...)
{
        va_list args;
        int n;
        char text[BG_RAS_DATA_MAX];

        va_start(args, format);
        n = vsnprintf(text, sizeof(text)-1, format, args);
        va_end(args);
	if (n < 0)
		n = 0;

	text[n] = '\0';
	printk(KERN_WARNING "%s\n", text);
	bgWriteRasStr(bg_comp_kernel, subComponent, id, text, 0);

	return;
}


module_init(e10000_init);
module_exit(e10000_exit);



MODULE_DESCRIPTION("10Gb Ethernet Driver for BlueGene");
MODULE_VERSION("2.0");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Andrew Tauferner");

