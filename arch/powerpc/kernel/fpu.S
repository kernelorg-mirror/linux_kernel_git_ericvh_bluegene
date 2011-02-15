/*
 *  FPU support code, moved here from head.S so that it can be used
 *  by chips which use other head-whatever.S files.
 *
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *    Copyright (C) 1996 Cort Dougan <cort@cs.nmt.edu>
 *    Copyright (C) 1996 Paul Mackerras.
 *    Copyright (C) 1997 Dan Malek (dmalek@jlc.net).
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 *    Revised 2011 Chris Ward <tjcw@uk.ibm.com>
 *  To improve the ability to use floating-point registers in the kernel,
 *  I have added code to hold the interrupt mask around some instructions
 *  in 'giveup_fpu()'. The specific case needing this is
 *  'Zepto' linux for BlueGene/P, where floating-point registers are
 *  needed in a 'tasklet' as part of the bgp_collective network driver,
 *  and floating-point registers are wanted as part of an optimisation
 *  to 'copy_tofrom_user'. The observed problem (without the interrupt mask)
 *  seemed to be that the code could be interrupted after the "mffs fr0"
 *  and before the ownership of the fp regs had been disclaimed; then
 *  'giveup_fpu()' could be called again as a consequence of the interrupt.
 *  When fr0 then got saved in the task struct, it would overwrite the
 *  proper fr0 value; and when the task was subspequently dispatched it
 *  would find an unexpected value in fr0.
 *
 *  There is some discussion (Benjamin Herrenschmidt <benh@kernel.crashing.org>)
 *  as to whether the code here is sufficient to achieve the intent, and
 *  as to how future-proof it is; current state of discussion is
 *
 * > > Reentering giveup_fpu() is only a problem if you can get a different
 * > > answer the second time from the first time; and the only way you can
 * > > get a different answer is if you reenter it while fr0 has the contents
 * > > of fpscr in it, instead of the application's fr0.
 * >
 * > That's very fishy ... in any case, just don't do it. FPU in the kernel
 * > is already close to the limit of what's acceptable, doing it in
 * > interrupt context is way beyond that limit, at least if using the
 * > standard FPU primitives.
 * >
 * > Do as I said earlier, that is save/restore what you use, if you really
 * > want to use it there.
 * >
 *
 */

#include <asm/reg.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/pgtable.h>
#include <asm/cputable.h>
#include <asm/cache.h>
#include <asm/thread_info.h>
#include <asm/ppc_asm.h>
#include <asm/asm-offsets.h>

/* #define DISABLE_INTERRUPTS_DURING_LOAD_UP_FPU */
#define DISABLE_INTERRUPTS_DURING_GIVEUP_FPU

#ifdef CONFIG_VSX
#define REST_32FPVSRS(n,c,base)						\
BEGIN_FTR_SECTION							\
	b	2f;							\
END_FTR_SECTION_IFSET(CPU_FTR_VSX);					\
	REST_32FPRS(n,c,base);						\
	b	3f;							\
2:	REST_32VSRS(n,c,base);						\
3:

#define SAVE_32FPVSRS(n,c,base)						\
BEGIN_FTR_SECTION							\
	b	2f;							\
END_FTR_SECTION_IFSET(CPU_FTR_VSX);					\
	SAVE_32FPRS(n,c,base);						\
	b	3f;							\
2:	SAVE_32VSRS(n,c,base);						\
3:
#else
#define REST_32FPVSRS(n,b,base)	REST_32FPRS(n,b,base)
#define SAVE_32FPVSRS(n,b,base)	SAVE_32FPRS(n,b,base)
#endif

/*
 * This task wants to use the FPU now.
 * On UP, disable FP for the task which had the FPU previously,
 * and save its floating-point registers in its thread_struct.
 * Load up this task's FP registers from its thread_struct,
 * enable the FPU for the current task and return to the task.
 */
_GLOBAL(load_up_fpu)
	mfmsr	r6
	ori	r6,r6,MSR_FP
#ifdef CONFIG_VSX
BEGIN_FTR_SECTION
	oris	r6,r6,MSR_VSX@h
END_FTR_SECTION_IFSET(CPU_FTR_VSX)
#endif
	SYNC
	MTMSRD(r6)			/* enable use of fpu now */
	isync
#if defined(DISABLE_INTERRUPTS_DURING_LOAD_UP_FPU)
	wrteei 0
#endif
/*
 * For SMP, we don't do lazy FPU switching because it just gets too
 * horrendously complex, especially when a task switches from one CPU
 * to another.  Instead we call giveup_fpu in switch_to.
 */
