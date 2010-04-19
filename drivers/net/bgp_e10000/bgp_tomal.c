/*
 * bgp_tomal.c: TOMAL device for BlueGene/P 10 GbE driver
 *
 * Copyright (c) 2007, 2010 International Business Machines
 * Author: Andrew Tauferner <ataufer@us.ibm.com>
 *
 * This program is free software; you can redistribute  it and/or modify i
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */



#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/dma-mapping.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/in.h>
#include <linux/tcp.h>
#include <linux/udp.h>


#include <asm/bluegene_ras.h>
#include <asm/udbg.h>

#include "bgp_e10000.h"
#include "bgp_tomal.h"
#include "bgp_emac.h"


static RxDescSegment* tomal_alloc_rx_segment(U32 numDescriptors);
static void tomal_free_rx_segment(RxDescSegment* segment);
static TxDescSegment* tomal_alloc_tx_segment(U32 numDescriptors);
static void tomal_free_tx_segment(TxDescSegment* segment);
static irqreturn_t tomal_irq0(int irq, void* data);
static irqreturn_t tomal_irq1(int irq, void* data);


/*  TOMAL hardware accessible through /proc/driver/e10000/tomal/hw */
static E10000_PROC_ENTRY tomal_hw_proc_entry[] = {
        { "configurationCtrl",			(void*) 0x0000, NULL },
	{ "revisionID",				(void*) 0x0060, NULL },
	{ "packetDataEngineCtrl",		(void*) 0x0400, NULL },
	{ "txNotificationCtrl",			(void*) 0x0600, NULL },
	{ "txMinTimer",				(void*) 0x0610, NULL },
	{ "txMaxTimer", 			(void*) 0x0620, NULL },
	{ "txMaxFrameNum0",			(void*) 0x06c0, NULL },
	{ "txMaxFrameNum1",			(void*) 0x07c0, NULL },
	{ "txMinFrameNum0",			(void*) 0x06d0, NULL },
	{ "txMinFrameNum1",                     (void*) 0x07d0, NULL },
	{ "txFramePerServiceCtrl",		(void*) 0x0650, NULL },
	{ "txHWCurrentDescriptorAddrH0",	(void*) 0x0660, NULL },
	{ "txHWCurrentDescriptorAddrH1",        (void*) 0x0760, NULL },
	{ "txHWCurrentDescriptorAddrL0",	(void*) 0x0670, NULL },
	{ "txHWCurrentDescriptorAddrL1",	(void*) 0x0770, NULL },
	{ "txPendingFrameCount0",		(void*) 0x0690, NULL },
	{ "txPendingFrameCount1", 		(void*) 0x0790, NULL },
	{ "txAddPostedFrames0",			(void*) 0x06a0, NULL },
	{ "txAddPostedFrames1",			(void*) 0x07a0, NULL },
	{ "txNumberOfTransmittedFrames0",	(void*) 0x06b0, NULL },
	{ "txNumberOfTransmittedFrames1",       (void*) 0x07b0, NULL },
	{ "txEventStatus0",			(void*) 0x06e0, NULL },
	{ "txEventStatus1", 			(void*) 0x07e0, NULL },
	{ "txEventMask0",			(void*) 0x06f0, NULL },
	{ "txEventMask1", 			(void*) 0x07f0, NULL },
	{ "rxNotificationCtrl",			(void*) 0x0f00, NULL },
	{ "rxMinTimer",				(void*) 0x0f10, NULL },
	{ "rxMaxTimer",				(void*) 0x0f20, NULL },
	{ "rxMaxFrameNum0",			(void*) 0x1080, NULL },
	{ "rxMaxFrameNum1", 			(void*) 0x1180, NULL },
	{ "rxMinFrameNum0",			(void*) 0x1090, NULL },
	{ "rxMinFrameNum1",                     (void*) 0x1190, NULL },
	{ "rxHWCurrentDescriptorAddrH0",	(void*) 0x1020, NULL },
	{ "rxHWCurrentDescriptorAddrH1",        (void*) 0x1120, NULL },
	{ "rxHWCurrentDescriptorAddrL0",        (void*) 0x1030, NULL },
	{ "rxHWCurrentDescriptorAddrL1",        (void*) 0x1130, NULL },
	{ "rxAddFreeBytes0",			(void*) 0x1040, NULL },
	{ "rxAddFreeBytes1",                    (void*) 0x1140, NULL },
	{ "rxTotalBuffersSize0",		(void*) 0x1050, NULL },
	{ "rxTotalBuffersSize1",                (void*) 0x1150, NULL },
	{ "rxNumberOfReceivedFrames0",		(void*) 0x1060, NULL },
	{ "rxNumberOfReceivedFrames1",          (void*) 0x1160, NULL },
	{ "rxDroppedFramesCount0",		(void*) 0x1070, NULL },
	{ "rxDroppedFramesCount1",              (void*) 0x1170, NULL },
	{ "rxEventStatus0", 			(void*) 0x10a0, NULL },
	{ "rxEventStatus1",                     (void*) 0x11a0, NULL },
	{ "rxEventMask0", 			(void*) 0x10b0, NULL },
	{ "rxEventMask1",                       (void*) 0x11b0, NULL },
	{ "softwareNonCriticalErrorsStatus0",	(void*) 0x1800, NULL },
	{ "softwareNonCriticalErrorsStatus1",   (void*) 0x1900, NULL },
	{ "softwareNonCriticalErrorsEnable0",	(void*) 0x1810, NULL },
	{ "softwareNonCriticalErrorsEnable1",   (void*) 0x1910, NULL },
	{ "softwareNonCriticalErrorsMask0",	(void*) 0x1820, NULL },
	{ "softwareNonCriticalErrorsMask1",     (void*) 0x1920, NULL },
	{ "receiveDataBufferSpace",		(void*) 0x1900, NULL },
	{ "transmitDataBuffer0FreeSpace",	(void*) 0x1910, NULL },
	{ "transmitDataBuffer1FreeSpace",	(void*) 0x1920, NULL },
	{ "rxMACStatus0",			(void*) 0x1b20, NULL },
	{ "rxMACStatus1",			(void*) 0x1c20, NULL },
	{ "rxMACStatusEnable0",			(void*) 0x1b30, NULL },
	{ "rxMACStatusEnable1",                 (void*) 0x1c30, NULL },
	{ "rxMACStatusMask0", 			(void*) 0x1b40, NULL },
	{ "rxMACStatusMask1",                   (void*) 0x1c40, NULL },
	{ "txMACStatus0",			(void*) 0x1b50, NULL },
	{ "txMACStatus1",                       (void*) 0x1c50, NULL },
	{ "txMACStatusEnable0",			(void*) 0x1b60, NULL },
	{ "txMACStatusEnable1",                 (void*) 0x1c60, NULL },
	{ "txMACStatusMask0",			(void*) 0x1b70, NULL },
	{ "txMACStatusMask1",                   (void*) 0x1c70, NULL },
	{ "hardwareErrorsStatus",		(void*) 0x1e00, NULL },
	{ "hardwareErrorsEnable",		(void*) 0x1e10, NULL },
	{ "hardwareErrorsMask",			(void*) 0x1e20, NULL },
	{ "softwareCriticalErrorsStatus",	(void*) 0x1f00, NULL },
	{ "softwareCriticalErrorsEnable",       (void*) 0x1f10, NULL },
	{ "softwareCriticalErrorsMask",       	(void*) 0x1f20, NULL },
	{ "receiveDescriptorBadCodeFEC",	(void*) 0x1f30, NULL },
	{ "transmitDescriptorBadCodeFEC", 	(void*) 0x1f40, NULL },
	{ "interruptStatus",			(void*) 0x1f80, NULL },
	{ "interruptRoute",			(void*) 0x1f90, NULL },
	{ "rxMACBadStatusCounter0",		(void*) 0x2060, NULL },
	{ "rxMACBadStatusCounter1",             (void*) 0x2160, NULL },
	{ "debugVectorsCtrl",			(void*) 0x3000, NULL },
	{ "debugVectorsReadData",		(void*) 0x3010, NULL },
	{ NULL,					(void*) 0, 	NULL }
};


/*  TOMAL software accessible through /proc/driver/e10000/tomal/sw */
static E10000_PROC_ENTRY tomal_sw_proc_entry[] = {
        { "rxMaxBuffers0",			NULL, 		NULL },
	{ "rxMaxBuffers1",                      NULL, 		NULL },
	{ "rxBufferSize0",			NULL,		NULL },
	{ "rxBufferSize1",			NULL,		NULL },
	{ "rxDescSegmentAddr0",			NULL,		NULL },
	{ "rxDescSegmentAddr1",                 NULL,           NULL },
	{ "rxOldDescSegmentAddr0",		NULL,		NULL },
	{ "rxOldDescSegmentAddr1",              NULL,           NULL },
	{ "txMaxBuffers0",			NULL,		NULL },
	{ "txMaxBuffers1",                      NULL,           NULL },
	{ "txPendingBuffers0",			NULL,		NULL },
	{ "txPendingBuffers1",                  NULL,           NULL },
	{ "txNumberOfTransmittedFrames0",	NULL,		NULL },
	{ "txNumberOfTransmittedFrames1",       NULL,           NULL },
	{ "txDescSegmentAddr0",			NULL,		NULL },
	{ "txDescSegmentAddr1",                 NULL,           NULL },
	{ "txOldDescSegmentAddr0",		NULL,		NULL },
	{ "txOldDescSegmentAddr1",              NULL,           NULL },
	{ "txFreeDescSegmentAddr0", 		NULL, 		NULL },
	{ "txFreeDescSegmentAddr1",             NULL,           NULL },
	{ "irq0",				NULL,		NULL },
	{ "irq1",                               NULL,           NULL },
	{ "numberOfNetrxDrops",                 NULL,           NULL },
	{ "numberOfHwDrops0",                   NULL,           NULL },
	{ "numberOfHwDrops1",                   NULL,           NULL },
	{ "numberOfNotLast",                    NULL,           NULL },
/* 	{ "txChecksumNONE",                     NULL,           NULL }, */
/* 	{ "txChecksumPARTIAL",                  NULL,           NULL }, */
/* 	{ "txChecksumUNNECESSARY",              NULL,           NULL }, */
/* 	{ "txChecksumCOMPLETE",                 NULL,           NULL }, */
	{ NULL,					NULL,		NULL }
};


