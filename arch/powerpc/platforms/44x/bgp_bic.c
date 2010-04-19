/*
 * Blue Gene/P interrupt controller
 *
 * Linux wants IRQs mapped to a small integer space.
 *
 * The bic defines 15 groups and 32 group interrupts in each group.
 * We encode an IRQ number like this (which requires NR_IRQS=512):
 *    GGGGIIIII
 *   where GGGG is the 4-bit group number+1 (i.e. GGGG=0000 is not used),
 *   and IIIII is the 5-bit interrupt index within the 32-bit word.
 * The interrupt indexes are numbered from the left bit (powerpc-style).
 * We avoid encoding GGGG=0000 so we never end up with an IRO=0 which is a
 * flag for "no interrupt" in arch/powerpc.
 *
 * The IPIs subdivide the group 0 interrupt word as follows:
 *
 *  CRSD CRSD CRSD CRSD .... .... .... ....
 *  0    4    8    12   16   20   24   28
 *  cpu0 cpu1 cpu2 cpu3

 * where C=call, R=resched, S=call-single, D=debug, and .=unused
 *
 * We encode IPI IRQ numbers specially.   By the above encoding they would be
 * 32..47 for these 16 bits.
 *
 * The other 16 bits in group 0 are treated normally.   These will translate to
 * IRQ = 48..63 and can be used by software to simulate hardware interrupts for
 * other purposes.
 *
 *
 * Todd Inglett <tinglett@us.ibm.com>
 * Copyright 2003-2009 International Business Machines, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <asm/bluegene.h>

/* #define TJCW_USE_BYTEWISE */
/* #define BIC_DIAGNOSE 1 */

#if defined(BIC_DIAGNOSE)
extern int bgp_dma_tcp_tracemask ;
static int bic_diagnose_count  ;
enum {
	k_bic_diagnose_limit = 100
};
static unsigned int bic_diagnosing(void)
{
	if( 0 == (bgp_dma_tcp_tracemask & 0x80000000) )
		{
		if( bic_diagnose_count < k_bic_diagnose_limit)
			{
				bic_diagnose_count += 1 ;
				return 1 ;
			}
		}
	else
		{
			bic_diagnose_count = 0 ;
		}
	return 0 ;
}
#define BIC_DIAG(X) if(bic_diagnosing()) { X ; }
#else
#define BIC_DIAG(X)
#endif

void bic_unmask_irq(unsigned int irq);
EXPORT_SYMBOL(bic_unmask_irq) ;
static void bic_mask_irq(unsigned int irq);
static void bic_eoi_irq(unsigned int irq);

static void bic_unmask_irq_bytewise(unsigned int irq) __attribute__((unused)) ;

static void bic_mask_irq_bytewise(unsigned int irq) __attribute__((unused)) ;
#if defined(TJCW_USE_BYTEWISE)
static struct irq_chip bgp_irq_chip = {
	.name		= "BIC",
	.unmask		= bic_unmask_irq_bytewise,
	.mask		= bic_mask_irq_bytewise,
	.eoi		= bic_eoi_irq,
};
#else
static struct irq_chip bgp_irq_chip = {
	.name		= "BIC",
	.unmask		= bic_unmask_irq,
	.mask		= bic_mask_irq,
	.eoi		= bic_eoi_irq,
};
#endif


/* Note that the BIC (and other devices) are at phys addresses > 4GB */
#define BIC_PHYS 0x730000000LL

/* These are defined by the hardware. */
#define NR_BIC_GROUPS 15
#define NR_BIC_GINTS 32
#define NR_BIC_CPUS 4

/* 4-bit target value for target register */
#define BIC_TARGET_MASK (0xf)
#define BIC_TARGET_TYPE_NORMAL (1<<2)
#define BIC_TARGET_NORMAL(cpu) (BIC_TARGET_TYPE_NORMAL|(cpu))
#define BIC_DEFAULT_CPU 0
#define BIC_IPI_GROUP 0

/* Define the layout of each group's registers.
 * This layout should be 0x80 bytes long (including pad).
 */
struct bic_group_regs {
	uint32_t status;			/* 0x00  RW */
	uint32_t rd_clr_status;			/* 0x04  RO */
	uint32_t status_clr;			/* 0x08  WO */
	uint32_t status_set;			/* 0x0c  WO */
	uint32_t target[4];			/* 0x10  RW */
	uint32_t normal[NR_BIC_CPUS];		/* 0x20  RW */
	uint32_t critical[NR_BIC_CPUS];		/* 0x30  RW */
	uint32_t mcheck[NR_BIC_CPUS];		/* 0x40  RW */
	uint32_t _pad[12];			/* 0x50     */
};

