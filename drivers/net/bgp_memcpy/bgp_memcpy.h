/*********************************************************************
 *
 * (C) Copyright IBM Corp. 2010
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses>.
 *
 * Author: Chris Ward <tjcw@uk.ibm.com>
 *
 * Description: Blue Gene low-level driver copy_tofrom_user using
 * BlueGene-specific hardware
 *
 *
 ********************************************************************/
#ifndef __BGP_MEMCPY_H__
#define __BGP_MEMCPY_H__


typedef struct
{
  int use_dma ;
  int use_fpu ;
  int dma_threshold ; /* Use the BGP DMA unit for copy_tofrom_user this size or larger */
  int fpu_threshold ; /* Use the BGP FPU for copy_tofrom_user this size or larger */
  int verify_dma ; /* Whether to verify the copy (for diagnostics) */
  int verify_fpu ; /* Whether to verify the copy (for diagnostics) */
  int cycles_per_packet ; /* Estimate of number of cycles per packet, for local spin before looking at counters */
  int faults_until_disable ; /* Number of faults until we disable acceleration */
  int rate_observe_report_count ; /* Number of times out of 256 that the rate gets displayed */
  int handle_pagecrossing ; /* Whether the DMA version should attempt to handle page-boundary-crossings */
  int fpu_handle_pagecrossing_read ; /* Whether the FPU version should attempt to handle page-boundary-crossings on reads */
  int fpu_handle_pagecrossing_write ;  /* Whether the FPU version should attempt to handle page-boundary-crossings on writes */
  int mask ; /* Whether to mask interrupts */
  int assist_active ; /* Whether to assist copypage and clearpages */
  /* int trace_count ; */ /* Number of trace records to cut before stopping */
} bgp_memcpy_control_t ;

extern bgp_memcpy_control_t bgp_memcpy_control ;

enum {
	k_copy_cause_fallback ,
  k_copy_verify_miscompares ,
  k_in_irq ,
  k_in_softirq ,

  k_copy_verify_attempts ,
  k_copy_tofrom_user_calls ,
	k_copy_accelerate_successes ,
	k_copy_accelerate_rejects ,

	k_copy_size_rejects ,
	k_copy_spanpage_rejects ,
	k_copy_crosspage_limitation_rejects ,
	k_copy_inconsistent_tlb_1_rejects ,

	k_copy_inconsistent_tlb_2_rejects ,
	k_copy_no_counter_rejects ,
	k_copy_source_tlb_rejects ,
	k_copy_target_tlb_rejects ,

	k_copy_source_rejects ,
	k_copy_target_rejects ,
	k_copy_unaligned_rejects ,
	k_copy_tlb_touches ,

	k_copy_await_idle_zero ,
	k_copy_await_idle_low ,
	k_copy_await_idle_high ,
	k_copy_inconsistent_tlb_1_info ,

	k_copy_inconsistent_tlb_2_info ,
	k_copy_segv_trap ,
	k_precopy_segv_trap ,
	k_postcopy_segv_trap ,

	k_copy_statistics
};

/* The underlying assembler copy function, returns 0 iff it copies all the data */
extern unsigned long __real__copy_tofrom_user(void  *to,
		const void __user *from, unsigned long size) ;

extern unsigned int bgp_dma_memcpy_statistics[k_copy_statistics] ;
static inline void dma_memcpy_statistic(unsigned int X)
{
	bgp_dma_memcpy_statistics[X] += 1 ;
}

extern unsigned long bgp_dma_instrument_copy_tofrom_user(void  *to,
                const void *from, unsigned long size) ;
extern unsigned long bgp_fpu_instrument_copy_tofrom_user(void  *to,
                const void *from, unsigned long size) ;

enum
{
	k_diagnose = 1
};
/* Items to record about a copy op, for diagnosing faults */
typedef struct
{
	const void * vaddr ;
	unsigned int tlb_v ;
	unsigned int pageid ;
	unsigned int xlat ;
	unsigned int attrib ;
} tlb_t ;

