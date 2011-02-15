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
 * Description: copy_tofrom_user using the BGP DMA hardware
 *
 *
 *
 ********************************************************************/
#define REQUIRES_DUMPMEM

#include <linux/version.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/mm.h>
#include <linux/cdev.h>
#include <linux/proc_fs.h>
#include <linux/highmem.h>
#include <linux/mman.h>
#include <linux/syscalls.h>
#include <linux/skbuff.h>
#include <linux/etherdevice.h>
#include <linux/if_ether.h>
#include <linux/ip.h>
#include <linux/pagemap.h>


#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/time.h>
#include <asm/bitops.h>
#include <asm/div64.h>
#include <linux/vmalloc.h>
#include <asm/atomic.h>

#include <linux/dma-mapping.h>

#include <net/inet_connection_sock.h>
#include <net/inet_sock.h>
#include <net/inet_hashtables.h>
#include <net/tcp.h>



/* #include "bglink.h" */
#include <spi/linux_kernel_spi.h>

#include <asm/time.h>

#include "bgp_dma_tcp.h"
#include "bgp_bic_diagnosis.h"
#include "../bgp_network/bgdiagnose.h"
#include "../bgp_network/450_tlb.h"
#include "bgp_memcpy.h"

/* Machine memory geometry */
enum {
	k_l1_line_size = 32 ,
	k_page_shift = PAGE_SHIFT ,
	k_page_size = 1 << k_page_shift ,
	k_page_offset_mask = k_page_size-1
};
/* How we are going to use the hardware */
enum {
	k_counters_per_core = 1 ,
	k_spinlimit = 100000 ,
	k_requires_fp = 0 ,
	k_my_vc_for_adaptive = k_VC_anyway
	/* 	k_my_vc_for_adaptive = k_VC_ordering */
};
/* What diagnostics/verification are we going to enable */
enum {
/* 	k_diagnose = 0 , */
	k_diag_not_mapped = 1 ,
	k_fromcheck_pre = 0 ,
	k_fromcheck_post = 0,
	k_tocheck_pre = 0,
	k_tocheck_post = 0 ,
	k_check_with_crc = 1 ,
	k_flush_target_from_l1 = 0 ,
	k_verify_dma = 1,
	k_fixup_faulty_memcpy=1,
	k_map_write_check=0 ,
	k_disable_after_too_many_faults=1
};

/* value to let the counter get to when it is idle --- we do not want '0' because that would mean an interrupt */
enum {
	k_counter_idle_value = 0x00000010
};


enum {
  k_InjectionFifoGroupMemcpy = 1 ,
  k_ReceptionCounterGroupMemcpy = 1
};

/* For putting an 'msync'in where we don't think we should need it, but helping initial diagnostics */
static inline void maybe_msync(void)
{
	_bgp_msync() ;
}
/* data cache block flush, evict the given line from L1 if it is there */
static inline void dcbf(unsigned int a0,unsigned int a1)
{
	  asm volatile( "dcbf %[a0],%[a1]"
                    :
                    : [a0] "b" (a0), [a1] "b" (a1)
                    ) ;
}
static inline void dcbf0(unsigned int a)
{
	  asm volatile( "dcbf 0,%[a]"
                    :
                    : [a] "b" (a)
                    ) ;
}
static void flush_l1(void * address, unsigned int length)
{
	unsigned int address_int=(unsigned int) address ;
	unsigned int address_end_int=address_int+length-1 ;
	unsigned int line_start=address_int & ~(k_l1_line_size-1) ;
	unsigned int line_end=address_end_int & ~(k_l1_line_size-1) ;
	unsigned int line_count=(line_end-line_start)/k_l1_line_size + 1 ;
	unsigned int x ;
	unsigned int flush_address=line_start;
	for(x=0;x<line_count;x+=1)
		{
			dcbf0(flush_address) ;
			flush_address += k_l1_line_size ;
		}
}
typedef struct {
	unsigned int count ;
	atomic_t in_use[k_counters_per_core] ;
	unsigned int pad_to_line_size[(k_l1_line_size-k_counters_per_core-1)/sizeof(unsigned int)] ;
} core_counter_allocation_t __attribute__((aligned(32)));

static core_counter_allocation_t counter_allocation[k_injecting_cores] ;

static void show_injection_fifo_state(dma_tcp_t * dma_tcp,unsigned int counter_index) ;
static int acquire_counter(void)
{
	unsigned int this_core=smp_processor_id();
	core_counter_allocation_t * cci = counter_allocation + this_core ;
	unsigned int prev_count = cci->count++ ;
	unsigned int counter_index = prev_count & (k_counters_per_core-1) ;
	int in_use = atomic_inc_return(cci->in_use+counter_index) ;
	int rc=(1 == in_use) ? (counter_index + this_core*k_counters_per_core) : -1 ;
	dma_tcp_t * dma_tcp=&dma_tcp_state ;
	TRACEN(k_t_dmacopy,"prev_count=0x%08x counter_index=%d in_use=%d rc=%d",prev_count,counter_index,in_use,rc) ;
	if( 1 == in_use)
		{
			 DMA_CounterSetValueBaseMaxHw(dma_tcp->memcpyRecCounterGroup.counter[rc].counter_hw_ptr,k_counter_idle_value,0,0x0fffffff) ;
			 show_injection_fifo_state(dma_tcp, rc) ;
		}
	return rc ;

}
static void release_counter(unsigned int counter)
{
	unsigned int counter_index=counter % k_counters_per_core ;
	unsigned int core_index=counter / k_counters_per_core ;
	core_counter_allocation_t * cci = counter_allocation + core_index ;
	TRACEN(k_t_dmacopy,"counter=%d core_index=%d counter_index=%d in_use=%d",counter,core_index,counter_index,atomic_read(cci->in_use+counter_index)) ;
	atomic_set(cci->in_use+counter_index,0) ;
}

static void cause_fallback(void)
{
	TRACEN(k_t_request,"Turning off DMA memcpy") ;
	bgp_memcpy_control.use_dma = 0 ;
	dma_memcpy_statistic(k_copy_cause_fallback) ;
}

static unsigned int find_real_address(const void * virtual_address)
{
	struct page *realpage = NULL ;
	int res ;
        /* Try to fault in all of the necessary pages */
	down_read(&current->mm->mmap_sem);
	res = get_user_pages(
		current,
		current->mm,
		(unsigned long) virtual_address,
		1, /* One page */
		0, /* intent read */
		0, /* don't force */
		&realpage,
		NULL);
	up_read(&current->mm->mmap_sem);

	TRACEN(k_t_dmacopy,"find_real_address virtual_address=%p res=%d page=%p pfn=0x%08lx real_address=0x%016llx",
			virtual_address,res,realpage,page_to_pfn(realpage),page_to_phys(realpage)) ;

	if( 1 == res) /* Number of pages mapped, should be 1 for this call */
		{
			unsigned int rc = page_to_phys(realpage) ;
			put_page(realpage) ;
			return rc ;
		}
	return 0 ;

}