/*  Allocate a single Rx descriptor segment with the specified number of descriptors. */
static RxDescSegment* tomal_alloc_rx_segment(U32 numDescriptors)
{
        RxDescSegment* segment = NULL;
        RxDesc* desc;
        size_t size = numDescriptors * sizeof(RxDesc) + sizeof(BranchDesc);
        dma_addr_t dmaHandle;

         /*  Allocate descriptor storage. */
        desc = (RxDesc*) dma_alloc_coherent(NULL, size, &dmaHandle, GFP_KERNEL);
        if (desc) {
		 /*  Clear the descriptors. */
		memset((void*) desc, 0, size);

                 /*  Allocate a segment. */
                segment = kmalloc(sizeof(RxDescSegment), GFP_KERNEL);
                if (segment) {
                        segment->size = size;
                        segment->dmaHandle = dmaHandle;
                        segment->desc = desc;

                        segment->branchDesc = (BranchDesc*) &desc[numDescriptors];
                        segment->branchDesc->code = TOMAL_BRANCH_CODE;
			segment->branchDesc->reserved = segment->branchDesc->nextDescAddrH = 0;
			segment->branchDesc->nextDescAddrL = (U32) NULL;

			 /*  Allocate storage for buffer pointers. */
			segment->skb = (struct sk_buff**)
				kmalloc(numDescriptors * sizeof(struct sk_buff*) +
					sizeof(struct sk_buff*), GFP_KERNEL);
			if (!segment->skb) {
				kfree((void*) segment);
				segment = NULL;
				dma_free_coherent(NULL, size, (void*) desc, dmaHandle);
			} else {
				memset((void*) segment->skb, 0,
					numDescriptors * sizeof(struct sk_buff*) + sizeof(struct sk_buff*));
				segment->currDesc = segment->desc;
				segment->currSkb = segment->skb;
				segment->next = segment;
			}
                } else
                        dma_free_coherent(NULL, size, (void*) desc, dmaHandle);
        }

        return segment;
}


/*  Allocate descriptor segment(s) until the specified number of Rx descriptors have been */
/*  created. */
int  tomal_alloc_rx_segments(TOMAL* tomal,
			     U8 channel,
			       U32 totalDescriptors)
{
	RxDescSegment* firstSegment = (RxDescSegment*) NULL;
	RxDescSegment* prevSegment = (RxDescSegment*) NULL;
	RxDescSegment* segment = (RxDescSegment*) NULL;
	U32 numDescriptors = totalDescriptors;
	U8 first = 1;
	int rc;

	 /*  Allocate RX segments until the indicated number of descriptors have been */
	 /*  created. */
	while (totalDescriptors && numDescriptors >= 1) {
		 /*  Allocate an RX descriptor segment. */
		segment = tomal_alloc_rx_segment(numDescriptors);
		if (segment) {
			 /*  If this was the first segment then remember it. */
			if (first) {
				firstSegment = prevSegment = segment;
				first = 0;
			}

			 /*  Link the previous segment to the new segment. */
			prevSegment->branchDesc->nextDescAddrL = (U32) segment->dmaHandle;
			prevSegment->next = segment;

			totalDescriptors -= numDescriptors;
		} else {
			 /*  Failure allocating a segment of the requested size.  Reduce the size. */
			numDescriptors /= 2;
		}
	}

	 /*  All segments created? */
	if (!segment) {
		RxDescSegment* nextSegment = NULL;

		 /*  Free any segments that were allocated. */
		segment = prevSegment = firstSegment;
		while (segment) {
			nextSegment = segment->next;
			BUG_ON(nextSegment == segment);

			tomal_free_rx_segment(segment);

			segment = nextSegment;
		}
		tomal->rxDescSegment[channel] = (RxDescSegment*) NULL;

		e10000_printr(bg_subcomp_tomal, tomal_ras_alloc_error,
				"Failure allocating RX descriptor segment - totalDescriptors=%d.",
				totalDescriptors);
		rc = -ENOMEM;
	} else {
		 /*  Link the last segment to the first. */
		segment->branchDesc->nextDescAddrL = (U32) firstSegment->dmaHandle;
		segment->next = firstSegment;

		tomal->rxDescSegment[channel] = segment;
		rc = 0;
	}

	 /*  Update TOMAL's view of the RX descriptors. */
	out_be32(&tomal->regs[channel]->rxHWCurrentDescriptorAddrH, 0);
	out_be32(&tomal->regs[channel]->rxHWCurrentDescriptorAddrL,
		 (U32) tomal->rxDescSegment[channel]->dmaHandle);

	tomal->oldRxSegment[channel] = tomal->rxDescSegment[channel];
	tomal->oldRxSegment[channel]->currDesc = tomal->oldRxSegment[channel]->desc;
	tomal->oldRxSegment[channel]->currSkb = tomal->oldRxSegment[channel]->skb;

	return rc;
}


/*  Free the specified Rx descriptor segment. */
static void tomal_free_rx_segment(RxDescSegment* segment)
{
	RxDesc* desc;
	struct sk_buff** skb;

	 /*  Look for any descriptors awaiting processing. */
	for (desc = segment->desc, skb = segment->skb;
	     desc && desc != (RxDesc*) segment->branchDesc; desc++, skb++) {
		if (*skb) {
			dma_unmap_single(NULL, desc->buffHeadAddrL,
					 desc->postedLength, DMA_FROM_DEVICE);
			dev_kfree_skb_any(*skb);
			*skb = NULL;
		}

		desc->postedLength = 0;
	}

	 /*  Free SKB pointer storage. */
	if (segment->skb)
		kfree(segment->skb);

	 /*  Free the descriptor storage. */
	if (segment->desc)
		dma_free_coherent(NULL, segment->size, (void*) segment->desc, segment->dmaHandle);

	 /*  Free the segment. */
	kfree((void*) segment);

	return;
}


/*  Free all Rx descriptor segments. */
void tomal_free_rx_segments(TOMAL* tomal,
			    U8 channel)
{
	RxDescSegment* segment = tomal->rxDescSegment[channel];
	RxDescSegment* startSegment = segment;
	RxDescSegment* nextSegment;

	PRINTK(DBG_TOMAL | DBG_LEVEL2, "entry - tomal=%p, channel=%d\n", tomal, channel);

	while (segment) {
		nextSegment = segment->next;

		tomal_free_rx_segment(segment);
		segment = nextSegment;

		if (segment == startSegment)
			break;
	}
	tomal->rxDescSegment[channel] = NULL;

	PRINTK(DBG_TOMAL | DBG_LEVEL2, "exit\n");

	return;
}


/*  Free all Rx buffers. */
int tomal_free_rx_buffers(TOMAL* tomal,
			   U8 channel)
{
	int rc = 0;
	RxDescSegment* segment = tomal->rxDescSegment[channel];
	RxDescSegment* startSegment = segment;
	RxDesc* desc;
	struct sk_buff** skb;

        PRINTK(DBG_TOMAL | DBG_LEVEL2, "entry - tomal=%p, channel=%d\n", tomal, channel);

	while (segment) {
		 /*  Look for any descriptors awaiting processing. */
                for (desc = segment->desc, skb = segment->skb;
			desc != (RxDesc*) segment->branchDesc; desc++, skb++) {
			if (*skb) {
				dma_unmap_single(NULL, desc->buffHeadAddrL,
						   desc->postedLength, DMA_FROM_DEVICE);
                                dev_kfree_skb_any(*skb);
                                *skb = NULL;
			}

			desc->postedLength = 0;
		}

		segment = segment->next;
		if (segment == startSegment)
			break;
        }

	 /*  Force TOMAL's total buffer size register back to zero.  We do this by adding */
	 /*  enough buffer space to make this 20 bit register wrap around. */
	while (in_be32(&tomal->regs[channel]->rxTotalBufferSize) &&
		(0x00100000 - in_be32(&tomal->regs[channel]->rxTotalBufferSize)) > 0x0000ffff)
		out_be32(&tomal->regs[channel]->rxAddFreeBytes, 0xffff);
	if (in_be32(&tomal->regs[channel]->rxTotalBufferSize))
		out_be32(&tomal->regs[channel]->rxAddFreeBytes, 0x00100000 - in_be32(&tomal->regs[channel]->rxTotalBufferSize));

	PRINTK(DBG_TOMAL | DBG_LEVEL2, "exit - rc=%d\n", rc);

	return rc;
}


/*  Returns the number of RX buffers that are waiting to be processed.  An error is indicated */
/*  by a negative value.  The caller should be holding the TOMAL lock for the specified channel. */
int tomal_pending_rx_buffers(TOMAL* tomal,
			     U8 channel)
{
	int rc = 0;
	RxDescSegment* segment = tomal->rxDescSegment[channel];
	RxDescSegment* startSegment = segment;
	RxDesc* desc;

        PRINTK(DBG_TOMAL | DBG_LEVEL2, "entry - tomal=%p, channel=%d\n", tomal, channel);

	do {
		 /*  Look for any descriptors awaiting processing. */
		for (desc = segment->desc; desc != (RxDesc*) segment->branchDesc; desc++)
			if ((desc->status &  TOMAL_RX_LAST) && desc->totalFrameLength)
				rc++;

		segment = segment->next;
	} while (segment != startSegment);

        PRINTK(DBG_TOMAL | DBG_LEVEL2, "exit - rc=%d\n", rc);

        return rc;
}


/*  Returns the number of TX buffers that are queued for transmission.  An error is indicated */
/*  by a negative value.  The caller should be holding the TOMAL TX lock for the specified channel. */
int tomal_pending_tx_buffers(TOMAL* tomal,
                             U8 channel)
{
        int rc = 0;
	TxDescSegment* segment = tomal->txDescSegment[channel];
	TxDescSegment* startSegment = segment;
	TxDesc* desc;

        PRINTK(DBG_TOMAL | DBG_LEVEL2, "entry - tomal=%p, channel=%d\n", tomal, channel);

        do {
		 /*  Look for any descriptors awaiting processing. */
                for (desc = segment->desc; desc != (TxDesc*) segment->branchDesc; desc++)
			if (desc->postedLength)
				rc++;

		segment = segment->next;
        } while (segment != startSegment);

        PRINTK(DBG_TOMAL | DBG_LEVEL2, "exit - rc=%d\n", rc);

        return rc;
}


/*  Allocate a Tx descriptor segment with the specified number of descriptors. */
static TxDescSegment* tomal_alloc_tx_segment(U32 numDescriptors)
{
        TxDescSegment* segment = NULL;
        TxDesc* desc;
        size_t size = numDescriptors * sizeof(TxDesc) + sizeof(BranchDesc);
        dma_addr_t dmaHandle;

	PRINTK(DBG_TOMAL | DBG_LEVEL2, "entry - numDescriptors=%d\n", numDescriptors);

         /*  Allocate descriptor storage. */
        desc = (TxDesc*) dma_alloc_coherent(NULL, size, &dmaHandle, GFP_KERNEL);
        if (desc) {
		 /*  Clear the descriptor storage. */
		memset((void*) desc, 0, size);

                 /*  Allocate a segment. */
                segment = kmalloc(sizeof(TxDescSegment), GFP_KERNEL);
                if (segment) {
                        segment->size = size;
                        segment->dmaHandle = dmaHandle;
                        segment->desc = desc;

                        segment->branchDesc = (BranchDesc*) &segment->desc[numDescriptors];
                        segment->branchDesc->code = TOMAL_BRANCH_CODE;
                        segment->branchDesc->reserved = segment->branchDesc->nextDescAddrH = 0;
                        segment->branchDesc->nextDescAddrL = (U32) NULL;

                         /*  Allocate storage for buffer pointers. */
                        segment->skb = (struct sk_buff**)
				kmalloc((numDescriptors+1) * sizeof(struct sk_buff*), GFP_KERNEL);
                        if (!segment->skb) {
                                kfree((void*) segment);
                                segment = NULL;
                                dma_free_coherent(NULL, size, (void*) segment->desc, segment->dmaHandle);
                        } else {
				memset((void*) segment->skb, 0,
					(numDescriptors+1) * sizeof(struct sk_buff*));
                                segment->oldIndex = segment->freeIndex = 0;
                                segment->next = segment;  /*  by default point this segment at itself */
                        }
                } else
                        dma_free_coherent(NULL, size, (void*) desc, dmaHandle);
        }

	PRINTK(DBG_TOMAL | DBG_LEVEL2, "exit - segment=%p\n", segment);

        return segment;
}