typedef struct
{
  void * to_vaddr ;
  const void * from_vaddr ;
  unsigned int size ;
  tlb_t a_tlb ;
  tlb_t b_tlb ;
  unsigned int a_raddress ;
  unsigned int b_raddress ;
  unsigned int from_check_pre ;
  unsigned int to_check_pre ;
  unsigned int from_check_post ;
  unsigned int to_check_post ;
  unsigned int frag_index ;
} copy_op_t ;

static void diagnose_tlb(tlb_t *t)
{
	unsigned int t0=t->pageid ;
	unsigned int t1=t->xlat ;
	unsigned int t2=t->attrib ;
	TRACEN(k_t_request,"vaddr=%p tlb_v=0x%08x %08x-%08x-%08x ts=%d tid=0x%02x epn=0x%08x rpn=0x%01x-%08x size=%s WIMG=%d%d%d%d U=%d%d%d%d V=%d uxwr=%d sxwr=%d",
			t->vaddr,t->tlb_v,t0,t1,t2,
			(t0 & TLB0_TS) ? 1 : 0,
			(t2 >> 22) & 0xff ,
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
			(t0 & TLB0_V) ? 1 : 0,
			(t2 >> 3) & 7,
			t2 & 7
			) ;
}
static void diagnose_faulty_copy(copy_op_t *c)  __attribute__((unused)) ;
static void diagnose_faulty_copy(copy_op_t *c)
{
	TRACEN(k_t_request,"from_vaddr=%p to_vaddr=%p size=0x%08x a_raddress=0x%08x b_raddress=0x%08x from_check_pre=0x%08x to_check_pre=0x%08x from_check_post=0x%08x to_check_post=0x%08x frag_index=%d",
			c->from_vaddr,c->to_vaddr,c->size,c->a_raddress,c->b_raddress,c->from_check_pre,c->from_check_post,c->to_check_pre,c->to_check_post,c->frag_index) ;
	diagnose_tlb(&c->a_tlb) ;
	diagnose_tlb(&c->b_tlb) ;
}

/* Find the real store address for a virtual address, by looking at the TLB and causing a TLB miss if needed */
static unsigned int v_to_r_maybe(const void * vaddr,tlb_t *t)
{
     unsigned int vaddr_int=(unsigned int)vaddr ;
     int tlbx=search_tlb_v(vaddr_int) ;
     int pageid=get_tlb_pageid(tlbx) ;
     int xlat=get_tlb_xlat(tlbx) ;
     int attrib=get_tlb_attrib(tlbx) ;
     int tlbx1=search_tlb_v((unsigned int)vaddr) ;
     if( k_diagnose)
	     {
		     t->vaddr = vaddr ;
		     t->tlb_v = tlbx1 ;
		     t->pageid = pageid ;
		     t->xlat = xlat ;
		     t->attrib = attrib ;
	     }
     if( (tlbx == tlbx1)    /* Translation didn't change under me due to e.g. interrupt */
		     && ((pageid & TLB0_V) != 0) /* TLB is valid */
		     && ((tlbx & 0x20000000) != 0) /* search_tlb_v sets this bit if it found a translation */
		     )
	     {
			unsigned int epn = TLB0_EPN_1K(pageid) ; // virtual page for the TLB
			unsigned int rpn = TLB1_RPN_1K(xlat) ; // real page for the TLB
			unsigned int result = (vaddr_int-epn) + rpn ;
			TRACEN(k_t_dmacopy,"vaddr=%p tlbx=0x%08x pageid=0x%08x xlat=0x%08x attrib=0x%08x epn=0x%08x rpn=0x%08x result=0x%08x",
					vaddr,tlbx,pageid,xlat,attrib,epn,rpn,result) ;
			return result ;

	     }
     else
	     {
			TRACEN(k_t_dmacopy,"vaddr=%p tlbx=0x%08x pageid=0x%08x tlbx1=0x%08x unmapped",
					vaddr,tlbx,pageid,tlbx1) ;
		     return (unsigned int) -1 ; // Not mapped
	     }
}

#endif