/* Define the layout of the interrupt controller mem mapped regs. */
struct bic_regs {
	struct bic_group_regs group[NR_BIC_GROUPS];		/* 0x000 */
	uint32_t hier_normal[NR_BIC_CPUS];			/* 0x780 */
	uint32_t hier_critical[NR_BIC_CPUS];			/* 0x790 */
	uint32_t hier_mcheck[NR_BIC_CPUS];			/* 0x7a0 */
};

/*  This table is indexed by 'real' IRQ, i.e. BIC values. Linux 'virtual' IRQs are +32 */
static volatile unsigned char intended_cpu_for_irq[NR_BIC_GROUPS*NR_BIC_GINTS] =
  {
/*  0 */
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(1),BIC_TARGET_NORMAL(1),BIC_TARGET_NORMAL(1),BIC_TARGET_NORMAL(1),
      BIC_TARGET_NORMAL(2),BIC_TARGET_NORMAL(2),BIC_TARGET_NORMAL(2),BIC_TARGET_NORMAL(2),
      BIC_TARGET_NORMAL(3),BIC_TARGET_NORMAL(3),BIC_TARGET_NORMAL(3),BIC_TARGET_NORMAL(3),

      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
/*  32 */
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
/*  64 */
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
/*  128 */
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
/*  256 */
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),
      BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0),BIC_TARGET_NORMAL(0)
/*  480 */

  };

static inline void out_be8(unsigned char * target, unsigned int val)
{
	*target = val ;
}

static inline unsigned int in_be8(unsigned char * target)
{
	return *target ;
}

/* Group is encoded in the upper 4 bits.   We account for group+1. */
static inline unsigned bic_irq_to_hwgroup(unsigned irq)
{
	return ((irq >> 5) & 0xf) - 1;
}
/* Gint is encoded in the bottom 5 bits. */
static inline unsigned bic_irq_to_hwgint(unsigned irq)
{
	return irq & 0x1f;
}

static inline unsigned bic_irq_to_hwirq(unsigned irq)
{
	return irq - (1 << 5);
}

/* bic_hw_to_irq(unsigned group, unsigned gint) is in bluegene.h */
/* Need to keep a track in memory of where each interrupt is pointed at
 * so we can reassemble the right hardware register contents even with SMP behaviour
 */
static volatile unsigned char cpu_for_irq[NR_BIC_GROUPS*NR_BIC_GINTS] ;
static void set_cpu_for_hwirq(unsigned int hwirq, unsigned int tcpu)
  {
    cpu_for_irq[hwirq] = tcpu ;
  }

void bic_set_cpu_for_irq(unsigned int irq, unsigned int cpu)
  {
	  unsigned int hwirq=bic_irq_to_hwirq(irq) ;
    if( irq < NR_BIC_GROUPS*NR_BIC_GINTS )
      {
        intended_cpu_for_irq[hwirq] = BIC_TARGET_NORMAL(cpu) ;
      }
	BIC_DIAG(printk(KERN_INFO "bic_set_cpu_for_irq irq=0x%02x cpu=%d hwirq=0x%02x\n",
			irq,cpu,hwirq)) ;
  }

/*  Stop the BIC from passing an interrupt to the CPU. The idea is to */
/*  call this in a FLIH if you don't want a 'reinterrupt', and call */
/*  'bic_set_cpu_for_irq' later on (e.g. from a NAPI 'poll') */
void bic_disable_irq(unsigned int irq)
  {
    if( irq < NR_BIC_GROUPS*NR_BIC_GINTS )
      {
        intended_cpu_for_irq[bic_irq_to_hwirq(irq)] = 0 ;
      }
  }

EXPORT_SYMBOL(bic_disable_irq) ;

int bic_get_cpu_for_irq(unsigned int irq)
  {
    return intended_cpu_for_irq[bic_irq_to_hwirq(irq)] ;
  }


struct bic {
	spinlock_t mask_lock;	/* could be finer grained if necessary */
	struct bic_regs *regs;
	uint32_t enabled_mask[NR_BIC_GROUPS] ; /* Hardware can report status even if a bit doesn't cause interrupt. This to mask off ... */
} bic;


/* ipi_to_irq(cpu, msg)
 * Produce a Linux IRQ number given a cpu+func.
 * The caller ensures cpu in 0..3 and func in 0..3.
 */