static unsigned int v_to_r_maybe_show(const void * vaddr)
{
     unsigned int vaddr_int=(unsigned int)vaddr ;
     int tlbx=search_tlb_v(vaddr_int) ;
     int pageid=get_tlb_pageid(tlbx) ;
     int xlat=get_tlb_xlat(tlbx) ;
     int attrib=get_tlb_attrib(tlbx) ;
     int tlbx1=search_tlb_v((unsigned int)vaddr) ;
     if( (tlbx == tlbx1)    /* Translation didn't change under me due to e.g. interrupt */
		     && ((pageid & TLB0_V) != 0) /* TLB is valid */
		     && ((tlbx & 0x20000000) != 0) /* search_tlb_v sets this bit if it found a translation */
		     )
	     {
			unsigned int epn = TLB0_EPN_1K(pageid) ; // virtual page for the TLB
			unsigned int rpn = TLB1_RPN_1K(xlat) ; // real page for the TLB
			unsigned int result = (vaddr_int-epn) + rpn ;
			TRACEN(k_t_request,"vaddr=%p tlbx=0x%08x pageid=0x%08x xlat=0x%08x attrib=0x%08x epn=0x%08x rpn=0x%08x result=0x%08x",
					vaddr,tlbx,pageid,xlat,attrib,epn,rpn,result) ;
			return result ;

	     }
     else
	     {
			TRACEN(k_t_request,"vaddr=%p tlbx=0x%08x pageid=0x%08x tlbx1=0x%08x unmapped",
					vaddr,tlbx,pageid,tlbx1) ;
			tlbx=search_tlb_v(vaddr_int) ;
			pageid=get_tlb_pageid(tlbx) ;
			xlat=get_tlb_xlat(tlbx) ;
			attrib=get_tlb_attrib(tlbx) ;
			tlbx1=search_tlb_v((unsigned int)vaddr) ;
			{
				unsigned int epn = TLB0_EPN_1K(pageid) ; // virtual page for the TLB
				unsigned int rpn = TLB1_RPN_1K(xlat) ; // real page for the TLB
				unsigned int result = (vaddr_int-epn) + rpn ;
				TRACEN(k_t_request,"retry vaddr=%p tlbx=0x%08x pageid=0x%08x xlat=0x%08x attrib=0x%08x epn=0x%08x rpn=0x%08x result=0x%08x",
						vaddr,tlbx,pageid,xlat,attrib,epn,rpn,result) ;
			}

		     return (unsigned int) -1 ; // Not mapped
	     }
}

static unsigned int v_to_r(const void * vaddr, tlb_t *t)
{
	unsigned int rc=v_to_r_maybe(vaddr,t) ;
	unsigned int rc2=v_to_r_maybe(vaddr,t) ;
	if( rc != rc2)
		{
			dma_memcpy_statistic(k_copy_inconsistent_tlb_1_info) ;
			rc=rc2 ;
			rc2=v_to_r_maybe(vaddr,t) ;
		}
	if( rc != rc2)
		{

			dma_memcpy_statistic(k_copy_inconsistent_tlb_1_rejects) ;
			TRACEN(k_t_request,"vaddr=%p rc=0x%08x rc2=0x%08x tlb_1",vaddr,rc,rc2) ;
			return 0xffffffff ;
		}
	if( 0xffffffff == rc)  // Not mapped, touch the address and see what happens
		{
			unsigned int pageInt ;
			int getrc = get_user(pageInt,(unsigned int __user *)vaddr ) ;
			_bgp_msync() ;
			if( getrc )
				{
					TRACEN(k_t_general,"Unmapped : %p",vaddr) ;
					rc =(unsigned int) -1 ; // Not mapped
				}
			else
				{
					rc=v_to_r_maybe(vaddr,t) ; // Try the lookup again; could miss (if we get an interrupt) but not likely
					rc2=v_to_r_maybe(vaddr,t) ; // Try the lookup again; could miss (if we get an interrupt) but not likely
					if( rc != rc2)
						{
							dma_memcpy_statistic(k_copy_inconsistent_tlb_2_info) ;
							rc=rc2 ;
							rc2=v_to_r_maybe(vaddr,t) ;
						}
					if( rc != rc2)
						{
							dma_memcpy_statistic(k_copy_inconsistent_tlb_2_rejects) ;
							TRACEN(k_t_request,"vaddr=%p rc=0x%08x rc2=0x%08x tlb_2",vaddr,rc,rc2) ;
							return 0xffffffff ;
						}
					dma_memcpy_statistic(k_copy_tlb_touches) ;
				}
		}
	return rc ;
}
static unsigned int v_to_r_write(const void * vaddr, tlb_t *t)
{
	unsigned int rc=v_to_r_maybe(vaddr,t) ;
	unsigned int rc2=v_to_r_maybe(vaddr,t) ;
	if( rc != rc2)
		{
			dma_memcpy_statistic(k_copy_inconsistent_tlb_1_info) ;
			rc=rc2 ;
			rc2=v_to_r_maybe(vaddr,t) ;
		}
	if( rc != rc2)
		{

			dma_memcpy_statistic(k_copy_inconsistent_tlb_1_rejects) ;
			TRACEN(k_t_request,"vaddr=%p rc=0x%08x rc2=0x%08x tlb_1",vaddr,rc,rc2) ;
			return 0xffffffff ;
		}
	if( 0xffffffff == rc)  // Not mapped, touch the address and see what happens
		{
			unsigned int pageInt =0;
			int putrc = get_user(pageInt,(unsigned int __user *)vaddr ) ;
			_bgp_msync() ;
			if( putrc )
				{
					TRACEN(k_t_general,"Unmapped : %p",vaddr) ;
					rc =(unsigned int) -1 ; // Not mapped
				}
			else
				{
					rc=v_to_r_maybe(vaddr,t) ; // Try the lookup again; could miss (if we get an interrupt) but not likely
					rc2=v_to_r_maybe(vaddr,t) ; // Try the lookup again; could miss (if we get an interrupt) but not likely
					if( rc != rc2)
						{
							dma_memcpy_statistic(k_copy_inconsistent_tlb_2_info) ;
							rc=rc2 ;
							rc2=v_to_r_maybe(vaddr,t) ;
						}
					if( rc != rc2)
						{
							dma_memcpy_statistic(k_copy_inconsistent_tlb_2_rejects) ;
							TRACEN(k_t_request,"vaddr=%p rc=0x%08x rc2=0x%08x tlb_2",vaddr,rc,rc2) ;
							return 0xffffffff ;
						}
					dma_memcpy_statistic(k_copy_tlb_touches) ;
				}
		}
	return rc ;
}
static inline void create_dma_descriptor_memcpy(dma_tcp_t *dma_tcp,
                int injection_counter,
                int reception_counter,
                dma_addr_t dataAddr,
                int msglen,
                unsigned int offset,
                DMA_InjDescriptor_t *desc
		)
{
	    int ret1 __attribute((unused));
	    TRACEN(k_t_dmacopy , "(>) memcpying injection_counter=%d reception_counter=%d dataAddr=0x%08llx msglen=0x%08x offset=0x%08x desc=%p",injection_counter,reception_counter,dataAddr,msglen,offset,desc);
	    if( 0 == msglen)
		    {
			    TRACEN(k_t_error , "(E) zero length memcpying injection_counter=%d reception_counter=%d dataAddr=0x%08llx msglen=0x%08x offset=0x%08x desc=%p",injection_counter,reception_counter,dataAddr,msglen,offset,desc);
		    }
	    ret1 = DMA_LocalDirectPutDescriptor( desc,
	                                     dma_tcp_InjectionCounterGroup(dma_tcp),          /*  inj cntr group id */
	                                     injection_counter,  /*  inj counter id */
	                                     dataAddr,        /*  send offset */
	                                     k_ReceptionCounterGroupMemcpy,        /*  rec ctr grp */
	                                     reception_counter,
	                                     offset,        /*  reception offset */
	                                     msglen          /*  message length */
	                                     );

	    TRACEN(k_t_dmacopy , "(<) ret1=%d",ret1);

}

