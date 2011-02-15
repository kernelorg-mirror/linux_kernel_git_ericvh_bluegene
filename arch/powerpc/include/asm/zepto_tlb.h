/****************************************************************************/
/* ZEPTOOS:zepto-info */
/*     This file is part of ZeptoOS: The Small Linux for Big Computers.
 *     See www.mcs.anl.gov/zeptoos for more information.
 */
/* ZEPTOOS:zepto-info */
/* */
/* ZEPTOOS:zepto-fillin */
/*     $Id:  $
 *     ZeptoOS_Version: 2.0
 *     ZeptoOS_Heredity: FOSS_ORIG
 *     ZeptoOS_License: GPL
 */
/* ZEPTOOS:zepto-fillin */
/* */
/* ZEPTOOS:zepto-gpl */
/*      Copyright: Argonne National Laboratory, Department of Energy,
 *                 and UChicago Argonne, LLC.  2004, 2005, 2006, 2007, 2008
 *      ZeptoOS License: GPL
 * 
 *      This software is free.  See the file ZeptoOS/misc/license.GPL
 *      for complete details on your rights to copy, modify, and use this
 *      software.
 */
/* ZEPTOOS:zepto-gpl */
/****************************************************************************/

#ifndef __ZEPTO_TLB_PARTITION_H_DEFINED__
#define __ZEPTO_TLB_PARTITION_H_DEFINED__

/* 
   Relevant function and file:

   _tlbil_all()          @ arch/powerpc/mm/tlb_nohash_low.S
   DataTLBError()        @ arch/powerpc/kernel/head_44x.S
   InstructionTLBError() @ arch/powerpc/kernel/head_44x.S

   init_bigmem_pa()      @ arch/powerpc/mm/zepto_bigmem.c
   install_bigmem_tlb()  @ arch/powerpc/mm/zepto_bigmem.c


   NOTE:
   tlb_44x_index keeps track of next available slot, which is 
   defined in  arch/powerpc/mm/44x_mmu.c

   CONFIG_ZEPTO_LOCKBOX_UPC_TLB installs 3 TLBs
   slot
   0   lockbox super
   1   lockbox user
   2   UPC

   CONFIG_ZEPTO_TREE_TORUS_TLB installs LOCKBOX_UPC_TLB + 3 more TLBs
   3   tree0 (CIO)
   4   tree1 (MPI)
   5   DMA

   CONFIG_ZEPTO_COMPUTENODE depends on ZEPTO_TREE_TORUS_TLB (and ZEPTO_LOCKBOX_UPC_TLB)
   NOTE: it does not depend on ZEPTO_MEMORY
*/

#ifdef CONFIG_ZEPTO_TREE_TORUS_TLB

#define TLB_SLOT_AFTERDEV       6

#else

#ifdef  CONFIG_ZEPTO_LOCKBOX_UPC_TLB
#define TLB_SLOT_AFTERDEV       3
#else 
#define TLB_SLOT_AFTERDEV       0
#endif

#endif


#ifdef CONFIG_ZEPTO_MEMORY

#define BIGMEM_TLB_START_SLOT       (TLB_SLOT_AFTERDEV)
#define BIGMEM_TLB_END_SLOT         (BIGMEM_TLB_START_SLOT+8-1)   /* tentative: max 7 256MB TLBs + 16MB shm */
#define BIGMEM_N_TLBS               (BIGMEM_TLB_END_SLOT-BIGMEM_TLB_START_SLOT+1)

#define REGULAR_TLB_START_SLOT      (BIGMEM_TLB_END_SLOT+1)

#else 

#define REGULAR_TLB_START_SLOT      (TLB_SLOT_AFTERDEV)

#endif

#endif