static inline unsigned ipi_to_irq(unsigned cpu, unsigned func)
{
	return bic_hw_to_irq(BIC_IPI_GROUP, (cpu<<2)+func);
}
/* Generate a 4-bit IPI range mask for this cpu retaining the unused bits. */
static inline unsigned ipi_mask(unsigned cpu)
{
	return 0xf0000000U >> (cpu << 2) | 0x0000ffffU;
}
/* Given an gint we know is an IPI (0..15), return the cpu that
 * should be targeted.  Remember these bits are numbered from the left.
 */
static inline unsigned ipi_gint_cpu(unsigned gint)
{
	return (gint >> 2) & 0x3;
}
static inline int is_ipi(unsigned group, unsigned gint)
{
	return (group == 0) && (gint < 16);
}

#define GINT_TO_IRQ(group, gint) (((group) << 5) | (gint))
static unsigned int get_tcpu_for_tnum(unsigned int group, unsigned int tnum)
  {
    unsigned int rbase = GINT_TO_IRQ(group,(tnum<<3)) ;
    unsigned int t0 = cpu_for_irq[rbase+0] ;
    unsigned int t1 = cpu_for_irq[rbase+1] ;
    unsigned int t2 = cpu_for_irq[rbase+2] ;
    unsigned int t3 = cpu_for_irq[rbase+3] ;
    unsigned int t4 = cpu_for_irq[rbase+4] ;
    unsigned int t5 = cpu_for_irq[rbase+5] ;
    unsigned int t6 = cpu_for_irq[rbase+6] ;
    unsigned int t7 = cpu_for_irq[rbase+7] ;
    return ((t0 & 0x0f) << 28) |
           ((t1 & 0x0f) << 24) |
           ((t2 & 0x0f) << 20) |
           ((t3 & 0x0f) << 16) |
           ((t4 & 0x0f) << 12) |
           ((t5 & 0x0f) << 8) |
           ((t6 & 0x0f) << 4) |
           ((t7 & 0x0f)) ;

  }
static unsigned int get_tcpu_for_tnum_byte(unsigned int group, unsigned int tnum)
  {
    unsigned int rbase = GINT_TO_IRQ(group,(tnum<<1)) ;
    unsigned int t0 = cpu_for_irq[rbase+0] ;
    unsigned int t1 = cpu_for_irq[rbase+1] ;
    return ((t0 & 0x0f) << 4) |
           ((t1 & 0x0f)) ;

  }
/*
 * Unmasking an IRQ will enable it.
 * We reach into the bic to set the target core of the interrupt appropriately.
 * For now, interrupts are wired to a default core, although IPIs (of course)
 * must be directed appropriately.
 */