static void diagnose_injection_fifo(DMA_InjFifo_t       *f_ptr)
{
	int  free_space_0 = DMA_FifoGetFreeSpace( &f_ptr->dma_fifo,
 		 				     0, /* Use shadow head */
 		 				     0);/* use shadow tail */
	int  free_space_1 = DMA_FifoGetFreeSpace( &f_ptr->dma_fifo,
 		 				     1, /* Use hardware head */
 		 				     0);/* use shadow tail */
	TRACEN(k_t_request,"free_space_0=0x%08x free_space_1=0x%08x",free_space_0,free_space_1) ;
}

static void diagnose_injection_fifo_by_id(
		DMA_InjFifoGroup_t    *fg_ptr,
		int                    fifo_id
		)
{
	diagnose_injection_fifo(&fg_ptr->fifos[fifo_id]) ;
}

static inline int inject_dma_descriptor_memcpy(dma_tcp_t *dma_tcp,
		                           unsigned int desired_fifo,
		                           DMA_InjDescriptor_t *desc)
  {
    int ret __attribute__((unused));
    TRACEN(k_t_dmacopy , "(>) injecting desired_fifo=%d desc=%p",desired_fifo,desc);
    maybe_msync() ;
    ret = DMA_InjFifoInjectDescriptorById( &dma_tcp->memcpyInjFifoGroupFrames,
                                            dma_tcp->memcpyInjFifoFramesIds[desired_fifo],
                                            desc );
    maybe_msync() ;
     if(ret != 1 )
 	    {
 		    TRACEN(k_t_error,"(!!!) ret=%d",ret) ;
 		    diagnose_injection_fifo_by_id(
 				    &dma_tcp->memcpyInjFifoGroupFrames,
                                    dma_tcp->memcpyInjFifoFramesIds[desired_fifo]
                                                                    ) ;

 	    }

    TRACEN(k_t_general , "(<) ret=%d",ret);
    return 1 ;

  }
static void show_injection_fifo_state(dma_tcp_t * dma_tcp,unsigned int counter_index) ;
static int instrument_copy_user_address_within_page(dma_tcp_t * dma_tcp,unsigned int counter_index,void * address, unsigned long size,const void * partner_vaddr,copy_op_t *c) ;

typedef struct {
	void * address ;
	const void * partner_address ;
	unsigned int size ;
} memcpy_control;

static unsigned int dma_copy_partial(dma_tcp_t * dma_tcp,unsigned int counter_index, memcpy_control * mc,copy_op_t *c)
{
	void * address = mc->address ;
	const void * partner_address = mc->partner_address ;
	unsigned int size = mc->size ;
	unsigned int address_int = (unsigned int) address ;
	unsigned int partner_address_int = (unsigned int ) partner_address ;

	unsigned int address_offset=address_int & k_page_offset_mask ;
	unsigned int partner_address_offset=partner_address_int & k_page_offset_mask ;
	unsigned int lim_address=min(size,k_page_size-address_offset) ;
	unsigned int lim_partner_address=min(size,k_page_size-partner_address_offset) ;
	unsigned int lim_size=min(lim_address,lim_partner_address) ;
	if( k_diagnose) c->frag_index += 1;

	TRACEN(k_t_dmacopy,"address=%p partner_address=%p size=0x%08x lim_size=0x%05x",
			address,partner_address,size,lim_size) ;

	mc->address = address+lim_size ;
	mc->partner_address = partner_address+lim_size ;
	mc->size = size-lim_size ;

	return instrument_copy_user_address_within_page(dma_tcp,counter_index,address,lim_size,partner_address,c) ;
}

/* return 0 iff the range described fits within one page */
static int crosses_page_boundary(const void * address, unsigned int size)
{
	unsigned int a=(unsigned int) address ;
	unsigned int ae = a+size-1 ;
	return (ae >> k_page_shift ) - (a >> k_page_shift) ;
}
static unsigned int dma_copy_full_singlepage(dma_tcp_t * dma_tcp,unsigned int counter_index,void * address,const void * partner_address,unsigned int size,copy_op_t *c)
{
	unsigned int rc ;
	TRACEN(k_t_dmacopy,"(>) address=%p partner_address=%p size=0x%08x",
			address,partner_address,size) ;
	rc=instrument_copy_user_address_within_page(dma_tcp,counter_index,address,size,partner_address,c) ;
	TRACEN(k_t_dmacopy,"(<) rc=%d",rc) ;
	return rc ;
}
static unsigned int dma_copy_full(dma_tcp_t * dma_tcp,unsigned int counter_index,void * address,const void * partner_address,unsigned int size,copy_op_t *c)
{
	unsigned int rc=0 ;
	memcpy_control mc ;
	TRACEN(k_t_dmacopy,"(>) address=%p partner_address=%p size=0x%08x",
			address,partner_address,size) ;
	mc.address=address ;
	mc.partner_address=partner_address ;
	mc.size=size ;
	while(mc.size != 0 && rc == 0)
		{
			rc |= dma_copy_partial(dma_tcp,counter_index,&mc,c) ;
		}
	TRACEN(k_t_dmacopy,"(<) rc=%d",rc) ;
	return rc ;
}
static unsigned int dma_copy_within_page(dma_tcp_t * dma_tcp,unsigned int counter_index, unsigned int real_address, unsigned int partner_real_address, unsigned int size,copy_op_t *c)
{
	unsigned int full_frame_count=size / k_torus_link_payload_size ;
	unsigned int full_frame_size = full_frame_count * k_torus_link_payload_size ;
	unsigned int trailing_frame_size = size - full_frame_size ;
	unsigned int rc=0 ;

	DMA_InjDescriptor_t desc ;
	TRACEN(k_t_dmacopy,"(>) counter_index=%d real_address=0x%08x partner_real_address=0x%08x size=0x%05x full_frame_count=%d full_frame_size=0x%08x trailing_frame_size=0x%08x",
			counter_index,real_address,partner_real_address,size,full_frame_count,full_frame_size,trailing_frame_size) ;
	if( k_requires_fp)
		{
			enable_kernel_fp() ;
		}
	if( full_frame_size > 0 )
		{
			create_dma_descriptor_memcpy(dma_tcp,0,counter_index,partner_real_address,full_frame_size,real_address,&desc) ;
			inject_dma_descriptor_memcpy(dma_tcp,counter_index,&desc) ;
			rc = 1 ;
		}
	if( trailing_frame_size > 0 )
		{
			show_injection_fifo_state(dma_tcp,counter_index) ;
			create_dma_descriptor_memcpy(dma_tcp,0,counter_index,partner_real_address+full_frame_size,trailing_frame_size,real_address + full_frame_size,&desc) ;
			inject_dma_descriptor_memcpy(dma_tcp,counter_index,&desc) ;
			rc+=1 ;
		}
	return rc ;
}

static void spin_idle(unsigned int idlecount)
{
	unsigned int x ;
	for(x=0;x<idlecount;x+=1)
		{
			asm volatile("nop;");
		}
}