/*  Allocate Tx descriptor segment(s) until the specified number of descriptors have been created. */
int  tomal_alloc_tx_segments(TOMAL* tomal,
                             U8 channel,
                               U32 totalDescriptors)
{
        TxDescSegment* firstSegment = (TxDescSegment*) NULL;
        TxDescSegment* prevSegment = (TxDescSegment*) NULL;
        TxDescSegment* segment = (TxDescSegment*) NULL;
        U32 numDescriptors = totalDescriptors;
        U8 first = 1;
        int rc;

	PRINTK(DBG_TOMAL | DBG_LEVEL2, "entry - tomal=%p, channel=%d, totalDescriptors=%d\n", tomal,
		channel, totalDescriptors);

         /*  Allocate TX segments until the indicated number of descriptors have been */
         /*  created. */
        while (totalDescriptors && numDescriptors >= 1) {
                 /*  Allocate an TX descriptor segment. */
                segment = tomal_alloc_tx_segment(numDescriptors);
                if (segment) {
                         /*  If this was the first segment then remember it. */
                        if (first) {
                                firstSegment = prevSegment = segment;
                                first = 0;
                        }

                         /*  Link the previous segment to the new segment. */
                        prevSegment->branchDesc->nextDescAddrL = (U32) segment->dmaHandle;
                        prevSegment->next = segment;

                        totalDescriptors -= numDescriptors;
                } else {
                         /*  Failure allocating a segment of the requested size.  Reduce the size. */
                        numDescriptors /= 2;
                }
        }

         /*  All segments created? */
        if (!segment) {
                TxDescSegment* nextSegment = NULL;

                 /*  Free any segments that were allocated. */
                segment = prevSegment = firstSegment;
                while (segment) {
                        nextSegment = segment->next;
                        BUG_ON(nextSegment == segment);

                        tomal_free_tx_segment(segment);

                        segment = nextSegment;
                }
                tomal->txDescSegment[channel] = (TxDescSegment*) NULL;

		e10000_printr(bg_subcomp_tomal, tomal_ras_alloc_error,
				"TX descriptor allocation failure - totalDescriptors=%d.",
				totalDescriptors);
                rc = -ENOMEM;
        } else {
                 /*  Link the last segment to the first. */
                segment->branchDesc->nextDescAddrL = (U32) firstSegment->dmaHandle;
                segment->next = firstSegment;

                tomal->txDescSegment[channel] = segment;
                rc = 0;
        }

	 /*  Tell TOMAL where the descriptor storage is. */
	out_be32(&tomal->regs[channel]->txHWCurrentDescriptorAddrH, 0);
	out_be32(&tomal->regs[channel]->txHWCurrentDescriptorAddrL,
		 (U32) tomal->txDescSegment[channel]->dmaHandle);
                        tomal->pendingTxBuffers[channel] = 0;
	tomal->oldTxSegment[channel] = tomal->freeTxSegment[channel] = tomal->txDescSegment[channel];
	tomal->freeTxSegment[channel]->freeIndex = tomal->freeTxSegment[channel]->oldIndex =
                                tomal->freeTxSegment[channel]->oldIndex =
                                tomal->numberOfTransmittedFrames[channel] = 0;

	PRINTK(DBG_TOMAL | DBG_LEVEL2, "exit - rc=%d\n", rc);

        return rc;
}


/*  Free all Tx descriptor segments. */
void tomal_free_tx_segments(TOMAL* tomal,
			           U8 channel)
{
        TxDescSegment* segment = tomal->txDescSegment[channel];
        TxDescSegment* startSegment = segment;
        TxDescSegment* nextSegment;

        PRINTK(DBG_TOMAL | DBG_LEVEL2, "entry - tomal=%p, channel=%d\n", tomal, channel);

        while (segment) {
                nextSegment = segment->next;

                tomal_free_tx_segment(segment);
                segment = nextSegment;

                if (segment == startSegment)
                        break;
        }
        tomal->txDescSegment[channel] = NULL;

        PRINTK(DBG_TOMAL | DBG_LEVEL2, "exit\n");

        return;
}


/*  Free the specified Tx segment. */
void tomal_free_tx_segment(TxDescSegment* segment)
{
	TxDesc* desc;
	struct sk_buff** skb;

	 /*  Look for any descriptors with an associated buffer. */
	for (desc = segment->desc, skb = segment->skb;
		desc && desc != (TxDesc*) segment->branchDesc; desc++, skb++) {
		if (*skb) {
			dma_unmap_single(NULL, desc->buffHeadAddrL,
					 desc->postedLength, DMA_FROM_DEVICE);
                        dev_kfree_skb_any(*skb);
                        *skb = NULL;
                }
		desc->postedLength = 0;
	}

         /*  Free SKB pointer storage. */
        if (segment->skb)
                kfree(segment->skb);

         /*  Free the descriptor storage. */
        if (segment->desc)
                dma_free_coherent(NULL, segment->size, (void*) segment->desc, segment->dmaHandle);

         /*  Free the segment. */
        kfree((void*) segment);

        return;
}



/*  Free all Tx buffers. */
void tomal_free_tx_buffers(TOMAL* tomal,
			   U8 channel)
{
	TxDescSegment* segment = tomal->txDescSegment[channel];
        TxDescSegment* startSegment = segment;
        TxDesc* desc;
        struct sk_buff** skb;

	while (segment) {
		 /*  Look for any descriptors with an associated buffer. */
                for (desc = segment->desc, skb = segment->skb;
			desc != (TxDesc*) segment->branchDesc; desc++, skb++) {
                        if (*skb) {
				dma_unmap_single(NULL, desc->buffHeadAddrL,
                                                 desc->postedLength, DMA_FROM_DEVICE);
                                dev_kfree_skb_any(*skb);
                                *skb = NULL;
                        }

                        desc->postedLength = 0;
                }

                segment = segment->next;
		if (segment == startSegment)
			break;
        }

        return;
}



int tomal_process_tx_buffers(TOMAL* tomal,
                             U8 channel,
                             register U32 framesToProcess)
{
        register TxDescSegment* segment = tomal->oldTxSegment[channel];
        register TxDesc* desc = &segment->desc[segment->oldIndex];
        register int skbFrag = 0;
	register int rc = 0;

        PRINTK(DBG_TOMAL | DBG_LEVEL2, "entry - tomal=%p, channel=%d\n", tomal, channel);

         /*  Process the non-served descriptors, starting with the oldest. */
	tomal->numberOfTransmittedFrames[channel] += framesToProcess;
        while (likely(framesToProcess)) {
                 /*  Have we reached the end of the segment? */
                if (unlikely(desc == (TxDesc*) segment->branchDesc)) {
                         /*  Reset the oldest descriptor pointer and move the oldest segment ahead. */
                        segment->oldIndex = 0;
                        tomal->oldTxSegment[channel] = segment = segment->next;
                        desc = segment->desc;
                }

                 /*  Process the current descriptor. */
                PRINTK(DBG_TOMAL | DBG_LEVEL3, "xmit of buffer [%x] complete\n",
                        desc->buffHeadAddrL);

                if (likely(desc->code & TOMAL_TX_LAST)) {
                         /*  Unmap the buffer.  Free the skb.  Check descriptor status.  Increment the */
                         /*  transmitted frame count. */
                        dma_unmap_single(NULL, desc->buffHeadAddrL, desc->postedLength, DMA_TO_DEVICE);
                        dev_kfree_skb_irq(segment->skb[segment->oldIndex]);
			segment->skb[segment->oldIndex] = NULL;
                        skbFrag = 0;
			framesToProcess--;
                        if (unlikely(!(desc->wBStatus & TOMAL_TX_STATUS_GOOD)))
                                ((EMAC*) netdev_priv(tomal->netDev[channel]))->stats.tx_errors++;
                } else
                         /*  We have a fragmented skb and the first buffer is a special */
                         /*  case because we didn't map an entire page for it.  Unmap */
                         /*  the buffer now. */
                        if (!skbFrag) {
                                dma_unmap_single(NULL, desc->buffHeadAddrL,
                                                 desc->postedLength, DMA_TO_DEVICE);
                                skbFrag = 1;
                        } else
                                 /*  Unmap the page that contains the current fragment. */
                                dma_unmap_page(NULL, desc->buffHeadAddrL,
                                                desc->postedLength, DMA_TO_DEVICE);

                 /*  Advance to next descriptor. */
                desc++;
                segment->oldIndex++;
		rc++;
        }

	tomal->pendingTxBuffers[channel] -= rc;

         /*  Restart the TX counters. */
        out_be32(&tomal->regs[0]->txNotificationCtrl, (channel ? TOMAL_TX_NOTIFY_CTRL_COUNTER_START1 : TOMAL_TX_NOTIFY_CTRL_COUNTER_START0));

        if (unlikely(netif_queue_stopped(tomal->netDev[channel]) &&
		     (tomal->pendingTxBuffers[channel] + MAX_SKB_FRAGS + 1) < tomal->maxTxBuffers[channel]))
                netif_wake_queue(tomal->netDev[channel]);

        PRINTK(DBG_TOMAL | DBG_LEVEL2, "exit - rc=%d\n", rc);

        return rc;
}



/*  Disable IRQs. */
void tomal_irq_disable(TOMAL* tomal,
			U8 channel)
{
         /*  Disable TX & RX MAC event and interrupt generation. */
        out_be32(&tomal->regs[channel]->rxMACStatusEnable, 0);
        out_be32(&tomal->regs[channel]->txMACStatusEnable, 0);
        out_be32(&tomal->regs[channel]->txMACStatusEnable, 0);
        out_be32(&tomal->regs[channel]->txMACStatusMask, 0);

         /*  Disable HW error event and interrupt generation. */
        out_be32(&tomal->regs[channel]->hwErrorsEnable, 0);
        out_be32(&tomal->regs[channel]->hwErrorsMask, 0);

         /*  Disable SW critical and non-critical error event and */
         /*  interrupt generation. */
        out_be32(&tomal->regs[channel]->swCriticalErrorsEnable, 0);
        out_be32(&tomal->regs[channel]->swCriticalErrorsMask, 0);
        out_be32(&tomal->regs[channel]->swNonCriticalErrorsEnable, 0);
        out_be32(&tomal->regs[channel]->swNonCriticalErrorsMask, 0);

         /*  Disable TX & RX event interrupts. */
        out_be32(&tomal->regs[channel]->rxEventMask, 0);
        out_be32(&tomal->regs[channel]->txEventMask, 0);

        return;
}


