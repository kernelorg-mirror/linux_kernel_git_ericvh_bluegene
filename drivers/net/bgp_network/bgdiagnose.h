/*
 * bgdiagnose.h
 *
 * Diagnostic routines for 450/BGP bringup
 *
 */
#ifndef __DRIVERS__NET__BLUEGENE__BGDIAGNOSE_H__
#define __DRIVERS__NET__BLUEGENE__BGDIAGNOSE_H__
/* #include <asm/bluegene.h> */

#include <linux/kernel.h>
/* #include <asm/bgp_personality.h> */
#include <asm/bluegene_ras.h>
#include "450_tlb.h"

/* static BGP_Personality_t* bgp_personality ; */

/* static void show_personality_kernel(BGP_Personality_Kernel_t * Kernel_Config) */
/* { */
/* 	printk(KERN_INFO "show_personality_kernel L1Config=0x%08x L2Config=0x%08x L3Config=0x%08x L3Select=0x%08x FreqMHz=%d NodeConfig=0x%08x\n", */
/* 			Kernel_Config->L1Config, */
/* 			Kernel_Config->L2Config, */
/* 			Kernel_Config->L3Config, */
/* 			Kernel_Config->L3Select, */
/* 			Kernel_Config->FreqMHz, */
/* 			Kernel_Config->NodeConfig) ; */
/*  */
/* } */
/* static void show_personality(void) */
/* { */
/* //	bgp_personality = bgcns()->getPersonalityData(); */
/* //	show_personality_kernel(&bgp_personality->Kernel_Config) ; */
/* } */

static const char* TLB_SIZES[] = {
    "  1K",  /*  0 */
    "  4K",
    " 16K",
    " 64K",
    "256K",
    "  1M",
    "?-6?",
    " 16M",
    "?-8?",
    "256M",
    "  1G",
    "?11?",
    "?12?",
    "?13?",
    "?14?",
    "?15?"
};

#include "450_tlb.h"


static void show_tlbs(unsigned int vaddr) __attribute__ ((unused)) ;
static void show_tlbs(unsigned int vaddr) {

    int i;
    uint32_t t0, t1, t2;
    int tlb_index = search_tlb(vaddr) ;
    for (i = 0; i < 64; i++) {
	    t0 = get_tlb_pageid(i) ;
	    t1 = get_tlb_xlat(i) ;
	    t2 = get_tlb_attrib(i) ;
/* 	_bgp_mftlb(i,t0,t1,t2); */
/* 	if (t0 & TLB0_V) { */
	{
	    printk(KERN_INFO
		"TLB 0x%02x %08x-%08x-%08x EPN=%08x RPN=%01x-%08x size=%s WIMG=%d%d%d%d U=%d%d%d%d V=%d\n",
		i,
		t0, t1, t2,
		TLB0_EPN_1K(t0),
		TLB1_ERPN(t1),TLB1_RPN_1K(t1),
		TLB_SIZES[(t0 & 0xF0) >> 4],
		(t2 & TLB2_W) ? 1 : 0,
		(t2 & TLB2_I) ? 1 : 0,
		(t2 & TLB2_M) ? 1 : 0,
		(t2 & TLB2_G) ? 1 : 0,
		(t2 & TLB2_U0) ? 1 : 0,
		(t2 & TLB2_U1) ? 1 : 0,
		(t2 & TLB2_U2) ? 1 : 0,
		(t2 & TLB2_U3) ? 1 : 0,
		(t0 & TLB0_V) ? 1 : 0
		);
	}
    }
    printk(KERN_INFO "vaddr=0x%08x tlb_index=%d\n", vaddr,tlb_index) ;
}

static void show_tlb_for_vaddr(unsigned int vaddr) __attribute__ ((unused)) ;
static void show_tlb_for_vaddr(unsigned int vaddr)
{
	    int i = search_tlb(vaddr) & 0x3f ;
	    uint32_t t0 = get_tlb_pageid(i) ;
	    uint32_t t1 = get_tlb_xlat(i) ;
	    uint32_t t2  = get_tlb_attrib(i) ;
	    printk(KERN_INFO
		"TLB 0x%02x %08x-%08x-%08x EPN=%08x RPN=%01x-%08x size=%s WIMG=%d%d%d%d U=%d%d%d%d V=%d\n",
		i,
		t0, t1, t2,
		TLB0_EPN_1K(t0),
		TLB1_ERPN(t1),TLB1_RPN_1K(t1),
		TLB_SIZES[(t0 & 0xF0) >> 4],
		(t2 & TLB2_W) ? 1 : 0,
		(t2 & TLB2_I) ? 1 : 0,
		(t2 & TLB2_M) ? 1 : 0,
		(t2 & TLB2_G) ? 1 : 0,
		(t2 & TLB2_U0) ? 1 : 0,
		(t2 & TLB2_U1) ? 1 : 0,
		(t2 & TLB2_U2) ? 1 : 0,
		(t2 & TLB2_U3) ? 1 : 0,
		(t0 & TLB0_V) ? 1 : 0
		);

}
static inline unsigned  int move_from_spr(unsigned int sprNum)
  {
    unsigned long sprVal = 0;

    asm volatile ("mfspr %0,%1\n" : "=r"(sprVal) : "i" (sprNum));

    return sprVal;

  }
static inline void show_spr(unsigned int spr, const char *name)
  {
    printk(KERN_INFO "%s[%03x] = 0x%08x\n",name,spr, move_from_spr(spr)) ;
  }

static inline unsigned int move_from_dcr(unsigned int dcrNum)
{
  unsigned long dcrVal = 0;

  asm volatile("mfdcrx %0,%1": "=r" (dcrVal) : "r" (dcrNum) : "memory");

  return dcrVal;
}

static inline unsigned int move_from_msr(void)
{
  unsigned long msrVal = 0;

  asm volatile("mfmsr %0" : "=r" (msrVal) : : "memory");

  return msrVal;
}

static inline void show_msr(void)
  {
    printk(KERN_INFO "MSR = 0x%08x\n",move_from_msr()) ;
  }

static void show_dcr_range(unsigned int start, unsigned int length) __attribute__ ((unused)) ;
static void show_dcr_range(unsigned int start, unsigned int length)
  {
    unsigned int x ;
    for( x=0;x<length;x+=8 )
      {
        unsigned int dcrx=start+x ;
        printk(KERN_INFO "dcr[%04x]=[%08x %08x %08x %08x %08x %08x %08x %08x]\n",
            start+x,
            move_from_dcr(dcrx),move_from_dcr(dcrx+1),move_from_dcr(dcrx+2),move_from_dcr(dcrx+3),
            move_from_dcr(dcrx+4),move_from_dcr(dcrx+5),move_from_dcr(dcrx+6),move_from_dcr(dcrx+7)
            ) ;
      }
  }
static void show_sprs(void) __attribute__ ((unused)) ;
static void show_sprs(void)
{
    show_msr() ;
    show_spr(0x3b3,"CCR0") ;
    show_spr(0x378,"CCR1") ;
    show_spr(0x3b2,"MMUCR") ;
    show_spr(0x39b,"RSTCFG") ;
/*     show_dcr_range(0x500,32) ; // _BGP_DCR_L30 */
/*     show_dcr_range(0x540,32) ; // _BGP_DCR_L31 */
/*     show_dcr_range(0xd00,16) ; // _BGP_DCR_DMA */

  }

#endif