/* Engage in least-squares regression to estimate data rates */
dma_statistic_t bgp_dma_rate ;
static void rate_observe(dma_statistic_t * st,int x,int y)
{
  int s1 = st->s1 + 1;
  int sx = st->sx + x;
  long long int sxx = st->sxx + x*x ;
  int sy = st->sy + y ;
  long long int sxy = st->sxy + x*y ;


  st->s1 = s1 ;
  st->sx = sx ;
  st->sxx = sxx ;
  st->sy = sy ;
  st->sxy = sxy ;

  if( ((s1 >> 1) & 0xff ) <= bgp_memcpy_control.rate_observe_report_count ) /* Sample a few */
	  {
		  long long det=s1*sxx-((long long)sx)*sx ;
		  long long m0 = s1*sxy - ((long long)sx)*sy ;
		  long long m1 = sxx*sy -sx*sxy ;
		  unsigned long long q0 = m0 ;
		  unsigned long long q1 = m1 ;
		  unsigned int uidet = det ;
		  if( uidet != 0)
			  {
				  do_div(q0,uidet) ;
				  do_div(q1,uidet) ;
			  }
		  else
			  {
				  q0 = 0 ;
				  q1 = 0 ;
			  }

		  TRACEN(k_t_request,"x=%d y=%d s1=%d sx=%d sxx=%lld sy=%d sxy=%lld det=%lld m0=%lld m1=%lld q0=%lld q1=%lld",
				  x,y,s1,sx,sxx,sy,sxy,det,m0,m1,q0,q1) ;
	  }

}
static int await_copy_completion(dma_tcp_t * dma_tcp,unsigned int counter_index, unsigned int size )
{
	int rc=0 ;
       unsigned int  fifo_current_head =
	(unsigned int) DMA_InjFifoGetHeadById( &dma_tcp->memcpyInjFifoGroupFrames, dma_tcp->memcpyInjFifoFramesIds[counter_index]) ;
       unsigned int fifo_initial_head = fifo_current_head ;
       unsigned int  fifo_tail =
	(unsigned int) DMA_InjFifoGetTailById( &dma_tcp->memcpyInjFifoGroupFrames, dma_tcp->memcpyInjFifoFramesIds[counter_index]) ;
       unsigned int spincount = 0 ;
       unsigned int initial_rec_counter_val=DMA_CounterGetValue(dma_tcp->memcpyRecCounterGroup.counter+counter_index) ;
       unsigned int idlecount=bgp_memcpy_control.cycles_per_packet*size/256 ;
	TRACEN(k_t_dmacopy,"(>) counter_index=%d size=0x%08x fifo_current_head=0x%08x fifo_tail=0x%08x initial_rec_counter_val=%d idlecount=%d",
			counter_index,size,fifo_current_head,fifo_tail,initial_rec_counter_val,idlecount) ;
	show_injection_fifo_state(dma_tcp,counter_index) ;
	spin_idle(idlecount) ;
	maybe_msync() ;
	{
		int rec_counter_after_idle=DMA_CounterGetValue(dma_tcp->memcpyRecCounterGroup.counter+counter_index) ;
		int rec_counter_val = rec_counter_after_idle ;
		if( rec_counter_after_idle > 0)
			  {
				  rate_observe(&bgp_dma_rate, 0,0) ;
				  rate_observe(&bgp_dma_rate, idlecount,initial_rec_counter_val-rec_counter_after_idle) ;
			  }
/* 		while(fifo_current_head != fifo_tail && spincount < k_spinlimit ) */
/* 			{ */
/* 			       fifo_current_head = */
/* 				(unsigned int) DMA_InjFifoGetHeadById( &dma_tcp->memcpyInjFifoGroupFrames, dma_tcp->memcpyInjFifoFramesIds[counter_index]) ; */
/* 	//			fifo_current_tail = */
/* 	//			(unsigned int) DMA_InjFifoGetTailById( &dma_tcp->memcpyInjFifoGroupFrames, dma_tcp->memcpyInjFifoFramesIds[counter_index]) ; */
/* 				spincount += 1 ; */
/* 			} */
		while( rec_counter_val > k_counter_idle_value && spincount < k_spinlimit )
			{
				maybe_msync() ;
				rec_counter_val=DMA_CounterGetValue(dma_tcp->memcpyRecCounterGroup.counter+counter_index) ;
				spincount += 1 ;
			}
		maybe_msync() ;
		DMA_CounterSetDisableById(&dma_tcp->memcpyRecCounterGroup,counter_index) ;
	       fifo_current_head =
		(unsigned int) DMA_InjFifoGetHeadById( &dma_tcp->memcpyInjFifoGroupFrames, dma_tcp->memcpyInjFifoFramesIds[counter_index]) ;
		{
/* 		unsigned int rec_counter_val=DMA_CounterGetValue(dma_tcp->memcpyRecCounterGroup.counter+counter_index) ; */
		dma_memcpy_statistic((0==spincount) ? k_copy_await_idle_zero : ((1==spincount) ? k_copy_await_idle_high : k_copy_await_idle_low)) ;
		TRACEN(k_t_dmacopy,
				"size=0x%08x fifo_initial_head=0x%08x fifo_current_head=0x%08x fifo_tail=0x%08x initial_rec=%d after_idle=%d rec=%d spincount=%d idlecount=%d",
				size,fifo_initial_head,fifo_current_head,fifo_tail,initial_rec_counter_val,rec_counter_after_idle,rec_counter_val,spincount,idlecount) ;
		if( fifo_current_head != fifo_tail || rec_counter_val != k_counter_idle_value)
			{
				rc=1 ;
				TRACEN(k_t_error,"(E) fifo_current_head=0x%08x fifo_tail=0x%08x spincount=%d rec_counter_val=%d",
						fifo_current_head,fifo_tail,spincount,rec_counter_val) ;
			}
		TRACEN(k_t_dmacopy,"(<) rc=%d fifo_current_head=0x%08x fifo_tail=0x%08x spincount=%d rec_counter_val=%d",rc,fifo_current_head,fifo_tail,spincount,rec_counter_val) ;
		}
	}
	return rc ;
}

static void show_injection_fifo_state(dma_tcp_t * dma_tcp,unsigned int counter_index)
{
       unsigned int  fifo_current_head =
	(unsigned int) DMA_InjFifoGetHeadById( &dma_tcp->memcpyInjFifoGroupFrames, dma_tcp->memcpyInjFifoFramesIds[counter_index]) ;
       unsigned int  fifo_current_tail =
	(unsigned int) DMA_InjFifoGetTailById( &dma_tcp->memcpyInjFifoGroupFrames, dma_tcp->memcpyInjFifoFramesIds[counter_index]) ;
       unsigned int rec_counter_val=DMA_CounterGetValue(dma_tcp->memcpyRecCounterGroup.counter+counter_index) ;
       unsigned int rec_counter_base=DMA_CounterGetBaseHw(dma_tcp->memcpyRecCounterGroup.counter[counter_index].counter_hw_ptr) ;
       unsigned int rec_counter_max=DMA_CounterGetMaxHw(dma_tcp->memcpyRecCounterGroup.counter[counter_index].counter_hw_ptr) ;
       unsigned int enabled=DMA_CounterGetEnabled(&dma_tcp->memcpyRecCounterGroup,0) ;
	TRACEN(k_t_dmacopy,"counter_index=%d fifo_current_head=0x%08x fifo_current_tail=0x%08x rec_counter_val=0x%08x base=0x%08x max=0x%08x enabled=0x%08x",
			counter_index,fifo_current_head,fifo_current_tail,rec_counter_val,rec_counter_base,rec_counter_max,enabled) ;

}

static inline int next_prbs(int seed)
{
	int ncmask = seed >> 31 ;  /*  0x00000000 or 0xffffffff */
	return (seed << 1) ^ (0x04C11DB7 & ncmask) ;   /*  CRC-32-IEEE 802.3 from http://en.wikipedia.org/wiki/Cyclic_redundancy_check */
}