void bic_unmask_irq(unsigned int irq)
{
	unsigned group = bic_irq_to_hwgroup(irq);
	unsigned gint = bic_irq_to_hwgint(irq);
	unsigned tnum = gint >> 3;
	unsigned tidx = gint & 7;
/* 	unsigned orig, tmask, tcpu; */
	unsigned tmask, tcpu;
	uint32_t *targetp = &bic.regs->group[group].target[tnum];
	unsigned cpu;
	unsigned int request_tcpu ;
	unsigned int verify_tcpu ;

	spin_lock(&bic.mask_lock);
	bic.enabled_mask[group] |= 0x80000000 >> gint ;  /*  Note that this interrupt is enabled */
	spin_unlock(&bic.mask_lock);

	tmask= ~(0xf << (7-tidx)*4);

	if (group == 0 /*is_ipi(group, gint)*/) {
		/* These bits are magic.  We know they are for IPIs
		 * and must direct them to the correct core.
		 */
		cpu = ipi_gint_cpu(gint);
		tcpu = BIC_TARGET_NORMAL(cpu) << (7-tidx)*4;
	} else {
		cpu = BIC_DEFAULT_CPU;
		tcpu = BIC_TARGET_NORMAL(cpu) << (7-tidx)*4;
	}


	{
		unsigned int hwirq = bic_irq_to_hwirq(irq) ;
		unsigned int tgtcpu=intended_cpu_for_irq[hwirq] ;  /*  Note .. 'cpu' has the b'0100' bit set already if appropriate */
		set_cpu_for_hwirq(hwirq,tgtcpu) ;
		request_tcpu=get_tcpu_for_tnum(group,tnum) ;
/* 		BIC_DIAG(printk(KERN_INFO "bic_unmask_irq irq=0x%02x hwirq=0x%02x group=0x%02x tnum=0x%02x gint=0x%02x tmask=0x%08x targetp=%p cpu=%d tgtcpu=%d targtval=0x%08x request_tcpy=0x%08x\n", */
/* 				irq,hwirq,group,tnum,gint,tmask,targetp,cpu,tgtcpu,(orig & tmask)|tcpu, request_tcpu)) ; */
		BIC_DIAG(printk(KERN_INFO "bic_unmask_irq irq=0x%02x hwirq=0x%02x group=0x%02x tnum=0x%02x gint=0x%02x tmask=0x%08x targetp=%p cpu=%d tgtcpu=%d request_tcpy=0x%08x\n",
				irq,hwirq,group,tnum,gint,tmask,targetp,cpu,tgtcpu, request_tcpu)) ;

		out_be32(targetp, request_tcpu) ;
		verify_tcpu=get_tcpu_for_tnum(group,tnum) ;
		while(request_tcpu != verify_tcpu)
		{
			 /*  If another CPU changed the target for an interrupt while we were writing, pick up the change */
			 /*  and set the hw register appropriately. Eventually the last writer should reflect what */
			 /*  everyone wants. */
			request_tcpu = verify_tcpu ;
			printk(KERN_NOTICE "irq=0x%02x set=%x redo request_tcpu=%08x\n", irq,BIC_TARGET_NORMAL(cpu),request_tcpu) ;
			out_be32(targetp, request_tcpu) ;
			verify_tcpu=get_tcpu_for_tnum(group,tnum) ;
		}

	}

}
static void bic_unmask_irq_bytewise(unsigned int irq)
{
	unsigned group = bic_irq_to_hwgroup(irq);
	unsigned gint = bic_irq_to_hwgint(irq);
	unsigned tnum = gint >> 1;
	unsigned tidx = gint & 1;
/* 	unsigned orig, tmask, tcpu; */
	unsigned tmask;
	unsigned char *basep = (unsigned char *)(bic.regs->group[group].target) ;
	unsigned char *targetp = basep+tnum ;
	unsigned cpu;
	unsigned int request_tcpu ;
	unsigned int verify_tcpu ;

	spin_lock(&bic.mask_lock);
	bic.enabled_mask[group] |= 0x80000000 >> gint ;  /*  Note that this interrupt is enabled */
	spin_unlock(&bic.mask_lock);

	tmask= ~(0xf << (1-tidx)*4);

	if (group == 0 /*is_ipi(group, gint)*/) {
		/* These bits are magic.  We know they are for IPIs
		 * and must direct them to the correct core.
		 */
		cpu = ipi_gint_cpu(gint);
	} else {
		cpu = BIC_DEFAULT_CPU;
	}


	{
		unsigned int hwirq = bic_irq_to_hwirq(irq) ;
		unsigned int tgtcpu=intended_cpu_for_irq[hwirq] ;  /*  Note .. 'cpu' has the b'0100' bit set already if appropriate */
		set_cpu_for_hwirq(hwirq,tgtcpu) ;
		request_tcpu=get_tcpu_for_tnum_byte(group,tnum) ;
/* 		BIC_DIAG(printk(KERN_INFO "bic_unmask_irq irq=0x%02x hwirq=0x%02x group=0x%02x tnum=0x%02x gint=0x%02x tmask=0x%08x targetp=%p cpu=%d tgtcpu=%d targtval=0x%08x request_tcpy=0x%08x\n", */
/* 			irq,hwirq,group,tnum,gint,tmask,targetp,cpu,tgtcpu,(orig & tmask)|tcpu, request_tcpu)) ; */
		BIC_DIAG(printk(KERN_INFO "bic_unmask_irq irq=0x%02x hwirq=0x%02x group=0x%02x tnum=0x%02x gint=0x%02x tmask=0x%08x targetp=%p cpu=%d tgtcpu=%d request_tcpy=0x%08x\n",
			irq,hwirq,group,tnum,gint,tmask,targetp,cpu,tgtcpu, request_tcpu)) ;

		out_be8(targetp, request_tcpu) ;
		verify_tcpu=get_tcpu_for_tnum_byte(group,tnum) ;
		while(request_tcpu != verify_tcpu)
		{
			 /*  If another CPU changed the target for an interrupt while we were writing, pick up the change */
			 /*  and set the hw register appropriately. Eventually the last writer should reflect what */
			 /*  everyone wants. */
			request_tcpu = verify_tcpu ;
			printk(KERN_NOTICE "irq=0x%02x set=%x redo request_tcpu=%08x\n", irq,BIC_TARGET_NORMAL(cpu),request_tcpu) ;
			out_be8(targetp, request_tcpu) ;
			verify_tcpu=get_tcpu_for_tnum_byte(group,tnum) ;
		}

	}

}