/*  Enable IRQs and interrupt generation mechanisms. */
void tomal_irq_enable(TOMAL* tomal,
			U8 channel)
{
         /*  Enable TX & RX MAC event and interrupt generation. */
        out_be32(&tomal->regs[channel]->rxMACStatusEnable, TOMAL_RX_MAC_XEMAC_MASK);
        out_be32(&tomal->regs[channel]->txMACStatusEnable, TOMAL_TX_MAC_XEMAC_MASK);
        out_be32(&tomal->regs[channel]->txMACStatusEnable, TOMAL_TX_MAC_XEMAC_MASK);
        out_be32(&tomal->regs[channel]->txMACStatusMask, TOMAL_TX_MAC_XEMAC_MASK);

         /*  Enable HW error event and interrupt generation. */
        out_be32(&tomal->regs[channel]->hwErrorsEnable,
		 TOMAL_HW_ERRORS_IRAPE | TOMAL_HW_ERRORS_ORAPE |
                 TOMAL_HW_ERRORS_IDBPE | TOMAL_HW_ERRORS_ODBPE);
        out_be32(&tomal->regs[channel]->hwErrorsMask,
		 TOMAL_HW_ERRORS_IRAPE | TOMAL_HW_ERRORS_ORAPE |
                 TOMAL_HW_ERRORS_IDBPE | TOMAL_HW_ERRORS_ODBPE);

         /*  Enable SW critical and non-critical error event and */
         /*  interrupt generation. */
        out_be32(&tomal->regs[channel]->swCriticalErrorsEnable,
		 TOMAL_SW_CRIT_ERRORS_TDBC | TOMAL_SW_CRIT_ERRORS_RDBC);
        out_be32(&tomal->regs[channel]->swCriticalErrorsMask,
		 TOMAL_SW_CRIT_ERRORS_TDBC | TOMAL_SW_CRIT_ERRORS_RDBC);
        out_be32(&tomal->regs[channel]->swNonCriticalErrorsEnable,
		 TOMAL_SW_NONCRIT_ERRORS_TPDBC |  TOMAL_SW_NONCRIT_ERRORS_RTSDB);
        out_be32(&tomal->regs[channel]->swNonCriticalErrorsMask,
		 TOMAL_SW_NONCRIT_ERRORS_TPDBC |  TOMAL_SW_NONCRIT_ERRORS_RTSDB);

         /*  Enable TX & RX event interrupts. */
        out_be32(&tomal->regs[channel]->rxEventMask, TOMAL_RX_EVENT);
        out_be32(&tomal->regs[channel]->txEventMask, TOMAL_TX_EVENT);

         /*  Enable TX counters. */
        out_be32(&tomal->regs[0]->txNotificationCtrl,
		 (channel ? TOMAL_TX_NOTIFY_CTRL_COUNTER_START1 :
                   TOMAL_TX_NOTIFY_CTRL_COUNTER_START0));

         /*  Enable RX counters. */
        out_be32(&tomal->regs[0]->rxNotificationCtrl,
		 (channel ? TOMAL_RX_NOTIFY_CTRL_COUNTER_START1 :
                  TOMAL_RX_NOTIFY_CTRL_COUNTER_START0));

        return;
}


/*  Handle IRQs for channel 0 and any IRQs not specific to any channel. */
static irqreturn_t tomal_irq0(int irq,
			      void* data)
{
	int rc = IRQ_NONE;
	TOMAL* tomal = (TOMAL*) data;
	EMAC* emac = (EMAC*) netdev_priv(tomal->netDev[0]);
	U32 isr = in_be32(&tomal->regs[0]->interruptStatus);
#ifdef CONFIG_BGP_E10000_NAPI
	int pollScheduled = 0;
#endif

	PRINTK(DBG_TOMAL | DBG_LEVEL2, "entry - irq=%d isr=%08x\n", irq, isr);

	if (likely(irq == tomal->irq0)) {
		if (isr & TOMAL_INTERRUPT_RX0) {
#ifndef CONFIG_BGP_E10000_NAPI
			int budget = tomal->maxRxBuffers[0];
#endif
			PRINTK(DBG_NAPI, "TOMAL_INTERRUPT_RX0 - irq=%d isr=%08x\n", irq, isr);
			spin_lock(&tomal->rxLock[0]);
#ifdef CONFIG_BGP_E10000_NAPI
			 /*  Disable further Rx interrupts. */
			out_be32(&tomal->regs[0]->rxEventMask, 0);

			 /*  Schedule Rx processing. */
			napi_schedule(&(tomal->napi[0])) ;
			pollScheduled = 1;
#endif

			 /*  Clear the RX interrupt. */
			out_be32(&tomal->regs[0]->rxEventStatus, TOMAL_RX_EVENT);

#ifndef CONFIG_BGP_E10000_NAPI
			 /*  Process the buffers then allocate new ones. */
			rc = tomal_poll(tomal->netDev[0], budget);
			if (rc != 0)
				printk(KERN_CRIT "Failure processing RX buffers [%d]\n", rc);
#endif
			spin_unlock(&tomal->rxLock[0]);
			PRINTK(DBG_NAPI, "TOMAL_INTERRUPT_RX0 - IRQ_HANDLED\n");
			rc = IRQ_HANDLED;
		}
                if (isr & TOMAL_INTERRUPT_TX0) {
                        spin_lock(&tomal->txLock[0]);

			 /*  Clear any TX interrupt. */
			out_be32(&tomal->regs[0]->txEventStatus, TOMAL_TX_EVENT);

                         /*  Process the buffers that have been transmitted. */
                        rc = tomal_process_tx_buffers(tomal, 0,
						      in_be32(&tomal->regs[0]->txNumberOfTransmittedFrames)-tomal->numberOfTransmittedFrames[0]);
			if (rc <0)
				printk(KERN_CRIT "Failure processing TX buffers [%d]\n", rc);

                        spin_unlock(&tomal->txLock[0]);
			rc = IRQ_HANDLED;
                }
		if (isr & TOMAL_INTERRUPT_TX_MAC_ERROR0) {
			U32 status = in_be32(&tomal->regs[0]->txMACStatus);

			PRINTK(DBG_TOMAL | DBG_LEVEL1, "TOMAL_INTERRUPT_TX_MAC_ERROR0 [%08x]\n", status);

			emac->stats.tx_errors++;

			 /*  Clear the interrupt. */
			out_be32(&tomal->regs[0]->txMACStatus, status);
			rc = IRQ_HANDLED;
		}
		if (isr & TOMAL_INTERRUPT_RX_MAC_ERROR0) {
			U32 status = in_be32(&tomal->regs[0]->rxMACStatus);

			PRINTK(DBG_TOMAL | DBG_LEVEL1, "TOMAL_INTERRUPT_RX_MAC_ERROR0 [%08x]\n", status);

			emac->stats.rx_errors++;

                         /*  Clear the interrupt. */
                        out_be32(&tomal->regs[0]->rxMACStatus, status);
			rc = IRQ_HANDLED;
		}
		if (isr & TOMAL_INTERRUPT_SW_NONCRITICAL_ERROR0) {
			U32 status = in_be32(&tomal->regs[0]->swNonCriticalErrorsStatus);
#ifndef CONFIG_BGP_E10000_NAPI
			int budget = tomal->maxRxBuffers[0];
#else
			U32 swNonCriticalErrorsMask;
#endif

			if (status & TOMAL_SW_NONCRIT_ERRORS_TPDBC) {
				 /*  Checksum failed on requested frame. */
				emac->stats.tx_errors++;
			} else if (status & TOMAL_SW_NONCRIT_ERRORS_RTSDB) {
				 /*  TOMAL has exhausted all the RX buffers. */
				U32 hwdrops = in_be32(&tomal->regs[0]->rxDroppedFramesCount);
				emac->stats.rx_dropped += hwdrops;
				tomal->numberOfHwDrops0 += hwdrops;
				out_be32(&tomal->regs[0]->rxDroppedFramesCount, 0);
				emac->stats.rx_errors++;
#ifndef CONFIG_BGP_E10000_NAPI
				tomal_poll(tomal->netDev[0], budget);
#else
				 /*  Disable too short Rx buffer interrupt and schedule Rx processing. */
				swNonCriticalErrorsMask = in_be32(&tomal->regs[0]->swNonCriticalErrorsMask);
				out_be32(&tomal->regs[0]->swNonCriticalErrorsMask,
					 swNonCriticalErrorsMask & ~TOMAL_SW_NONCRIT_ERRORS_RTSDB);
				PRINTK(DBG_NAPI, "TOMAL_INTERRUPT_SW_NONCRITICAL_ERROR0 pollScheduled=%d\n",pollScheduled);
				if (!pollScheduled)
					napi_schedule(&(tomal->napi[0])) ;

#endif
			}
			else
				e10000_printr(bg_subcomp_tomal, tomal_ras_unknown_noncrit_int,
						"Unknown non-critical SW error [0x%08x].", status);

			 /*  Clear the interrupt. */
			out_be32(&tomal->regs[0]->swNonCriticalErrorsStatus, status);
			rc = IRQ_HANDLED;
		}
		if (isr & TOMAL_INTERRUPT_CRITICAL_ERROR) {
			U32 swStatus = in_be32(&tomal->regs[0]->swCriticalErrorsStatus);
			U32 hwStatus = in_be32(&tomal->regs[0]->hwErrorsStatus);

			PRINTK(DBG_TOMAL | DBG_LEVEL1, "TOMAL_INTERRUPT_CRITICAL_ERROR [SW=%08x, HW=%08x]\n",
				swStatus, hwStatus);

			 /*  Check for software errors. */
			if (swStatus & TOMAL_SW_CRIT_ERRORS_TDBC)
				emac->stats.tx_errors++;
			else if (swStatus & TOMAL_SW_CRIT_ERRORS_RDBC)
				emac->stats.rx_errors++;
			else if (swStatus)
				e10000_printr(bg_subcomp_tomal, tomal_ras_unknown_critical_int,
						"Unknown critical SW error [%08x].", swStatus);

			 /*  Check for hardware errors. */
			if (hwStatus & (TOMAL_HW_ERRORS_IRAPE | TOMAL_HW_ERRORS_IDBPE))
				emac->stats.rx_errors++;
			else if (hwStatus & (TOMAL_HW_ERRORS_ORAPE | TOMAL_HW_ERRORS_ODBPE))
				emac->stats.tx_errors++;
			else if (hwStatus)
				e10000_printr(bg_subcomp_tomal, tomal_ras_unknown_critical_int,
					"Unknown critical HW error [%08x].", hwStatus);

			 /*  Clear the interrupt(s). */
			out_be32(&tomal->regs[0]->hwErrorsStatus, hwStatus);
			out_be32(&tomal->regs[0]->swCriticalErrorsStatus, swStatus);

			 /*  Soft reset required here. */
			tomal_soft_reset(tomal);
			tomal_irq_enable(tomal, 0);

			rc = IRQ_HANDLED;
		}
		if (rc != IRQ_HANDLED) {
			e10000_printr(bg_subcomp_tomal, tomal_ras_spurious_irq,
                                "Unhandled interrupt - irq=%d, isr=0x%08x, rc=%d",
                                irq, isr, rc);
		}
	} else {
		e10000_printr(bg_subcomp_tomal, tomal_ras_spurious_irq,
				"Spurious interrupt - irq=%d, isr=0x%08x.",
				irq, isr);
	}

	return rc;
}