static inline unsigned int rc_revise(unsigned int X0, unsigned int X1)
{
	if(k_check_with_crc)
		{
			return next_prbs(X0) ^ X1 ;
		}
	else
		{
			return X0+X1 ;
		}

}
static unsigned int region_check_int(const unsigned int * ai, unsigned int intcount)
{
	unsigned int x ;
	unsigned int rc=0 ;
	for(x=0;x<intcount;x+=1)
		{
			rc=rc_revise(rc,*(ai++)) ;
		}
	return rc ;

}
static unsigned int region_check(const void * addr, unsigned int size)
{
	const unsigned int * ai = (const unsigned int *) addr ;
	unsigned int intcount = size/sizeof(int) ;
	unsigned int tailcount = size % sizeof(int) ;
	unsigned int rc = region_check_int(ai,intcount) ;
	if(tailcount )
		{
			const unsigned char * ac = (const unsigned char *) addr ;
			unsigned int tail = (ac[size-3] << 16) | (ac[size-3] << 8) | ac[size-1] ;
			rc=rc_revise(rc,tail) ;
		}
	return rc ;

}
static void report_faulty_memcpy(void * dest, const void * src, unsigned long size,copy_op_t *c)
{
	unsigned int * di = (unsigned int *) dest ;
	const unsigned int * si = (const unsigned int *) src ;
	unsigned char * dc = (unsigned char *) (dest) ;
	const unsigned char * sc = (const unsigned char *) (src) ;
	unsigned int x ;
	unsigned int faultwordcount = 0 ;
	unsigned int zsourcecount = 0 ;
	v_to_r_maybe_show(dest) ;
	v_to_r_maybe_show(src) ;
	c->to_check_post=region_check(dest,size) ;
	if( k_disable_after_too_many_faults)
		{
			int faults_to_go=bgp_memcpy_control.faults_until_disable-1 ;
			if( faults_to_go <= 0 )
				{
					cause_fallback() ;
				}
			else
				{
					bgp_memcpy_control.faults_until_disable=faults_to_go ;
				}
		}
	dma_memcpy_statistic(k_copy_verify_miscompares) ;
	TRACEN(k_t_error,"dest=%p src=%p size=0x%08lx",dest,src,size) ;
	for(x=0;x<size/sizeof(unsigned int);x+=1)
		{
			unsigned int sx = si[x] ;
			unsigned int dx = di[x] ;
			zsourcecount += (0 == sx) ;
			if( dx != sx )
				{
					if( faultwordcount < 10 )
						{
							TRACEN(k_t_error,"(E) x=0x%08x di+x=%p si+x=%p di[x]=0x%08x si[x]=0x%08x",
									x,di+x,si+x,dx,sx) ;
						}
					if( k_fixup_faulty_memcpy) di[x]=sx ;
					faultwordcount += 1 ;
				}
		}
	if( dc[size-3] != sc[size-3])
		{
			TRACEN(k_t_error,"(E) x=0x%08lx dc+x=%p sc+x=%p dc[x]=0x%02x sc[x]=0x%02x",
					size-3,dc+size-3,sc+size-3,dc[size-3],sc[size-3]) ;
			if( k_fixup_faulty_memcpy) dc[size-3]=sc[size-3] ;
		}
	if( dc[size-2] != sc[size-2])
		{
			TRACEN(k_t_error,"(E) x=0x%08lx dc+x=%p sc+x=%p dc[x]=0x%02x sc[x]=0x%02x",
					size-2,dc+size-2,sc+size-2,dc[size-2],sc[size-2]) ;
			if( k_fixup_faulty_memcpy) dc[size-2]=sc[size-2] ;
		}
	if( dc[size-1] != sc[size-1])
		{
			TRACEN(k_t_error,"(E) x=0x%08lx dc+x=%p sc+x=%p dc[x]=0x%02x sc[x]=0x%02x",
					size-1,dc+size-1,sc+size-1,dc[size-1],sc[size-1]) ;
			if( k_fixup_faulty_memcpy) dc[size-1]=sc[size-1] ;
		}
	TRACEN(k_t_error,"%d/%ld words incorrectly copied, %d sourcewords were zero",faultwordcount,size/sizeof(unsigned int),zsourcecount) ;
	v_to_r_maybe_show(dest) ;
	v_to_r_maybe_show(src) ;
	show_stack(NULL,0) ;
	c->from_check_post=region_check(src,size) ;
	diagnose_faulty_copy(c) ;
}
/*  Check that a 'memcpy' was accurately done ... */
static int verify_memcpy(void * dest, const void * src, unsigned long size,copy_op_t *c)
{
	unsigned int * di = (unsigned int *) dest ;
	const unsigned int * si = (const unsigned int *) src ;
	unsigned char * dc = (unsigned char *) (dest) ;
	const unsigned char * sc = (const unsigned char *) (src) ;
	unsigned int q = di[0] ^ si[0] ;
	unsigned int x ;
	dma_memcpy_statistic(k_copy_verify_attempts) ;
	TRACEN(k_t_dmacopy,"dest=%p src=%p size=0x%08lx di[0]=0x%08x si[0]=0x%08x",dest,src,size,di[0],si[0]) ;
	for(x=1;x<size/sizeof(unsigned int);x+=1)
		{
			q |= *(++di) ^ *(++si) ;
		}
	q |= (dc[size-3] ^ sc[size-3]) |(dc[size-2] ^ sc[size-2]) |(dc[size-1] ^ sc[size-1]) ;
	if(q) report_faulty_memcpy(dest,src,size,c) ;
	return q ;
}

static int instrument_copy_user_address_within_page(dma_tcp_t * dma_tcp,unsigned int counter_index,void * address, unsigned long size,const void * partner_vaddr,copy_op_t *c)
{
	unsigned int addr_int =(unsigned int) address ;
	unsigned int start_page=(addr_int >> k_page_shift) ;
	unsigned int end_page=((addr_int+size-1) >> k_page_shift) ;
	unsigned int partner_address=(unsigned int) partner_vaddr ;
	unsigned int partner_start_page=(partner_address >> k_page_shift) ;
	unsigned int partner_end_page=((partner_address+size-1) >> k_page_shift) ;
	TRACEN(k_t_dmacopy,"counter_index=%d address=%p size=0x%08lx partner_vaddr=%p start_page=0x%08x end_page=0x%08x partner_start_page=0x%08x partner_end_page=0x%08x",
			counter_index,address,size,partner_vaddr,start_page,end_page,partner_start_page,partner_end_page) ;
	maybe_msync() ;
	if( end_page == start_page && partner_end_page == partner_start_page)
		{
			unsigned int real_address=v_to_r( address,&c->a_tlb) ;
			unsigned int real_address_tablewalk=find_real_address(address) ;
			unsigned int partner_real_address=v_to_r_write(partner_vaddr,&c->b_tlb) ;
			unsigned int partner_real_address_tablewalk=find_real_address(partner_vaddr) ;
			TRACEN(k_t_dmacopy,"address=%p real_address=0x%08x r_a_tablewalk=0x%08x partner_vaddr=%p partner_real_address=0x%08x p_r_a_tablewalk=0x%08x",address,real_address,real_address_tablewalk,partner_vaddr,partner_real_address,partner_real_address_tablewalk) ;
			if( k_diagnose)
				{
					c->a_raddress=real_address ;
					c->b_raddress=partner_real_address ;
				}
			if( 0xffffffff != real_address && 0xffffffff != partner_real_address)
				{
					unsigned int injection_count ;
					TRACEN(k_t_dmacopy,"address=%p real_address=0x%08x r_a_tablewalk=0x%08x partner_vaddr=%p partner_real_address=0x%08x p_r_a_tablewalk=0x%08x",address,real_address,real_address_tablewalk,partner_vaddr,partner_real_address,partner_real_address_tablewalk) ;
					if( k_flush_target_from_l1)
						{
							flush_l1(address,size) ;
						}
					injection_count=dma_copy_within_page(dma_tcp,counter_index,real_address,partner_real_address,size,c) ;
					return 0 ;

				}
			if( 0xffffffff == real_address ) dma_memcpy_statistic(k_copy_source_tlb_rejects) ;
			if( 0xffffffff == partner_real_address ) dma_memcpy_statistic(k_copy_target_tlb_rejects) ;
			return 1 ;
		}
	dma_memcpy_statistic(k_copy_spanpage_rejects) ;
	return 1 ;  // At least one of the addresses wasn't mapped, or things spanned a page boundary

}