#ifndef CONFIG_SMP
	LOAD_REG_ADDRBASE(r3, last_task_used_math)
	toreal(r3)
	PPC_LL	r4,ADDROFF(last_task_used_math)(r3)
	PPC_LCMPI	0,r4,0
	beq	1f
	toreal(r4)
	addi	r4,r4,THREAD		/* want last_task_used_math->thread */
	SAVE_32FPVSRS(0, r5, r4)
	mffs	fr0
	stfd	fr0,THREAD_FPSCR(r4)
	PPC_LL	r5,PT_REGS(r4)
	toreal(r5)
	PPC_LL	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
	li	r10,MSR_FP|MSR_FE0|MSR_FE1
	andc	r4,r4,r10		/* disable FP for previous task */
	PPC_STL	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
1:
#endif /* CONFIG_SMP */
	/* enable use of FP after return */
#ifdef CONFIG_PPC32
	mfspr	r5,SPRN_SPRG3		/* current task's THREAD (phys) */
	lwz	r4,THREAD_FPEXC_MODE(r5)
	ori	r9,r9,MSR_FP		/* enable FP for current */
	or	r9,r9,r4
#else
	ld	r4,PACACURRENT(r13)
	addi	r5,r4,THREAD		/* Get THREAD */
	lwz	r4,THREAD_FPEXC_MODE(r5)
	ori	r12,r12,MSR_FP
	or	r12,r12,r4
	std	r12,_MSR(r1)
#endif
	lfd	fr0,THREAD_FPSCR(r5)
	MTFSF_L(fr0)
	REST_32FPVSRS(0, r4, r5)
#ifndef CONFIG_SMP
	subi	r4,r5,THREAD
	fromreal(r4)
	PPC_STL	r4,ADDROFF(last_task_used_math)(r3)
#endif /* CONFIG_SMP */
#if defined(DISABLE_INTERRUPTS_DURING_LOAD_UP_FPU)
	wrtee r6			/* restore the interrupt mask */
#endif
	/* restore registers and return */
	/* we haven't used ctr or xer or lr */
	blr

/*
 * giveup_fpu(tsk)
 * Disable FP for the task given as the argument,
 * and save the floating-point registers in its thread_struct.
 * Enables the FPU for use in the kernel on return.
 */
_GLOBAL(giveup_fpu)
	mfmsr	r6
	ori	r6,r6,MSR_FP
#ifdef CONFIG_VSX
BEGIN_FTR_SECTION
	oris	r6,r6,MSR_VSX@h
END_FTR_SECTION_IFSET(CPU_FTR_VSX)
#endif
	SYNC_601
	ISYNC_601
	MTMSRD(r6)			/* enable use of fpu now */
	SYNC_601
	isync
	PPC_LCMPI	0,r3,0
	beqlr-				/* if no previous owner, done */
	addi	r3,r3,THREAD	        /* want THREAD of task */
	PPC_LL	r5,PT_REGS(r3)
	PPC_LCMPI	0,r5,0
	SAVE_32FPVSRS(0, r4 ,r3)
#if defined(DISABLE_INTERRUPTS_DURING_GIVEUP_FPU)
	wrteei 0	/* Disable interupts while fr0 is inconsistent with what the application thinks should be there */
#endif
	mffs	fr0
	stfd	fr0,THREAD_FPSCR(r3)
	beq	1f
	PPC_LL	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
	li	r3,MSR_FP|MSR_FE0|MSR_FE1
	andc	r4,r4,r3		/* disable FP for previous task */
	PPC_STL	r4,_MSR-STACK_FRAME_OVERHEAD(r5)
1:
#ifndef CONFIG_SMP
	li	r5,0
	LOAD_REG_ADDRBASE(r4,last_task_used_math)
	PPC_STL	r5,ADDROFF(last_task_used_math)(r4)
#endif /* CONFIG_SMP */
#if defined(DISABLE_INTERRUPTS_DURING_GIVEUP_FPU)
	wrtee r6	/* Restore the interrupt mask */
#endif
	blr

/*
 * These are used in the alignment trap handler when emulating
 * single-precision loads and stores.
 * We restore and save the fpscr so the task gets the same result
 * and exceptions as if the cpu had performed the load or store.
 */

_GLOBAL(cvt_fd)
	lfd	0,THREAD_FPSCR(r5)	/* load up fpscr value */
	MTFSF_L(0)
	lfs	0,0(r3)
	stfd	0,0(r4)
	mffs	0
	stfd	0,THREAD_FPSCR(r5)	/* save new fpscr value */
	blr

_GLOBAL(cvt_df)
	lfd	0,THREAD_FPSCR(r5)	/* load up fpscr value */
	MTFSF_L(0)
	lfd	0,0(r3)
	stfs	0,0(r4)
	mffs	0
	stfd	0,THREAD_FPSCR(r5)	/* save new fpscr value */
	blr