/*  Handle interrupts for channel 0. */
static irqreturn_t tomal_irq1(int irq,
			      void* data)
{
	int rc = IRQ_NONE;
	TOMAL* tomal = (TOMAL*) data;
	EMAC* emac = (EMAC*) netdev_priv(tomal->netDev[1]);
	U32 isr = in_be32(&tomal->regs[0]->interruptStatus);
#ifdef CONFIG_BGP_E10000_NAPI
	int pollScheduled = 0;
#endif

	PRINTK(DBG_TOMAL | DBG_LEVEL2, "entry - irq=%d isr=%08x\n", irq, isr);

	if (likely(irq == tomal->irq1)) {
		if (isr & TOMAL_INTERRUPT_RX1) {
#ifndef CONFIG_BGP_E10000_NAPI
			int budget = tomal->maxRxBuffers[1];
#endif
			spin_lock(&tomal->rxLock[1]);

#ifdef CONFIG_BGP_E10000_NAPI
			 /*  Disable further Rx interrupts. */
			out_be32(&tomal->regs[1]->rxEventMask, 0);

			 /*  Schedule Rx processing. */
			napi_schedule(&(tomal->napi[1])) ;
			pollScheduled = 1;
#endif

			 /*  Clear the RX interrupt. */
			out_be32(&tomal->regs[1]->rxEventStatus, TOMAL_RX_EVENT);

#ifndef CONFIG_BGP_E10000_NAPI
			 /*  Process the buffers then allocate new ones. */
			rc = tomal_poll(tomal->netDev[1], budget);
			if (rc != 0)
				printk(KERN_CRIT "Failure processing RX buffers [%d]\n", rc);
#endif
			spin_unlock(&tomal->rxLock[1]);
			rc = IRQ_HANDLED;
		}
		if (isr & TOMAL_INTERRUPT_TX1) {
			spin_lock(&tomal->txLock[1]);

			 /*  Clear any TX interrupt. */
			out_be32(&tomal->regs[1]->txEventStatus, TOMAL_TX_EVENT);

			 /*  Process the buffers that have been transmitted. */
			rc = tomal_process_tx_buffers(tomal, 1,
						      in_be32(&tomal->regs[1]->txNumberOfTransmittedFrames) - tomal->numberOfTransmittedFrames[1]);
			if (rc < 0)
				printk(KERN_CRIT "Failure processing TX buffers [%d]\n", rc);

			spin_unlock(&tomal->txLock[1]);
			rc = IRQ_HANDLED;
		}
		if (isr & TOMAL_INTERRUPT_TX_MAC_ERROR1) {
			U32 status = in_be32(&tomal->regs[1]->txMACStatus);

			PRINTK(DBG_TOMAL | DBG_LEVEL1, "TOMAL_INTERRUPT_TX_MAC_ERROR1 [%08x]\n", status);

			emac->stats.tx_errors++;

			 /*  Clear the interrupt. */
			out_be32(&tomal->regs[1]->txMACStatus, status);
			rc = IRQ_HANDLED;
		}
		if (isr & TOMAL_INTERRUPT_RX_MAC_ERROR1) {
			U32 status = in_be32(&tomal->regs[1]->rxMACStatus);

			PRINTK(DBG_TOMAL | DBG_LEVEL1, "TOMAL_INTERRUPT_RX_MAC_ERROR1 [%08x]\n", status);

			emac->stats.rx_errors++;

			 /*  Clear the interrupt. */
			out_be32(&tomal->regs[1]->rxMACStatus, status);
			rc = IRQ_HANDLED;
		}
		if (isr & TOMAL_INTERRUPT_SW_NONCRITICAL_ERROR0) {
			U32 status = in_be32(&tomal->regs[1]->swNonCriticalErrorsStatus);
#ifndef CONFIG_BGP_E10000_NAPI
			int budget = tomal->maxRxBuffers[1];
#else
			U32 swNonCriticalErrorsMask;
#endif
			if (status & TOMAL_SW_NONCRIT_ERRORS_TPDBC)
				emac->stats.tx_errors++;
			else if (status & TOMAL_SW_NONCRIT_ERRORS_RTSDB) {
				 /*  TOMAL has exhausted all the RX buffers. */
				U32 hwdrops = in_be32(&tomal->regs[1]->rxDroppedFramesCount);
				emac->stats.rx_dropped += hwdrops;
				tomal->numberOfHwDrops1 += hwdrops;
				out_be32(&tomal->regs[1]->rxDroppedFramesCount, 0);
				emac->stats.rx_errors++;
#ifndef CONFIG_BGP_E10000_NAPI
				tomal_poll(tomal->netDev[1], budget);
#else
				 /*  Disable 'too short Rx buffer' interrupt and schedule Rx processing. */
				swNonCriticalErrorsMask = in_be32(&tomal->regs[1]->swNonCriticalErrorsMask);
				out_be32(&tomal->regs[1]->swNonCriticalErrorsMask,
					 swNonCriticalErrorsMask & ~TOMAL_SW_NONCRIT_ERRORS_RTSDB);
				if (!pollScheduled)
					napi_schedule(&(tomal->napi[1])) ;
#endif
			} else
				e10000_printr(bg_subcomp_tomal, tomal_ras_unknown_noncrit_int,
					      "Unknown non-critical SW error [0x%08x].", status);

			 /*  Clear the interrupt. */
			out_be32(&tomal->regs[1]->swNonCriticalErrorsStatus, status);
			rc = IRQ_HANDLED;
		}
		if (rc != IRQ_HANDLED) {
			e10000_printr(bg_subcomp_tomal, tomal_ras_spurious_irq,
				      "Unhandled interrupt - irq=%d, isr=0x%08x, rc=%d",
				      irq, isr, rc);
                }
        } else {
		e10000_printr(bg_subcomp_tomal, tomal_ras_spurious_irq,
			      "Spurious interrupt - irq=%d, isr=0x%08x.", irq, isr);
	}

	return rc;
}


/*  Configure TOMAL. */
int tomal_configure(TOMAL* tomal)
{
	int rc = 0;
	int c;

	PRINTK(DBG_TOMAL | DBG_LEVEL2 | DBG_NAPI, "entry - tomal=%p\n", tomal);

	out_be32(&tomal->regs[0]->configurationCtrl, TOMAL_CFG_CTRL_RX_MAC0 |
		 TOMAL_CFG_CTRL_RX_MAC1 | TOMAL_CFG_CTRL_TX_MAC0 |
		  TOMAL_CFG_CTRL_TX_MAC1 | TOMAL_CFG_CTRL_PLB_FREQ_250);
	out_be32(&tomal->regs[0]->consumerMemoryBaseAddr, 0);
	out_be32(&tomal->regs[0]->packetDataEngineCtrl, TOMAL_PDE_CTRL_RX_PREFETCH1 |
		 TOMAL_PDE_CTRL_TX_PREFETCH1);  /*  prefetch 1 descriptor */
        out_be32(&tomal->regs[0]->interruptRoute, TOMAL_IRQ1_MASK);  /*  route #1 ints to TOE_PLB_INT[1] */
	for (c = 0; c < TOMAL_MAX_CHANNELS; c++)
		if (tomal->netDev[c]) {
                         /*  Allocate RX descriptors. */
                        rc = tomal_alloc_rx_segments(tomal, c, tomal->maxRxBuffers[c]);
                        if (rc) {
                                 /*  Failure allocating requested descriptors. */
                                BUG_ON(rc);
                        }

			 /*  Allocate RX buffers and initialize RX descriptor info. */
			tomal->oldRxSegment[c] = tomal->rxDescSegment[c];

			rc = tomal_alloc_rx_buffers(tomal, c);
			if (rc <= 0) {
				if (c && tomal->netDev[0])
					tomal_free_rx_buffers(tomal, 0);
				break;
			}
			else
				rc = 0;

                         /*  Allocate TX descriptors and initialize TX descriptor info. */
                        rc = tomal_alloc_tx_segments(tomal, c, tomal->maxTxBuffers[c]);
                        if (rc) {
                                 /*  Failure allocating requested descriptors. */
                                printk(KERN_CRIT "Failure allocating %d TX descriptors.\n", tomal->maxTxBuffers[c]);
				BUG_ON(rc);
                        }
                        tomal->pendingTxBuffers[c] = 0;
			tomal->oldTxSegment[c] = tomal->freeTxSegment[c] = tomal->txDescSegment[c];
			tomal->freeTxSegment[c]->freeIndex = tomal->freeTxSegment[c]->oldIndex =
				tomal->numberOfTransmittedFrames[c] = tomal->numberOfReceivedFrames[c] = 0;

			 /*  Initialize the timers and counters. */
			out_be32(&tomal->regs[c]->txMinTimer, 255);
			out_be32(&tomal->regs[c]->txMaxTimer, 255);
			out_be32(&tomal->regs[c]->txMaxFrameNum, tomal->maxTxBuffers[c]);
			out_be32(&tomal->regs[c]->txMinFrameNum, 255);
			out_be32(&tomal->regs[c]->rxMinTimer, 255);
			out_be32(&tomal->regs[c]->rxMaxTimer, 22);
			out_be32(&tomal->regs[c]->rxMinFrameNum, 255);
#ifdef CONFIG_BGP_E10000_NAPI
			out_be32(&tomal->regs[c]->rxMaxFrameNum, 4);
#else
			out_be32(&tomal->regs[c]->rxMaxFrameNum, 64);
#endif

                         /*  Initialize spinlocks. */
                        spin_lock_init(&tomal->rxLock[c]);
			spin_lock_init(&tomal->txLock[c]);

#ifdef CONFIG_BGP_E10000_NAPI
			    netif_napi_add(tomal->netDev[c],&(tomal->napi[c]),tomal_poll_napi,tomal->maxRxBuffers[c]) ;
			    napi_enable(&(tomal->napi[c])) ;
#endif
		}

	PRINTK(DBG_TOMAL | DBG_LEVEL2 | DBG_NAPI, "exit - rc=%d\n", rc);

	return rc;
}