static int instrument_copy_user_address(dma_tcp_t * dma_tcp,unsigned int counter_index,void * address, unsigned long size,dma_addr_t partner_addr, const void * partner_vaddr,copy_op_t *c)
{
	int rc ;
	{
	rc= dma_copy_full(dma_tcp,counter_index,address, partner_vaddr,size,c) ;
	if( 0 == rc)
		{
			rc = await_copy_completion(dma_tcp,counter_index,size) ;
		}
	}
	if( 0 == rc && k_verify_dma && bgp_memcpy_control.verify_dma)
		{
				{
					rc = verify_memcpy(address, partner_vaddr,  size,c) ;
					if(rc)
						{
							TRACEN(k_t_error,"trapped") ;
						}
				}
		}
	return rc ;

}
static int instrument_copy_user_address_singlepage(dma_tcp_t * dma_tcp,unsigned int counter_index,void * address, unsigned long size,dma_addr_t partner_addr, const void * partner_vaddr,copy_op_t *c)
{
	int rc ;
	{
	rc= dma_copy_full_singlepage(dma_tcp,counter_index, address, partner_vaddr,size,c) ;
	if( 0 == rc)
		{
			rc = await_copy_completion(dma_tcp,counter_index,size) ;
		}
	}
	if( 0 == rc && k_verify_dma && bgp_memcpy_control.verify_dma)
		{
				{
					rc = verify_memcpy(address, partner_vaddr,  size,c) ;
					if(rc)
						{
							TRACEN(k_t_error,"trapped") ;
						}
				}
		}
	return rc ;

}
static int instrument_copy_user(void * to, const void * from, unsigned long size,unsigned int counter_index,copy_op_t *c)
{
	dma_tcp_t * dma_tcp=&dma_tcp_state ;
	dma_addr_t fromAddr = dma_map_single(NULL, (void *)from, size, DMA_TO_DEVICE);
	int rc ;
	TRACEN(k_t_dmacopy,"(>)") ;
	maybe_msync() ;
	DMA_CounterSetValueHw(dma_tcp->memcpyRecCounterGroup.counter[counter_index].counter_hw_ptr,size+k_counter_idle_value) ;
	 show_injection_fifo_state(dma_tcp, counter_index) ;
	DMA_CounterSetEnableById(&dma_tcp->memcpyRecCounterGroup,counter_index) ;
	 show_injection_fifo_state(dma_tcp, counter_index) ;
	maybe_msync() ;
        DMA_CounterSetValueWideOpenById ( & dma_tcp->injCounterGroup, dma_tcp->injCounterId,  0xffffffff );
        _bgp_msync() ;
	rc= instrument_copy_user_address(dma_tcp,counter_index,to,size,fromAddr,(void *)from,c) ;
	TRACEN(k_t_dmacopy,"(<) rc=%d",rc) ;
	return rc ;
}
static int instrument_copy_user_singlepage(void * to, const void * from, unsigned long size,unsigned int counter_index,copy_op_t *c)
{
	dma_tcp_t * dma_tcp=&dma_tcp_state ;
	dma_addr_t fromAddr = dma_map_single(NULL, (void *)from, size, DMA_TO_DEVICE);
	int rc ;
	TRACEN(k_t_dmacopy,"(>)") ;
	maybe_msync() ;
	 show_injection_fifo_state(dma_tcp, counter_index) ;
	DMA_CounterSetValueHw(dma_tcp->memcpyRecCounterGroup.counter[counter_index].counter_hw_ptr,size+k_counter_idle_value) ;
	 show_injection_fifo_state(dma_tcp, counter_index) ;
	DMA_CounterSetEnableById(&dma_tcp->memcpyRecCounterGroup,counter_index) ;
	 show_injection_fifo_state(dma_tcp, counter_index) ;
	maybe_msync() ;
        DMA_CounterSetValueWideOpenById ( & dma_tcp->injCounterGroup, dma_tcp->injCounterId,  0xffffffff );
        _bgp_msync() ;
	rc= instrument_copy_user_address_singlepage(dma_tcp,counter_index,to,size,fromAddr,from,c) ;
	TRACEN(k_t_dmacopy,"(<) rc=%d",rc) ;
	return rc ;
}
static int instrument_copy_tofrom_user(void * to, const void * from, unsigned long size,copy_op_t *c)
{
	int rc=1 ;
	int counter_index=acquire_counter() ;
	TRACEN(k_t_dmacopy,"(>) to=%p from=%p size=0x%08lx counter_index=%d",to,from,size,counter_index) ;
	if( counter_index >= 0)
		{
			rc= instrument_copy_user(to,from,size,counter_index,c) ;
			release_counter(counter_index) ;
		}
	else
		{
			dma_memcpy_statistic(k_copy_no_counter_rejects) ;
		}
	TRACEN(k_t_dmacopy,"(<) rc=%d",rc) ;
	return rc ;
}

static int instrument_copy_tofrom_user_singlepage(void *to, const void * from, unsigned long size,copy_op_t *c)
{
	int rc=1 ;
	int counter_index=acquire_counter() ;
	TRACEN(k_t_dmacopy,"(>) to=%p from=%p size=0x%08lx counter_index=%d",to,from,size,counter_index) ;
	if( counter_index >= 0)
		{
			rc= instrument_copy_user_singlepage(to,from,size,counter_index,c) ;
			release_counter(counter_index) ;
		}
	else
		{
			dma_memcpy_statistic(k_copy_no_counter_rejects) ;
		}
	TRACEN(k_t_dmacopy,"(<) rc=%d",rc) ;
	return rc ;
}

static int all_pages_mapped_read(unsigned long address, unsigned long size)
{
	unsigned int start_page=(address >> k_page_shift) ;
	unsigned int end_page=((address+size) >> k_page_shift) ;
	unsigned int page_count = end_page-start_page+1 ;
	unsigned int x ;
	if( is_kernel_addr(address)) return 0 ; // If we have a 'kernel address', assume it's OK
	 /*  Defend against the possibility that the user application has posted an unmapped address */
	for(x=0;x<page_count;x+=1)
		{
			int pageInt ;
			int __user * pageIntP = (int __user *) ((start_page+x) << k_page_shift)  ;
			if( get_user(pageInt,pageIntP) )
				{
					TRACEN(k_t_general,"Unmapped : 0x%08x start_page=0x%08x page_count=0x%08x is_kernel_addr=%d",((start_page+x) << k_page_shift),start_page,page_count,is_kernel_addr(((start_page+x) << k_page_shift))) ;
					if( k_diag_not_mapped)
					{
						tlb_t t ;
						unsigned int r=v_to_r_maybe((void *)address, &t) ;
						TRACEN(k_t_request,"Unmapped : 0x%08x start_page=0x%08x page_count=0x%08x is_kernel_addr=%d",((start_page+x) << k_page_shift),start_page,page_count,is_kernel_addr(((start_page+x) << k_page_shift))) ;
						TRACEN(k_t_request,"address=0x%08lx r=0x%08x",address,r) ;
						diagnose_tlb(&t) ;
					}

					return 1;
				}

		}
	return 0 ;
}