/*
 * Masking an IRQ will disable it.
 * We do this by changing the target to disable.   This works for IPI bits,
 */
static void bic_mask_irq(unsigned int irq)
{
	unsigned group = bic_irq_to_hwgroup(irq);
	unsigned gint = bic_irq_to_hwgint(irq);
	unsigned tnum = gint >> 3;
	unsigned tidx = gint & 7;
	unsigned orig, tmask;
	uint32_t *targetp = &bic.regs->group[group].target[tnum];

	tmask = BIC_TARGET_MASK << (7-tidx)*4;
	BIC_DIAG(printk(KERN_INFO "bic_mask_irq irq=0x%02x group=0x%02x gint=0x%02x tmask=0x%02x\n",
			irq,group,gint,tmask)) ;
	spin_lock(&bic.mask_lock);
	bic.enabled_mask[group] &= 0xffffffff ^ (0x80000000 >> gint) ;  /*  Note that this interrupt is disabled */
	orig = in_be32(targetp);
	out_be32(targetp, orig & ~tmask);
	spin_unlock(&bic.mask_lock);
}

static void bic_mask_irq_bytewise(unsigned int irq)
{
	unsigned int hwirq = bic_irq_to_hwirq(irq) ;
	unsigned group = bic_irq_to_hwgroup(irq);
	unsigned gint = bic_irq_to_hwgint(irq);
	unsigned tnum = gint >> 1;
	unsigned tidx = gint & 1;
	unsigned orig, tmask;
	unsigned char *basep = (unsigned char *)(bic.regs->group[group].target) ;
	unsigned char *targetp = basep+tnum ;

	set_cpu_for_hwirq(hwirq,0) ;
	tmask = BIC_TARGET_MASK << ((1-tidx)*4);
	BIC_DIAG(printk(KERN_INFO "bic_mask_irq irq=0x%02x group=0x%02x gint=0x%02x tmask=0x%02x\n",
			irq,group,gint,tmask)) ;
	spin_lock(&bic.mask_lock);
	bic.enabled_mask[group] &= 0xffffffff ^ (0x80000000 >> gint) ;  /*  Note that this interrupt is disabled */
	orig = in_be8(targetp);
	out_be8(targetp, orig & ~tmask);
	spin_unlock(&bic.mask_lock);
}

/*
 * End an interrupt.   We just need to write the bit to be cleared
 * and the hardware handles it.   No locking needed.
 */
static void bic_eoi_irq(unsigned int irq)
{
	unsigned group = bic_irq_to_hwgroup(irq);
	unsigned gint = bic_irq_to_hwgint(irq);
	uint32_t gintbits = 1 << (31 - gint);
/* 	BIC_DIAG(printk(KERN_INFO "bic_eoi_irq irq=0x%02x group=0x%02x gint=0x%02x \n",irq,group,gint)) ; */

	out_be32(&bic.regs->group[group].status_clr, gintbits);
	mb();
}

/* Return the hardware cpu index as needed by the bic.
 * Currently this matches smp_processor_id(), but we do this explicitly
 * in case we ever want to virtualize the processor id.
 */
static inline unsigned this_cpu(void)
{
	unsigned cpu;
	asm volatile("mfspr %0, 0x11e" : "=r" (cpu));
	return cpu;
}

/* Return 0..32 counting from the left (same as bic).  32=> no bit set.
 * Could use bitops.h as long as it always matches the bic.
 */
static inline unsigned bic_find_first_bit(unsigned x)
{
    unsigned lz;
    asm("cntlzw %0,%1" : "=r" (lz) : "r" (x));
    return lz;
}

/*
 * Get an IRQ from the BIC.
 * We analyze the normal hierarchy register to find which group has caused an
 * interrupt.   Similarily, we find the first bit within a group to find the first
 * source of interrupt.   This artificially prioritizes interrupts.
 *
 * We handle IPIs specially.   This core can see IPI bits which did not actually
 * interrupt this core.   We mask off those bits and otherwise process normally.
 */