TOMAL* __init
tomal_init(void* devMapAddr,
			struct net_device* netDev0,
                        U32 rxTotalBufferSize0,
                        U32 numTxBuffers0,
                        struct net_device* netDev1,
                        U32 rxTotalBufferSize1,
                        U32 numTxBuffers1,
			int irq0,
			int irq1,
			struct proc_dir_entry* procDir)
{
	TOMAL* tomal;
	int rc = 0;
	int c;

	PRINTK(DBG_TOMAL | DBG_LEVEL2, "entry - netDev0=%p, rxTotalBufferSize0=%d, "
		"numTxBuffers0=%d, netDev1=%p, rxTotalBufferSize1=%d, "
		"numTxBuffers1=%d, irq0=%d, irq1=%d, procDir=%p\n", netDev0, rxTotalBufferSize0,
		numTxBuffers0, netDev1, rxTotalBufferSize1, numTxBuffers1, irq0, irq1, procDir);

	 /*  Allocate tomal object. */
	tomal = kmalloc(sizeof(TOMAL), GFP_KERNEL);
	if (!tomal) {
		e10000_printr(bg_subcomp_tomal, tomal_ras_alloc_error,
				"Failure allocating TOMAL device.");
		rc = -ENOMEM;
		goto end;
	}
	memset((void*) tomal, 0, sizeof(*tomal));

	 /*  Map the TOMAL registers. */
	tomal->regs[0] = (TOMALRegs*) devMapAddr;
	if (!tomal->regs[0]) {
		e10000_printr(bg_subcomp_tomal, tomal_ras_ioremap_error,
				"Failure maping TOMAL registers.");
		rc = -ENXIO;
		goto free_tomal;
	}

	 /*  Setup a register mapping for the second channel.  The registers that */
	 /*  are specific to the second channel are located 0x100 bytes past the */
	 /*  registers specific to the first channel.  Use this mapping for */
	 /*  channel 1 specific registers only! */
	tomal->regs[1] = (TOMALRegs*) ((U8*) tomal->regs[0]) + 0x100;

         /*  Register interrupt handlers.  TOMAL has two interrupt lines. */
	tomal->irq0 = irq0;
	tomal->irq1 = irq1;
        rc = request_irq(tomal->irq0, tomal_irq0, IRQF_DISABLED, "TOMAL IRQ0", (void*) tomal);
        if (!rc) {
                rc = request_irq(tomal->irq1, tomal_irq1, IRQF_DISABLED, "TOMAL IRQ1", (void*) tomal);
                if (rc) {
			e10000_printr(bg_subcomp_tomal, tomal_ras_irq_unavailable,
					"Unable to register IRQ - irq1=0x%08x.", irq1);
                        free_irq(tomal->irq0, tomal);
			tomal->irq0 = 0xffffffff;
                        goto free_irqs;
		}
        } else {
		e10000_printr(bg_subcomp_tomal, tomal_ras_irq_unavailable,
				"Unable to register IRQ - irq0=0x%08x.", irq0);
                goto unmap_tomal_regs;
        }

	 /*  Create /proc/driver/e10000/tomal directory. */
	tomal->parentDir = procDir;
	if (procDir) {
		tomal->tomalDir = proc_mkdir("tomal", procDir);
		if (tomal->tomalDir) {
			tomal->hwDir = proc_mkdir("hw", tomal->tomalDir);
			if (tomal->hwDir) {
				E10000_PROC_ENTRY* entry = tomal_hw_proc_entry;

				while (entry->name) {
					entry->entry = e10000_create_proc_entry(tomal->hwDir, entry->name, (void*)
										((U32) entry->addr + (U32) tomal->regs[0]));
					entry++;
				}
			}
			tomal->swDir = proc_mkdir("sw", tomal->tomalDir);
			if (tomal->swDir) {
                                tomal_sw_proc_entry[0].entry =
                                        e10000_create_proc_entry(tomal->swDir, tomal_sw_proc_entry[0].name,
								 (void*) &tomal->maxRxBuffers[0]);
                                tomal_sw_proc_entry[1].entry =
                                        e10000_create_proc_entry(tomal->swDir, tomal_sw_proc_entry[1].name,
								(void*) &tomal->maxRxBuffers[1]);
                                tomal_sw_proc_entry[2].entry =
                                        e10000_create_proc_entry(tomal->swDir, tomal_sw_proc_entry[2].name,
								(void*) &tomal->rxBufferSize[0]);
                                tomal_sw_proc_entry[3].entry =
                                        e10000_create_proc_entry(tomal->swDir, tomal_sw_proc_entry[3].name,
								(void*) &tomal->rxBufferSize[1]);
                                tomal_sw_proc_entry[4].entry =
                                        e10000_create_proc_entry(tomal->swDir, tomal_sw_proc_entry[4].name,
								(void*) &tomal->rxDescSegment[0]);
                                tomal_sw_proc_entry[5].entry =
                                        e10000_create_proc_entry(tomal->swDir, tomal_sw_proc_entry[5].name,
								(void*) &tomal->rxDescSegment[1]);
                                tomal_sw_proc_entry[6].entry =
                                        e10000_create_proc_entry(tomal->swDir, tomal_sw_proc_entry[6].name,
								(void*) &tomal->oldRxSegment[0]);
                                tomal_sw_proc_entry[7].entry =
                                        e10000_create_proc_entry(tomal->swDir, tomal_sw_proc_entry[7].name,
								(void*) &tomal->oldRxSegment[1]);
                                tomal_sw_proc_entry[8].entry =
                                        e10000_create_proc_entry(tomal->swDir, tomal_sw_proc_entry[8].name,
								(void*) &tomal->maxTxBuffers[0]);
                                tomal_sw_proc_entry[9].entry =
                                        e10000_create_proc_entry(tomal->swDir, tomal_sw_proc_entry[9].name,
								(void*) &tomal->maxTxBuffers[1]);
                                tomal_sw_proc_entry[10].entry =
                                        e10000_create_proc_entry(tomal->swDir, tomal_sw_proc_entry[10].name,
								(void*) &tomal->pendingTxBuffers[0]);
                                tomal_sw_proc_entry[11].entry =
                                        e10000_create_proc_entry(tomal->swDir, tomal_sw_proc_entry[11].name,
								(void*) &tomal->pendingTxBuffers[1]);
                                tomal_sw_proc_entry[12].entry =
                                        e10000_create_proc_entry(tomal->swDir, tomal_sw_proc_entry[12].name,
								(void*) &tomal->numberOfTransmittedFrames[0]);
                                tomal_sw_proc_entry[13].entry =
                                        e10000_create_proc_entry(tomal->swDir, tomal_sw_proc_entry[13].name,
								(void*) &tomal->numberOfTransmittedFrames[1]);
                                tomal_sw_proc_entry[14].entry =
                                        e10000_create_proc_entry(tomal->swDir, tomal_sw_proc_entry[14].name,
								(void*) &tomal->txDescSegment[0]);
                                tomal_sw_proc_entry[15].entry =
                                        e10000_create_proc_entry(tomal->swDir, tomal_sw_proc_entry[15].name,
								(void*) &tomal->txDescSegment[1]);
                                tomal_sw_proc_entry[16].entry =
                                        e10000_create_proc_entry(tomal->swDir, tomal_sw_proc_entry[16].name,
								(void*) &tomal->oldTxSegment[0]);
                                tomal_sw_proc_entry[17].entry =
                                        e10000_create_proc_entry(tomal->swDir, tomal_sw_proc_entry[17].name,
								(void*) &tomal->oldTxSegment[1]);
                                tomal_sw_proc_entry[18].entry =
                                        e10000_create_proc_entry(tomal->swDir, tomal_sw_proc_entry[18].name,
								(void*) &tomal->freeTxSegment[0]);
                                tomal_sw_proc_entry[19].entry =
                                        e10000_create_proc_entry(tomal->swDir, tomal_sw_proc_entry[19].name,
								(void*) &tomal->freeTxSegment[1]);
                                tomal_sw_proc_entry[20].entry =
                                        e10000_create_proc_entry(tomal->swDir, tomal_sw_proc_entry[20].name,
								(void*) &tomal->irq0);
                                tomal_sw_proc_entry[21].entry =
                                        e10000_create_proc_entry(tomal->swDir, tomal_sw_proc_entry[21].name,
								(void*) &tomal->irq1);
                                tomal_sw_proc_entry[22].entry =
                                        e10000_create_proc_entry(tomal->swDir, tomal_sw_proc_entry[22].name,
								(void*) &tomal->numberOfNetrxDrops);
                                tomal_sw_proc_entry[23].entry =
                                        e10000_create_proc_entry(tomal->swDir, tomal_sw_proc_entry[23].name,
								(void*) &tomal->numberOfHwDrops0);
                                tomal_sw_proc_entry[24].entry =
                                        e10000_create_proc_entry(tomal->swDir, tomal_sw_proc_entry[24].name,
								(void*) &tomal->numberOfHwDrops1);
                                tomal_sw_proc_entry[25].entry =
                                        e10000_create_proc_entry(tomal->swDir, tomal_sw_proc_entry[25].name,
								(void*) &tomal->numberOfNotLast);
/*                                 tomal_sw_proc_entry[22].entry = */
/*                                         e10000_create_proc_entry(tomal->swDir, tomal_sw_proc_entry[22].name, */
/* 								(void*) &tomal->count_tx_checksum_type[0]); */
/*                                 tomal_sw_proc_entry[23].entry = */
/*                                         e10000_create_proc_entry(tomal->swDir, tomal_sw_proc_entry[23].name, */
/* 								(void*) &tomal->count_tx_checksum_type[1]); */
/*                                 tomal_sw_proc_entry[24].entry = */
/*                                         e10000_create_proc_entry(tomal->swDir, tomal_sw_proc_entry[24].name, */
/* 								(void*) &tomal->count_tx_checksum_type[2]); */
/*                                 tomal_sw_proc_entry[25].entry = */
/*                                         e10000_create_proc_entry(tomal->swDir, tomal_sw_proc_entry[25].name, */
/* 								(void*) &tomal->count_tx_checksum_type[3]); */
			}
		}
	}