static int all_pages_mapped_write(unsigned long address, unsigned long size)
{
	unsigned int start_page=(address >> k_page_shift) ;
	unsigned int end_page=((address+size) >> k_page_shift) ;
	unsigned int page_count = end_page-start_page+1 ;
	unsigned int x ;
/* 	int pageInt ; */
	char __user * pageCharP = (char __user *) address ;
	if( is_kernel_addr(address)) return 0 ; // If we have a 'kernel address', assume it's OK
	if(put_user(0,pageCharP))
		{
			TRACEN(k_t_general,"Unmapped : 0x%08x start_page=0x%08x page_count=0x%08x is_kernel_addr=%d",((start_page+x) << k_page_shift),start_page,page_count,is_kernel_addr(((start_page+x) << k_page_shift))) ;
			if( k_diag_not_mapped)
			{
				tlb_t t ;
				unsigned int r=v_to_r_maybe((void *)address, &t) ;
				TRACEN(k_t_request,"Unmapped : 0x%08x start_page=0x%08x page_count=0x%08x is_kernel_addr=%d",((start_page+x) << k_page_shift),start_page,page_count,is_kernel_addr(((start_page+x) << k_page_shift))) ;
				TRACEN(k_t_request,"address=0x%08lx r=0x%08x",address,r) ;
				diagnose_tlb(&t) ;
			}

			return 1;
		}
	 /*  Defend against the possibility that the user application has posted an unmapped address */
	for(x=1;x<page_count;x+=1)
		{
/* 			int pageInt ; */
			char __user * pageCharP = (char __user *) ((start_page+x) << k_page_shift)  ;
/*  TODO: Fix this up against the possibility of 0..2 bytes at the start of the last page */
			if( put_user(0,pageCharP) )
				{
					TRACEN(k_t_general,"Unmapped : 0x%08x start_page=0x%08x page_count=0x%08x is_kernel_addr=%d",((start_page+x) << k_page_shift),start_page,page_count,is_kernel_addr(((start_page+x) << k_page_shift))) ;
					if( k_diag_not_mapped)
					{
						tlb_t t ;
						unsigned int r=v_to_r_maybe((void *)address, &t) ;
						TRACEN(k_t_request,"Unmapped : 0x%08x start_page=0x%08x page_count=0x%08x is_kernel_addr=%d",((start_page+x) << k_page_shift),start_page,page_count,is_kernel_addr(((start_page+x) << k_page_shift))) ;
						TRACEN(k_t_request,"address=0x%08lx r=0x%08x",address,r) ;
						diagnose_tlb(&t) ;
					}

					return 1;
				}

		}
	return 0 ;
}

/* Returns 1 if we could DMA-copy things, 0 if we couldn't */
extern unsigned long bgp_dma_instrument_copy_tofrom_user(void  *to,
                const void *from, unsigned long size)
{
	TRACEN(k_t_general,"to=%p from=%p size=0x%08lx",to,from,size) ;
	dma_memcpy_statistic(k_copy_tofrom_user_calls) ;
	if( size > 0 && size >= bgp_memcpy_control.dma_threshold )
		{
			copy_op_t c ;
			TRACEN(k_t_dmacopy,"to=%p from=%p size=0x%08lx",to,from,size) ;
			if( all_pages_mapped_read((unsigned long) from,size))
				{
					dma_memcpy_statistic(k_copy_source_rejects) ;
					return 1 ;
				}
			if( k_map_write_check && all_pages_mapped_write((unsigned long) to,size))
				{
					dma_memcpy_statistic(k_copy_target_rejects) ;
					return 1 ;
				}
			if( k_diagnose)
				{
					c.to_vaddr=to ;
					c.from_vaddr=(void *)from ;
					c.size=size ;
					c.frag_index=0 ;
					c.from_check_post = 0xffffffff ;
					c.to_check_pre = 0xffffffff ;
					c.to_check_post = 0xffffffff ;
					if(k_fromcheck_pre)
						{
							c.from_check_pre=region_check((void *)from,size) ;
						}
					else
						{
							c.from_check_pre = 0xffffffff ;
						}
					if(k_tocheck_pre)
						{
							c.to_check_pre=region_check(to,size) ;
						}
					else
						{
							c.to_check_pre = 0xffffffff ;
						}
				}


			if( crosses_page_boundary(from,size) || crosses_page_boundary(to,size))
				{
					if( bgp_memcpy_control.handle_pagecrossing)
						{

							unsigned long rc= instrument_copy_tofrom_user(to,from,size,&c) ;
							dma_memcpy_statistic((0==rc) ? k_copy_accelerate_successes : k_copy_accelerate_rejects) ;
							TRACEN(k_t_dmacopy,"rc=%ld",rc) ;
							if(k_diagnose && 0 == rc )
								{
									if(k_fromcheck_post)
										{
											c.from_check_post=region_check(from,size) ;
										}
									if(k_tocheck_post)
										{
											c.to_check_post=region_check(to,size) ;
										}
									if( (k_fromcheck_pre && k_fromcheck_post && c.from_check_post != c.from_check_pre)
										||
										(k_fromcheck_pre && k_tocheck_post && c.from_check_pre != c.to_check_post)
										||
										(k_fromcheck_post && k_tocheck_post && c.from_check_post != c.to_check_post)
										)
										{
											diagnose_faulty_copy(&c) ;
											return 1 ;
										}
								}
							return rc ;
						}
					else
						{
							dma_memcpy_statistic(k_copy_crosspage_limitation_rejects) ;
							return 1 ;
						}
				}
			else
				{
					{
						unsigned long rc= instrument_copy_tofrom_user_singlepage(to,from,size,&c) ;
						dma_memcpy_statistic((0==rc) ? k_copy_accelerate_successes : k_copy_accelerate_rejects) ;
						TRACEN(k_t_dmacopy,"rc=%ld",rc) ;
						if(k_diagnose && 0 == rc )
							{
								if(k_fromcheck_post)
									{
										c.from_check_post=region_check(from,size) ;
									}
								if(k_tocheck_post)
									{
										c.to_check_post=region_check(to,size) ;
									}
								if( (k_fromcheck_pre && k_fromcheck_post && c.from_check_post != c.from_check_pre)
									||
									(k_fromcheck_pre && k_tocheck_post && c.from_check_pre != c.to_check_post)
									||
									(k_fromcheck_post && k_tocheck_post && c.from_check_post != c.to_check_post)
									)
									{
										diagnose_faulty_copy(&c) ;
										return 1 ;
									}
							}

						return rc ;
					}

				}
		}
	dma_memcpy_statistic(k_copy_size_rejects) ;
	return 1 ; // Not copied, size under threshold

}

