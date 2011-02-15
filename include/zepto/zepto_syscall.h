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

#ifndef __ZEPTO_SYSCALL_H_DEFINED__
#define __ZEPTO_SYSCALL_H_DEFINED__

/*
  Systemcall related files:

  include/asm-powerpc/unistd.h
  include/zepto/zepto-syscall.h
  arch/powerpc/kernel/systbl.S
  arch/ppc/mm/zepto_bigmem.c
  arch/ppc/syslib/bgdd/zepto_bluegene_dma.c
  arch/ppc/syslib/bgdd/zepto_bluegene_lockbox.c
*/

#define ZEPTO_GENERIC_SYSCALL_NO     (1048)
#define ZEPTO_BIGMEM_SYSCALL_NO      (1049)
#define ZEPTO_LOCKBOX_SYSCALL_NO     (1050)
#define ZEPTO_DMA_SYSCALL_NO         (1051)          


/* sys_zepto_generic() in arch/ppc/mm/zepto_bigmem.c */
enum { 
    ZEPTOSC_NULL = 100,        /* return 0. do nothing */
    ZEPTOSC_FLIP,              /* return (~val). this is used to check the zepto kernel */
    ZEPTOSC_COREID,            /* return coreid. val is unused */
    ZEPTOSC_ZEPTO_TASK,        /* return 1 if the current task is a zepto task */
    ZEPTOSC_GETDEC,            /* return decrementer value */
};

/* sys_zepto_bigmem() in arch/ppc/mm/zepto_bigmem.c */
enum {
    ZEPTOSC_BIGMEM_N_SEGS = 200,     /* return n_segs(# of bigmem segments) (currently 1). val is unused */
    ZEPTOSC_BIGMEM_VA_START,         /* val is the seg number.  [coreid*n_segs, coreid*(n_segs+1) ) */
    ZEPTOSC_BIGMEM_PA_START,
    ZEPTOSC_BIGMEM_LEN,
    ZEPTOSC_SCRATCHPAD_VA_START = 300,
    ZEPTOSC_SCRATCHPAD_PA_START,
    ZEPTOSC_SCRATCHPAD_LEN,
};

/* sys_zepto_lockbox() in arch/ppc/syslib/bgdd/zepto_bluegene_lockbox.c */
enum {
    ZEPTOSC_LOCKBOX_ALLOCATE = 300,
    ZEPTOSC_LOCKBOX_RESET,
};

/* sys_zepto_dma() in arch/ppc/syslib/bgdd/zepto_bluegene_dma.c */
enum {
    ZEPTOSC_DMA_COUNTERGROUPQUERYFREE = 400,
    ZEPTOSC_DMA_COUNTERGROUPALLOCATE,
    ZEPTOSC_DMA_INJFIFOGROUPQUERYFREE,
    ZEPTOSC_DMA_INJFIFOGROUPALLOCATE,   
    ZEPTOSC_DMA_INJFIFOINITBYID,
    ZEPTOSC_DMA_RECFIFOSETMAP,
    ZEPTOSC_DMA_RECFIFOGETFIFOGROUP,
    ZEPTOSC_DMA_RECFIFOINITBYID,
    ZEPTOSC_DMA_CHGCOUNTERINTERRUPTENABLES,
};


/* AllocateLockBox_struct is used in arch/ppc/syslib/bgdd/zepto_bluegene_lockbox.c */

#define ALLOCATELOCKBOX_MAX_LOCK (32)  /* lockbox barrier requires 5 locks. */
struct AllocateLockBox_struct
{
    unsigned   locknum;
    unsigned   numlocks; 
    unsigned   lockbox_va[ALLOCATELOCKBOX_MAX_LOCK]; 
    unsigned   flags;
};


/* DMA related struct used in arch/ppc/syslib/bgdd/zepto_bluegene_dma.c */

struct CounterGroupQueryFree_struct {
    unsigned  type;
    unsigned  group;
    unsigned  n_subgroups;   /* is filled by kernel code */
    unsigned  *subgroups;    /* pointer to an user buffer */
};

struct CounterGroupAllocate_struct {
    unsigned  type;
    unsigned  group;
    unsigned  num_subgroups;
    unsigned* subgroups;     /* points to an user buffer, read-only */
    unsigned* cg_ptr;        /* points to a special buffer */
};

struct InjFifoGroupQueryFree_struct {
    unsigned   group;
    unsigned   num_fifos;     /* altered by kernel */
    unsigned*  fifo_ids;      /* points to an user buffer, filled by kernel */
};

struct InjFifoGroupAllocate_struct {
    unsigned  group;
    unsigned  num_fifos;
    unsigned* fifo_ids;     /* points to an user buffer */
    unsigned short* priorities;   /* points to an user buffer */
    unsigned short* locals;       /* points to an user buffer */
    unsigned char*  ts_inj_maps;  /* points to an user buffer */
    unsigned* fg_ptr;       /* points to an special buffer */
};

struct InjFifoInitByID_struct {
    unsigned* fg_ptr;
    int       fifo_id;
    unsigned* va_start;    
    unsigned* va_head;     
    unsigned* va_end;      
};

struct RecFifoGetFifoGroup_struct {
    unsigned*  fg_ptr;
    int   group;
    int   target;
};

struct RecFifoInitByID_struct {
    unsigned*     fg_ptr;
    int           fifo_id;
    void          *va_start;
    void          *va_head;
    void          *va_end;
};

#endif