	 /*  For each configured channel allocate descriptor segments and perform other initialization. */
	tomal->netDev[0] = netDev0;
	if (netDev0) {
		tomal->rxBufferSize[0] = 9000 + ETH_HLEN + BGP_E10000_FCS_SIZE;
		tomal->maxRxBuffers[0] = (rxTotalBufferSize0 <= TOMAL_RX_TOTAL_BUFFER_SIZE_MAX ? rxTotalBufferSize0 :
					  TOMAL_RX_TOTAL_BUFFER_SIZE_MAX) / tomal->rxBufferSize[0] ;
		tomal->maxTxBuffers[0] = numTxBuffers0;
	}
	tomal->netDev[1] = netDev1;
	if (netDev1) {
		tomal->rxBufferSize[1] = 9000 + ETH_HLEN + BGP_E10000_FCS_SIZE;
		tomal->maxRxBuffers[1] = (rxTotalBufferSize1 <= TOMAL_RX_TOTAL_BUFFER_SIZE_MAX ? rxTotalBufferSize1 :
					  TOMAL_RX_TOTAL_BUFFER_SIZE_MAX) / tomal->rxBufferSize[1];
		tomal->maxTxBuffers[1] = numTxBuffers1;
	}
	for (c = 0; c < TOMAL_MAX_CHANNELS; c++) {
		if (tomal->netDev[c]) {
#ifdef CONFIG_BGP_E10000_IP_CHECKSUM
			 /*  Tell the network stack that TOMAL performs IP checksum and */
			 /*  that it can handle the transmission of scatter/gather data. */
			tomal->netDev[c]->features |= (NETIF_F_SG | NETIF_F_IP_CSUM);
#endif
			tomal->netDev[c]->features |= (NETIF_F_HIGHDMA | NETIF_F_LLTX);

		}
	}
	tomal_soft_reset(tomal);

        goto end;

free_irqs:
	if (tomal->irq0)
		free_irq(tomal->irq0, (void*) tomal);
	if (tomal->irq1)
		free_irq(tomal->irq1, (void*) tomal);

unmap_tomal_regs:
	tomal->regs[0] = NULL;

free_tomal:
	kfree((void*) tomal);

end:

	PRINTK(DBG_TOMAL | DBG_LEVEL2, "exit - rc=%d\n", rc);

	return (rc ? ERR_PTR(rc) : tomal);
}


/*  Allocate an SKB for each Rx descriptor that doesn't already reference one. */
int tomal_alloc_rx_buffers(TOMAL* tomal,
			      U8 channel)
{
	int rc = 0;
	RxDescSegment* segment;
	RxDesc* desc;
	RxDesc* startDesc;
	struct sk_buff** skb;
	U32 bytesAlloced = 0;
	U32 buffersAlloced = 0;

	PRINTK(DBG_TOMAL | DBG_LEVEL2, "entry - tomal=%p channel=%d\n", tomal, channel);

	segment = tomal->rxDescSegment[channel];
	desc = segment->desc;
	startDesc = desc;
	skb = segment->skb;

	 /*  Iterate over all descriptors and allocate a buffer to any */
	 /*  descriptors that don't already point to a buffer. */
	do {
		 /*  Have we reached the end of the segment? */
		if (desc == (RxDesc*) segment->branchDesc) {
			 /*  Move the descriptor segment pointer to the next segment. */
			segment = segment->next;
			desc = segment->desc;
			skb = segment->skb;
			if (desc == startDesc)
				 /*  We've been through all descriptors. */
				break;
		}

		 /*  If this descriptor is unused then allocate a buffer here. */
		if (!desc->postedLength) {
			 /*  Allocate a buffer. */
			*skb = alloc_skb(tomal->rxBufferSize[channel] + 16, GFP_ATOMIC);
			if (*skb) {
				skb_reserve(*skb, 2);

				 /*  Point a descriptor at the buffer. */
				desc->code = TOMAL_RX_DESC_CODE;
				desc->postedLength = tomal->rxBufferSize[channel];
				desc->status = 0;
				desc->totalFrameLength = 0;
				desc->buffHeadAddrH = 0;
				desc->buffHeadAddrL =
					dma_map_single(NULL, (*skb)->data,
						       desc->postedLength,
						       DMA_FROM_DEVICE);
				BUG_ON(!desc->buffHeadAddrL);

				bytesAlloced += desc->postedLength;
				buffersAlloced++;
			} else {
				e10000_printr(bg_subcomp_tomal, tomal_ras_alloc_error,
						"Failure allocating SKB.");
				break;
			}
		}

		 /*  Advance to the next descriptor and buffer. */
		desc++;
		skb++;
	} while (desc != startDesc);

	 /*  Now tell TOMAL about all the buffers allocated. */
	 /*  We can add up to 64K at a time for a maximum total of 1MB. */
	while (bytesAlloced) {
		U32 size = (bytesAlloced <= 0xffff ? bytesAlloced : 0xffff);

		BUG_ON(in_be32(&tomal->regs[channel]->rxTotalBufferSize) + size > 0x100000);
		out_be32(&tomal->regs[channel]->rxAddFreeBytes, size);
		bytesAlloced -= size;
	}

	rc = (rc ? rc : buffersAlloced);

	PRINTK(DBG_TOMAL | DBG_LEVEL2, "exit - rc=%d\n", rc);

	return rc;
}