unsigned int bgp_get_irq(void)
{
	unsigned thiscpu = this_cpu();
	unsigned nhier, group, gint;
	uint32_t gintbits;
	int irq = NO_IRQ;

	nhier = in_be32(&(bic.regs->hier_normal[thiscpu]));
	group = bic_find_first_bit(nhier);
	if (group >= NR_BIC_GROUPS)
		goto out;
		{
			gintbits = in_be32(&bic.regs->group[group].status) & bic.enabled_mask[group] ;
			if (group == BIC_IPI_GROUP) {
				/* This may be an IPI.  Mask out other cpu IPI bits so we don't try
				 * to handle it on this core!   We don't mask the other 16 bits.
				 */
				unsigned mask = ipi_mask(thiscpu);
				gintbits &= mask;
			}
			gint = bic_find_first_bit(gintbits);
		}
	if (gint >= NR_BIC_GINTS)
		goto out;
	irq = bic_hw_to_irq(group, gint);
out:
/* 	BIC_DIAG(printk(KERN_INFO "bgp_get_irq nhier=0x%02x group=0x%02x gintbits=0x%08x gint=0x%02x irq=0x%02x\n", */
/* 			nhier,group,gintbits,gint,irq)) ; */
	return irq;
}

#ifdef CONFIG_SMP
/*
 * Send an IPI to another cpu.
 * This could be coded to send to a cpu mask.
 */
enum {
	k_spinlimit = 1000000 ,
	k_reportlimit = 100
};
static unsigned int reportcount  ;
void bgp_send_ipi(int cpu, int msg)
{
    unsigned group = BIC_IPI_GROUP;
    unsigned gint = ipi_to_irq(cpu, msg) & 0x1f;
    uint32_t gintbits = 1 << (31 - gint);
    uint32_t ngintbits;
    unsigned int spincount = 0 ;

    /* If this interrupt is already raised we must wait for it to complete else
     * we might race with the ack by the other waiting cpu.
     * Once it is clear there is no guarantee another cpu won't take it in tandem
     * with this cpu.  Currently that is ok, because a reschedule race is harmless
     * as the goal of rescheduling is met, and the others hold a lock while the
     * operation is in progress.  Why doesn't the lock protect us?  There is a window
     * between the lock release and the IPI interrupt ack where we will race.
     * This plugs the race.  It may be better to reallocate the IPI bits for unique
     * core-to-core combinations.
     */
    do {
	    spincount += 1 ;
	    ngintbits = in_be32(&bic.regs->group[group].status);
    } while ( (ngintbits & gintbits) && (spincount < k_spinlimit) ) ;

    /* Pull the interrupt. */
    if( spincount < k_spinlimit)
	    {
		    out_be32(&bic.regs->group[group].status_set, gintbits);
	    }
    else
	    {
		    if(reportcount < k_reportlimit)
			    {
				    printk(KERN_WARNING "bgp_send_ipi cpu=%d msg=%d stuck\n", cpu, msg) ;
				    reportcount += 1;
			    }
	    }
}

/* Initialize an IPI handler.   This is only here to use ipi_to_irq(), which
 * could be exposed in bluegene.h.
 */
void bgp_init_IPI(int cpu, int msg)
{
	smp_request_message_ipi(ipi_to_irq(cpu, msg), msg);
}
#endif

/* Initialize the bic.
 * We set the handlers as percpu because bic interrupts are wired
 * to specific cores (we never broadcast to all cores).
 */
static void __init disable_all_bic_interrupts(void)
{
	int group ;
	struct bic_regs * regs = bic.regs ;
	for(group=0; group<NR_BIC_GROUPS; group += 1)
		{
			struct bic_group_regs *group_regs = regs->group+group ;
			group_regs->target[0] = 0 ;
			group_regs->target[1] = 0 ;
			group_regs->target[2] = 0 ;
			group_regs->target[3] = 0 ;
			bic.enabled_mask[group] = 0 ;
		}
}
void __init bgp_init_IRQ(void)
{
        int irq;

	bic.regs = ioremap(BIC_PHYS, sizeof(*bic.regs));
	disable_all_bic_interrupts() ;
	bic.mask_lock = SPIN_LOCK_UNLOCKED;
	for_each_irq(irq) {
		/* Interrupts from the BIC are percpu (we don't use broadcast)
		 * so we may as well take the cycle advantage and declare it.
		 */
		set_irq_chip_and_handler(irq, &bgp_irq_chip, handle_percpu_irq);
	}
}

EXPORT_SYMBOL(bic) ;
EXPORT_SYMBOL(bic_set_cpu_for_irq) ;
