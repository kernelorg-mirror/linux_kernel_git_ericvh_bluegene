/* Basic access functions for 'software TLBs' in powerpc 440/450 */
#ifndef __450_tlb_h__
#define __450_tlb_h__
#include <asm/bluegene_ras.h>

static inline int get_tlb_pageid(int tlbindex)
  {
	  int rc ;
	   /*  PPC44x_TLB_PAGEID is 0 */
	  asm volatile( "tlbre  %[rc],%[index],0"
                    : [rc] "=r" (rc)
                    : [index] "r" (tlbindex)
                    ) ;
	  return rc ;
 }

static inline int get_tlb_xlat(int tlbindex)
  {
	  int rc ;
	   /*  PPC44x_TLB_XLAT is 1 */
	  asm volatile( "tlbre  %[rc],%[index],1"
                    : [rc] "=r" (rc)
                    : [index] "r" (tlbindex)
                    ) ;
	  return rc ;
 }

static inline int get_tlb_attrib(int tlbindex)
  {
	  int rc ;
	   /*  PPC44x_TLB_ATTRIB is 2 */
	  asm volatile( "tlbre  %[rc],%[index],2"
                    : [rc] "=r" (rc)
                    : [index] "r" (tlbindex)
                    ) ;
	  return rc ;
 }

static inline int search_tlb(unsigned int vaddr)
  {
    int rc ;
     /*  PPC44x_TLB_ATTRIB is 2 */
    asm volatile( "tlbsx  %[rc],0,%[vaddr]"
                    : [rc] "=r" (rc)
                    : [vaddr] "r" (vaddr)
                    ) ;
    return rc ;
 }

//static inline int search_tlb_validity(unsigned int vaddr)
//{
//  int validity ;
//  asm volatile( "tlbsx.  %[validity],0,%[vaddr]" "\n"
//		    "mfcr %[validity]"
//                  :
//                    [validity] "=r" (validity)
//                  : [vaddr] "r" (vaddr)
//                  : "cc"
//                  ) ;
//  return validity ;
//}


static inline int search_tlb_v(unsigned int vaddr)
  {
    int rc ;
    int tlbindex ;
    int validity ;
     /*  PPC44x_TLB_ATTRIB is 2 */
    asm volatile( "tlbsx.  %[tlbindex],0,%[vaddr]" "\n"
		    "mfcr %[validity]"
                    : [tlbindex] "=r" (tlbindex),
                      [validity] "=r" (validity)
                    : [vaddr] "r" (vaddr)
                    : "cc"
                    ) ;
//    tlbindex = search_tlb(vaddr) ;
//    validity=search_tlb_validity(vaddr) ;
    rc = (validity & 0x20000000) | (tlbindex & 0xefffffff) ; // Hi bit for 'found', other bits (bottom 6, really) for index
//    TRACEN(k_t_request,"vaddr=0x%08x tlbindex=0x%08x validity=0x%08x rc=0x%08x",vaddr,tlbindex,validity,rc) ;
    return rc ;
 }

#define TLB0_EPN_1K(a)   ((a)&0xFFFFFC00)    /*   EA[ 0:21] */
#define TLB0_V          _BN(22)              /*   Valid Bit */
#define TLB0_TS         _BN(23)              /*   Translation Address Space */
#define TLB0_SIZE(x)    _B4(27,x)            /*   Page Size */
#define TLB1_ERPN(e)     _B4(31,e)           /*   Extended RPN: 4 MSb's of 36b Physical Address */
#define TLB1_RPN_1K(p)   ((p)&0xFFFFFC00)    /*   RPN[ 0:21] */

#define TLB2_FAR         _BN(10)             /*   Fixed Address Region */
#define TLB2_WL1         _BN(11)             /*   Write-Thru L1        (when CCR1[L2COBE]=1) */
#define TLB2_IL1I        _BN(12)             /*   Inhibit L1-I caching (when CCR1[L2COBE]=1) */
#define TLB2_IL1D        _BN(13)             /*   Inhibit L1-D caching (when CCR1[L2COBE]=1) */
#define TLB2_IL2I        _BN(14)             /*   see below (on normal C450: Inhibit L2-I caching (when CCR1[L2COBE]=1) */
#define TLB2_IL2D        _BN(15)             /*   see below (on normal C450: Inhibit L2-D caching (when CCR1[L2COBE]=1) */
#define TLB2_U0          _BN(16)             /*   see below (undefined/available on normal C450 */
#define TLB2_U1          _BN(17)             /*   User 1: L1 Transient Enable */
#define TLB2_U2          _BN(18)             /*   User 2: L1 Store WithOut Allocate #define TLB2_U3          _BN(19)            //  see below (on normal C450: User 3: L3 Prefetch Inhibit (0=Enabled, 1=Inhibited) */
#define TLB2_U3          _BN(19)             /*   see below (on normal C450: User 3: L3 Prefetch Inhibit (0=Enabled, 1=Inhibited) */
#define TLB2_W           _BN(20)             /*   Write-Thru=1, Write-Back=0 */
#define TLB2_I           _BN(21)             /*   Cache-Inhibited=1, Cacheable=0 */
#define TLB2_M           _BN(22)             /*   Memory Coherence Required */
#define TLB2_G           _BN(23)             /*   Guarded */
#define TLB2_E           _BN(24)             /*   Endian: 0=Big, 1=Little */
#define TLB2_UX          _BN(26)             /*   User       Execute Enable */
#define TLB2_UW          _BN(27)             /*   User       Write   Enable */
#define TLB2_UR          _BN(28)             /*   User       Read    Enable */
#define TLB2_SX          _BN(29)             /*   Supervisor Execute Enable */
#define TLB2_SW          _BN(30)             /*   Supervisor Write   Enable */
#define TLB2_SR          _BN(31)             /*   Supervisor Read    Enable */

/*  BGP Specific controls */
#define TLB2_IL3I        (TLB2_IL2I)         /*  L3 Inhibit for Instruction Fetches */
#define TLB2_IL3D        (TLB2_IL2D)         /*  L3 Inhibit for Data Accesses */
#define TLB2_IL2         (TLB2_U0)           /*  U0 is L2 Prefetch Inhibit */
#define TLB2_T           (TLB2_U1)           /*  U1 Transient Enabled is supported. */
#define TLB2_SWOA        (TLB2_U2)           /*  U2 Store WithOut Allocate is supported. */
#define TLB2_L2_PF_OPT   (TLB2_U3)           /*  U3 is L2 Optimiztic Prefetch ("Automatic" when 0) */

#endif