/*  Receive frames until the indicated number of frames have been received or there are no more */
/*  frames available. */
#if defined(CONFIG_BGP_E10000_NAPI)
int tomal_poll_napi(struct napi_struct * napi, int budget)  /*  struct net_device* netDev, int* budget) */
{
	struct net_device *netDev = napi->dev ;
#else
int tomal_poll(struct net_device *netDev, int budget)  /*  struct net_device* netDev, int* budget) */
{
#endif
        int rc;
        EMAC* emac = (EMAC*) netdev_priv(netDev);
	TOMAL* tomal = emac->tomal;
	U8 channel = emac->channel;
        RxDescSegment* segment = tomal->oldRxSegment[channel];
	register RxDesc* desc = segment->currDesc;
        register struct sk_buff** skb = segment->currSkb;
	register const U32 buffLen = tomal->rxBufferSize[channel];
	register const U32 skbSize = buffLen + 16;
	register U32 rxNumberOfReceivedFrames = in_be32(&tomal->regs[channel]->rxNumberOfReceivedFrames);
	register U32 framesToProcess = rxNumberOfReceivedFrames - tomal->numberOfReceivedFrames[channel];
	register U32 framesReceived = 0;
	register U32 bytesPosted = 0;
	register int quota = min(budget, (int) framesToProcess);

	PRINTK(DBG_TOMAL | DBG_LEVEL2 | DBG_NAPI, "entry - netDev=%p, budget=%d\n", netDev, budget);

/* #ifdef CONFIG_BGP_E10000_NAPI */
/* 	// Determine receive quota. */
/* 	if (quota > netDev->quota) */
/* 		quota = netDev->quota; */
/* #endif */

         /*  Iterate over the RX descriptors, starting with the oldest, processing each */
         /*  data buffer that has been received until the indicated number of frames */
         /*  have been processed. */
	while (likely((framesReceived < quota) && framesToProcess)) {
                 /*  Is the current descriptor describing a valid frame? */
		if (likely(desc->status & TOMAL_RX_LAST)) {
			PRINTK(DBG_TOMAL | DBG_LEVEL3 | DBG_NAPI, "Received %d bytes to skb %p\n", desc->totalFrameLength, *skb);
			if (likely((desc->status & TOMAL_RX_STATUS_CHECKSUM_VALID) &&
				   (desc->status & TOMAL_RX_STATUS_IP_CHECKSUM_PASSED) &&
				   (desc->status & TOMAL_RX_STATUS_TCP_UDP_CHECKSUM_PASSED)))
				 /*  Valid checksum. */
				(*skb)->ip_summed = CHECKSUM_UNNECESSARY;
			else
				(*skb)->ip_summed = CHECKSUM_NONE;
			skb_put(*skb, desc->totalFrameLength);
			(*skb)->dev = netDev;
			(*skb)->protocol = eth_type_trans(*skb, netDev);
#ifdef CONFIG_BGP_E10000_NAPI
		        PRINTK(DBG_NAPI, "netif_receive_skb\n");
			rc = netif_receive_skb(*skb);
#else
			rc = netif_rx(*skb);
#endif
			*skb = NULL;
			if (likely(rc == NET_RX_SUCCESS)) {
				framesReceived++;
				emac->stats.rx_bytes += desc->totalFrameLength;
			} else if (rc == NET_RX_DROP || rc == NET_RX_BAD) {
				emac->stats.rx_dropped++;
				tomal->numberOfNetrxDrops ++ ;
			} else
				emac->stats.rx_errors++;
		} else {
			tomal->numberOfNotLast++ ;
		}

		 /*  Make the current slot in the Rx ring useable again. */
		if (likely(*skb == NULL)) {
			*skb = alloc_skb(skbSize, GFP_ATOMIC);
			if (likely(*skb)) {
				skb_reserve(*skb, 2);  /*  align */
				desc->buffHeadAddrL = dma_map_single(NULL, (*skb)->data, buffLen, DMA_FROM_DEVICE);
				desc->postedLength = buffLen;
				bytesPosted += desc->postedLength;
			} else
				desc->postedLength = desc->buffHeadAddrL = 0;
		} else     /*  Reinitialize this descriptor */
			bytesPosted += desc->postedLength;   /*  descriptor avaialable again so repost */
		desc->status = 0;

		 /*  Post additional buffers to the device if we've accumulated enough. */
		if (unlikely(bytesPosted >= 0xffff)) {
			out_be32(&tomal->regs[channel]->rxAddFreeBytes, 0xffff);
			bytesPosted -= 0xffff;
		}

		skb++;
		desc++;
		framesToProcess--;

		 /*  Have we reached the end of the segment? */
		if (unlikely(desc->code != TOMAL_RX_DESC_CODE)) {
		         /*  Move to the next segment. */
			segment->currDesc = segment->desc;
                        segment->currSkb = segment->skb;
	                tomal->oldRxSegment[channel] = segment = segment->next;
		        desc = segment->currDesc;
			skb = segment->currSkb;
                }
	}

	 /*  Post any remaining buffers to the device. */
        if (likely(bytesPosted))
              out_be32(&tomal->regs[channel]->rxAddFreeBytes, bytesPosted);

	 /*  Update segment information and statistics. */
	segment->currDesc = desc;
	segment->currSkb = skb;
	emac->stats.rx_packets += framesReceived;
	tomal->numberOfReceivedFrames[channel] = rxNumberOfReceivedFrames - framesToProcess;

         /*  Reset the Rx notification mechanism. */
        out_be32(&tomal->regs[0]->rxNotificationCtrl, (channel ? TOMAL_RX_NOTIFY_CTRL_COUNTER_START1 : TOMAL_RX_NOTIFY_CTRL_COUNTER_START0));

#ifdef CONFIG_BGP_E10000_NAPI
/*         netDev->quota -= framesReceived; */
        budget -= framesReceived;
        if (framesReceived == quota) {
                 /*  We processed all frames within the specified quota.  Reenable interrupts */
		 /*  and tell the kernel that we received everything available. */
		U32 swNonCriticalErrorsMask = in_be32(&tomal->regs[0]->swNonCriticalErrorsMask);
	        PRINTK(DBG_NAPI, "napi_complete\n");
		napi_complete(napi) ;
                out_be32(&tomal->regs[channel]->rxEventMask, TOMAL_RX_EVENT);
		if (!(swNonCriticalErrorsMask & TOMAL_SW_NONCRIT_ERRORS_RTSDB))
			out_be32(&tomal->regs[0]->swNonCriticalErrorsMask,
				 swNonCriticalErrorsMask | TOMAL_SW_NONCRIT_ERRORS_RTSDB);
                rc = 0;
        } else
		rc = 1;
#else
	rc = 0;
#endif

        PRINTK(DBG_TOMAL | DBG_LEVEL2 | DBG_NAPI, "exit - rc=%d\n", rc);

        return rc;
}

static inline U16 * frame_checksum_ptr(struct sk_buff* skb)
{
	struct ethhdr *eth = (struct ethhdr *)skb->data;
        unsigned int eth_proto = eth->h_proto ;
        struct iphdr *iph = (struct iphdr *)((skb->data)+sizeof(struct ethhdr)) ;
        unsigned int iphlen = 4*iph->ihl ;
        struct tcphdr *tcph = (struct tcphdr *) ( ((char *)(iph)) + (iphlen) );
        struct udphdr *udph = (struct udphdr *) ( ((char *)(iph)) + (iphlen) );
        unsigned int ip_proto = iph->protocol ;
        if( eth_proto == ETH_P_IP) {
		if( ip_proto == IPPROTO_TCP) return &(tcph->check) ;
		if( ip_proto == IPPROTO_UDP) return &(udph->check) ;
        }
        return NULL ;

}
/*  Transmit a frame. */
/*  Caller should be holding the TOMAL lock for the specified channel. */
int tomal_xmit_tx_buffer(TOMAL* tomal,
			 U8 channel,
                         struct sk_buff* skb)
{
	int rc = 0;
	int nr_frags = skb_shinfo(skb)->nr_frags;
	int f = -1;
	TxDescSegment* segment = tomal->freeTxSegment[channel];
	U32 framesToProcess;
	U32 buffLen;
	dma_addr_t buffAddr;

	PRINTK(DBG_TOMAL | DBG_LEVEL2, "entry - tomal=%p, skb=%p, channel=%d\n", tomal, skb, channel);

	do {
		 /*  Are we at the end of the segment? */
		if (unlikely(segment->desc[segment->freeIndex].code == 0x20)) {
			segment->freeIndex = 0;
			tomal->freeTxSegment[channel] = segment = segment->next;
		}

		 /*  Point the next free descriptor(s) at the SKB buffer(s).  The first buffer is a special case. */
		if (f < 0) {
			 /*  The data is in the skb's data buffer. */
			buffLen = skb->len - skb->data_len;
			buffAddr = dma_map_single(NULL, skb->data, buffLen, DMA_TO_DEVICE);
/* 			tomal->count_tx_checksum_type[skb->ip_summed] += 1 ; */
#ifdef CONFIG_BGP_E10000_IP_CHECKSUM
			 /*  When using the IO node as a router (collective --> ethernet ) frames are coming across marked CHECKSUM_COMPLETE */
			 /*  even though I think they should be marked CHECKSUM_PARTIAL. Use the TOMAL checksumming hardware on the frames. */
/* 			if (skb->ip_summed == CHECKSUM_PARTIAL) */
			if( 1)
			{
				 /*  Generate IP checksum for this frame. */
				U16 * frame_ck_ptr=frame_checksum_ptr(skb) ;
				if( frame_ck_ptr ) *frame_ck_ptr = 0 ;
/* 				if( frame_ck_ptr && frame_ck_ptr != (U16*)(skb->head+skb->csum_start + skb->csum_offset)) */
/* 					{ */
/* 						printk(KERN_INFO "(E) frame_ck_ptr=%p skb->head=%p skb->csum_start=%d skb->csum_offset=%d\n", */
/* 								frame_ck_ptr,skb->head,skb->csum_start,skb->csum_offset) ; */
/* 					} */
/* 				*(U16*)(skb->head+skb->csum_start + skb->csum_offset) = 0; */
				segment->desc[segment->freeIndex].command = TOMAL_TX_ENABLE_HW_CHECKSUM |
						TOMAL_TX_GENERATE_FCS | TOMAL_TX_GENERATE_PAD;
			} else  {
				segment->desc[segment->freeIndex].command = TOMAL_TX_GENERATE_FCS | TOMAL_TX_GENERATE_PAD;
			}
#else
                                segment->desc[segment->freeIndex].command = TOMAL_TX_GENERATE_FCS | TOMAL_TX_GENERATE_PAD;
#endif

		} else {
			struct skb_frag_struct* frag = &skb_shinfo(skb)->frags[f];

			 /*  Map the page that contains the current fragment. */
			buffAddr = dma_map_page(NULL, frag->page, frag->page_offset, frag->size, DMA_TO_DEVICE);
			buffLen = frag->size;
		}

		segment->desc[segment->freeIndex].wBStatus = 0;
		segment->desc[segment->freeIndex].postedLength = buffLen;
		segment->desc[segment->freeIndex].buffHeadAddrL = (U32) buffAddr;
		segment->desc[segment->freeIndex].code = TOMAL_TX_DESC_CODE;
		if (f == (nr_frags - 1)) {   /*  Last buffer? */
			segment->desc[segment->freeIndex].code |=  TOMAL_TX_NOTIFY_REQ | TOMAL_TX_SIGNAL | TOMAL_TX_LAST;
			segment->skb[segment->freeIndex] = skb;

			 /*  Post buffer(s) for transmission. */
			PRINTK(DBG_TOMAL | DBG_LEVEL3, "Enqueueing buffer 0x%08x for xmit, index=%d, desc=%p, len=%d, code=0x%x\n",
				(U32) buffAddr, segment->freeIndex, &segment->desc[segment->freeIndex], segment->desc[segment->freeIndex].postedLength,
				segment->desc[segment->freeIndex].code);
			smp_wmb();
			out_be32(&tomal->regs[channel]->txAddPostedFrames, 1);
		}

		 /*  Advance to the next free descriptor index. */
		segment->freeIndex++;
		f++;
	} while (f < nr_frags);
	tomal->pendingTxBuffers[channel] += f+1;

	 /*  Clean up any buffers for frames that have been transmitted. */
	framesToProcess = in_be32(&tomal->regs[channel]->txNumberOfTransmittedFrames) - tomal->numberOfTransmittedFrames[channel];
	if (unlikely(framesToProcess > 32)) {
		int bufsProcessed = tomal_process_tx_buffers(tomal, channel, framesToProcess);
		if (unlikely(bufsProcessed < 0))
			printk(KERN_WARNING "%s: Error processing TX buffers [%d]\n",
				tomal->netDev[channel]->name, bufsProcessed);
	}

	 /*  Stop the queue if we lack the space to transmit another frame. */
	if (unlikely((tomal->pendingTxBuffers[channel] + MAX_SKB_FRAGS + 1) >
			tomal->maxTxBuffers[channel]))
		netif_stop_queue(tomal->netDev[channel]);

	tomal->netDev[channel]->trans_start = jiffies;

	PRINTK(DBG_TOMAL | DBG_LEVEL2, "exit - rc=%d\n", rc);

	return rc;
}



void tomal_exit(TOMAL* tomal)
{
	int c;

        PRINTK(DBG_TOMAL | DBG_LEVEL2, "entry\n");

	if (tomal) {
	         /*  Release interrupt handlers. */
		free_irq(TOMAL_IRQ0, tomal);
	        free_irq(TOMAL_IRQ1, tomal);

		 /*  Free descriptor segments for each channel. */
	        for (c = 0; c < TOMAL_MAX_CHANNELS; c++) {
	                tomal_free_rx_segments(tomal, c);
		        tomal_free_tx_segments(tomal, c);

			 /*  Unregister and free net_device */
			if (tomal->netDev[c]) {
				EMAC* emac = netdev_priv(tomal->netDev[c]);

				 /*  Allow EMAC to cleanup. */
				if (emac)
					emac_exit(emac);

				unregister_netdev(tomal->netDev[c]);
				free_netdev(tomal->netDev[c]);
			}
		}

		 /*  Remove /proc entries. */
		if (tomal->tomalDir) {
			if (tomal->hwDir) {
				E10000_PROC_ENTRY* entry = tomal_hw_proc_entry;

				while (entry->name) {
					if (entry->entry) {
						remove_proc_entry(entry->entry->name, tomal->hwDir);
						entry->entry = NULL;
					}
                                        entry++;
				}

				remove_proc_entry(tomal->hwDir->name, tomal->tomalDir);
				tomal->hwDir = NULL;
			}
			if (tomal->swDir) {
				E10000_PROC_ENTRY* entry = tomal_sw_proc_entry;
				while (entry->name) {
					if (entry->entry) {
						remove_proc_entry(entry->entry->name, tomal->swDir);
						entry->entry = NULL;
					}
					entry++;
				}

				remove_proc_entry(tomal->swDir->name, tomal->tomalDir);
				tomal->swDir = NULL;
			}

			remove_proc_entry(tomal->tomalDir->name, tomal->parentDir);
			tomal->tomalDir = NULL;
		}

		 /*  Free the TOMAL object. */
		kfree((void*) tomal);
        }

        PRINTK(DBG_TOMAL | DBG_LEVEL2, "exit\n");

        return;
}


/*  Reset and reconfigure the TOMAL hardware and reinitialize Rx descriptors. */
int tomal_soft_reset(TOMAL* tomal)
{
	int rc = 0;
	int c;

	PRINTK(DBG_TOMAL | DBG_LEVEL2, "entry - tomal=%p\n", tomal);

	 /*  Reset TOMAL and wait for it to finish. */
	out_be32(&tomal->regs[0]->configurationCtrl, TOMAL_CFG_CTRL_SOFT_RESET);
	for (c = 100; (in_be32(&tomal->regs[0]->configurationCtrl) & TOMAL_CFG_CTRL_SOFT_RESET) && c; c--)
		udelay(10000);
	if (!c) {
		e10000_printr(bg_subcomp_tomal, tomal_ras_timeout,
				"TOMAL reset failure.");
		rc = -ETIME;
	} else {
		 /*  Reset EMAC(s) and free any buffers. */
		for (c = 0; c < TOMAL_MAX_CHANNELS; c++)
			if (tomal->netDev[c]) {
				 /*  Free any RX and TX buffers. */
				tomal_free_rx_buffers(tomal, c);
				tomal_free_tx_buffers(tomal, c);

				 /*  Free descriptor segments */
				tomal_free_rx_segments(tomal, c);
				tomal_free_tx_segments(tomal, c);
			}

		 /*  Reconfigure TOMAL. */
		rc = tomal_configure(tomal);
	}

	PRINTK(DBG_TOMAL | DBG_LEVEL2, "exit - rc=%d\n", rc);

	return rc;
}