static struct ctl_table dma_memcpy_table[] = {
	        {
	                .ctl_name       = CTL_UNNUMBERED,
	                .procname       = "counter_allocation_0",
	                .data           = counter_allocation+0,
	                .maxlen         = sizeof(core_counter_allocation_t),
	                .mode           = 0644,
	                .proc_handler   = &proc_dointvec
	        },
	        {
	                .ctl_name       = CTL_UNNUMBERED,
	                .procname       = "counter_allocation_1",
	                .data           = counter_allocation+1,
	                .maxlen         = sizeof(core_counter_allocation_t),
	                .mode           = 0644,
	                .proc_handler   = &proc_dointvec
	        },
	        {
	                .ctl_name       = CTL_UNNUMBERED,
	                .procname       = "counter_allocation_2",
	                .data           = counter_allocation+2,
	                .maxlen         = sizeof(core_counter_allocation_t),
	                .mode           = 0644,
	                .proc_handler   = &proc_dointvec
	        },
	        {
	                .ctl_name       = CTL_UNNUMBERED,
	                .procname       = "counter_allocation_3",
	                .data           = counter_allocation+3,
	                .maxlen         = sizeof(core_counter_allocation_t),
	                .mode           = 0644,
	                .proc_handler   = &proc_dointvec
	        },
	        { 0 },
} ;

static struct ctl_path dma_memcpy_ctl_path[] = {
	{ .procname = "bgp", .ctl_name = 0, },
	{ .procname = "dmacopy", .ctl_name = 0, },
	{ },
};

static void __init
bgp_dma_memcpy_init_counter_allocation(void)
{
	unsigned int core_index ;
	       register_sysctl_paths(dma_memcpy_ctl_path,dma_memcpy_table) ;
	for(core_index=0;core_index<k_injecting_cores;core_index+=1)
		{
			core_counter_allocation_t * cci = counter_allocation + core_index ;
			unsigned int counter_index ;
			cci->count =  0;
			for(counter_index=0;counter_index<k_counters_per_core;counter_index+=1)
				{
					atomic_set(cci->in_use+counter_index,0) ;
				}

		}
	TRACEN(k_t_init,"counter_allocation initialised") ;
}

/*  This gets driven in the FLIH when a DMA interrupt occurs */
static void dummyCounterZeroHandler(u32 arg1, u32 arg2, u32 arg3, u32 arg4)
{
	TRACEN(k_t_error,"(>) Unexpected interrupt" );
	TRACEN(k_t_error,"(<)" );
}

/* 'copyin/out' via the BGP DMA is believed functional, but seems not useful since copying via the parallel FP regs */
/* seems to run faster, even in cases where that wipes out the L1 cache. Code is left here in case someone wants to */
/* try improving it, and to indicate which sections of the BGP DMA unit (injection fifo and reception counters) are needed */
/* to make it work. */
void __init
bgp_dma_memcpyInit(dma_tcp_t * dma_tcp)
{
	       bgp_dma_memcpy_init_counter_allocation() ;
    {
      int counter_index ;
      for( counter_index=0; counter_index< k_injecting_cores; counter_index += 1  )
        {
              dma_tcp->memcpyInjFifoFramesPri[ counter_index ] = 0 ;
              dma_tcp->memcpyInjFifoFramesLoc[ counter_index ] = 1 ;
              dma_tcp->memcpyInjFifoFramesIds[ counter_index ] = counter_index ;
              dma_tcp->memcpyInjFifoFramesMap[ counter_index ] = 0;  /*  'memcpy' injector not connected to torus */
        }
    }
    {
      int ret = DMA_InjFifoGroupAllocate( k_InjectionFifoGroupMemcpy,
          k_injecting_cores,   /*  num inj fifos */
                                  dma_tcp->memcpyInjFifoFramesIds,
                                  dma_tcp->memcpyInjFifoFramesPri,
                                  dma_tcp->memcpyInjFifoFramesLoc,
                                  dma_tcp->memcpyInjFifoFramesMap,
                                  NULL,
                                  NULL,
                                  NULL,
                                  NULL,
                                  NULL,
                                  & dma_tcp->memcpyInjFifoGroupFrames );

      TRACEN(k_t_init,"(=)DMA_InjFifoGroupAllocate rc=%d", ret );

      if( 0 == ret)
    {
      int counter_index ;
      for( counter_index=0; counter_index< k_injecting_cores; counter_index += 1  )
        {
        	TRACEN(k_t_init,"fg_ptr=%p fifo_id=%d va_start=%p va_head=%p va_end=%p",
        			&dma_tcp->memcpyInjFifoGroupFrames,
        			dma_tcp->memcpyInjFifoFramesIds[counter_index],
        			dma_tcp->idma.idma_core[counter_index].memcpy_packet_fifo,
        			dma_tcp->idma.idma_core[counter_index].memcpy_packet_fifo,
        			dma_tcp->idma.idma_core[counter_index].memcpy_packet_fifo+1
        			) ;
        	{
              int ret = DMA_InjFifoInitById( &dma_tcp->memcpyInjFifoGroupFrames,
                  dma_tcp->memcpyInjFifoFramesIds[counter_index],
                  dma_tcp->idma.idma_core[counter_index].memcpy_packet_fifo,
                  dma_tcp->idma.idma_core[counter_index].memcpy_packet_fifo,   /*  head */
                  dma_tcp->idma.idma_core[counter_index].memcpy_packet_fifo+1   /*  end */
                                 );

              dma_tcp->idma.idma_core[counter_index].memcpy_fifo_initial_head =
                (unsigned int) DMA_InjFifoGetHeadById( &dma_tcp->memcpyInjFifoGroupFrames, dma_tcp->memcpyInjFifoFramesIds[counter_index]) ;
              TRACEN(k_t_init,"(=)DMA_InjFifoInitById rc=%d initial_head=0x%08x", ret , dma_tcp->idma.idma_core[counter_index].memcpy_fifo_initial_head);
        	}
        }
    }
     /*  Set up a reception counter for 'memcpy' */
        {
           /*  Initialize reception counter group */
          int ret  __attribute__ ((unused)) = DMA_CounterGroupAllocate( DMA_Type_Reception,
        		  k_ReceptionCounterGroupMemcpy,  /*  group number */
              DMA_NUM_COUNTER_SUBGROUPS_PER_GROUP,
                                dma_tcp->memcpyRecCntrSubgrps,
/*  TODO: Not really taking interrupts from this counter group, but maybe it has to be coherent ? */
//       	                                0,   /*  target core for interrupts */
//       	                                NULL, /* Not planning to take interrupts from memcpy counters */
                                2,   /*  target core for interrupts */
                                dummyCounterZeroHandler,
                                NULL,
                                NULL,
                                & dma_tcp->memcpyRecCounterGroup );
          TRACEN(k_t_init,"(=)DMA_CounterGroupAllocate rc=%d", ret );
        }
/* 		    { */
/* 		      int counter_index ; */
/* 		      for( counter_index=0; counter_index< DMA_NUM_COUNTERS_PER_GROUP; counter_index += 1  ) */
/* 			      { */
/* 				      DMA_CounterSetDisableById(&dma_tcp->memcpyRecCounterGroup,counter_index) ; */
/* 				      DMA_CounterSetValueBaseMaxHw(dma_tcp->memcpyRecCounterGroup.counter[counter_index].counter_hw_ptr,k_counter_idle_value,0,0xffffffff) ; */
/* 			      } */
/* 			_bgp_msync() ; */
/* //		      for( counter_index=0; counter_index< k_injecting_cores; counter_index += 1  ) */
/* //			      { */
/* //					DMA_CounterSetEnableById(&dma_tcp->memcpyRecCounterGroup,counter_index) ; */
/* //			      } */
/* 			_bgp_msync() ; */
/* 		    } */



}
}
