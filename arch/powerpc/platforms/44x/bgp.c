/*
 * Blue Gene/P board specific routines
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
#include <linux/of_platform.h>
#include <linux/root_dev.h>
#include <linux/delay.h>

#include <asm/machdep.h>
#include <asm/prom.h>
#include <asm/time.h>
#include <asm/ppc4xx.h>
#include <asm/mmu-44x.h>
#include <asm/smp.h>
#include <asm/cacheflush.h>
#include <asm/bluegene.h>
#include <asm/udbg.h>
#include <asm/bluegene_ras.h>


extern int bgWriteRasStr(unsigned int component,
                          unsigned int subcomponent,
                          unsigned int errCode,
                          char*        str,
                          unsigned int strLen);
extern int bgFlushOutboxMsgs(void);

/*
 * bgp_probe() is called very early; cpu 0 only
 *	one pinned TLB
 *	device-tree isn't unflattened
 * Look to see if the boot wrapper says we are a Blue Gene/P.
 * Setup udbg_ptc, but it will do nothing until the CNS interface is initialized.
 */
static int __init bgp_probe(void)
{
	unsigned long root = of_get_flat_dt_root();

	if (!of_flat_dt_is_compatible(root, "ibm,bluegenep"))
		return 0;

	udbg_putc = bgp_udbg_putc;
	return 1;
}

/*
 * There isn't a concept of a kernel asking to be rebooted on Blue Gene.
 * The restart, power_off and halt functions should produce RAS to tell the control
 * system this node is no longer functional.
 */
static void bgp_halt(void)
{
	bgWriteRasStr(bg_comp_kernel, bg_subcomp_linux, bg_code_halted, "System Halted", 0);

        // Flush halt RAS and any other buffered outbox messages.
        while (bgFlushOutboxMsgs());
}

static void bgp_panic(char *str)
{
	bgWriteRasStr(bg_comp_kernel, bg_subcomp_linux, bg_code_panic, str, 0);

        // Flush halt RAS and any other buffered outbox messages.
        while (bgFlushOutboxMsgs());
}

/* Blue Gene is given the decrementor frequency via the device tree (personality). */
static void __init bgp_calibrate_decr(void)
{
	struct device_node *pernode = of_find_node_by_path("/ibm,bluegene/personality");

	ppc_tb_freq = 0;
	if (pernode) {
		int len;
		const unsigned *reg = of_get_property(pernode, "frequency", &len);
		if (reg)
			ppc_tb_freq = *reg;
	}
	if (ppc_tb_freq == 0) {
		udbg_printf("personality/frequency device-tree field not found!\n");
		ppc_tb_freq = 850000000;	/* A very good default */
	}

	ppc_proc_freq = ppc_tb_freq;
	mtspr(SPRN_TSR, TSR_ENW | TSR_WIS | TSR_DIS | TSR_FIS);
	mtspr(SPRN_TCR, TCR_DIE);
}

/* Generic 44x init disables icache prefetch which can be enabled. */
static void __init bgp_enable_icache_prefetch(void)
{
	mtspr(SPRN_CCR0, mfspr(SPRN_CCR0)|2);
	isync();
	mb();
}

#ifdef CONFIG_SMP
/*
 * The Blue Gene interrupt controller (in bgp_bic.c) can implement
 * sending IPIs with a cpumask.   Consider changing this interface.
 */
static void smp_bluegene_message_pass(int target, int msg)
{
	unsigned int i;

	if (target < NR_CPUS) {
		bgp_send_ipi(target, msg);
	} else {
		for_each_online_cpu(i) {
			if (target == MSG_ALL_BUT_SELF
			    && i == smp_processor_id())
				continue;
			bgp_send_ipi(i, msg);
		}
	}
}


/* Return number of cpus possible in the system.
 * We wire this to 4 even though it may disagree with NR_CPUS.
 *
 * Also a good time to register the IPI interrupt handlers.
 * The cpu_present_map was already setup via setup_arch, so we use it.
 */
static int smp_bluegene_probe(void)
{
	return  cpus_weight(cpu_possible_map);
}

/*
 * Start a cpu by calling firmware.
 */
static void smp_bluegene_kick_cpu(int cpu)
{
	int ret = bluegene_takeCPU(cpu, 0, (void (*)(unsigned, void *))4);
	if (ret == 0) {
		cpu_set(cpu, cpu_present_map);
	} else {
		udbg_printf("CPU %d is not available (firmware returns %d)\n", cpu, ret);
	}
}

/*
 * Each secondary cpu needs some initialization.
 */
static void __init smp_bluegene_setup_cpu(int nr)
{
	int cpu = smp_processor_id();

	flush_instruction_cache();
	bgp_enable_icache_prefetch();
	bgp_init_cns();		/* map CNS for this cpu */

	bgp_init_IPI(cpu, PPC_MSG_CALL_FUNCTION);
	bgp_init_IPI(cpu, PPC_MSG_RESCHEDULE);
	bgp_init_IPI(cpu, PPC_MSG_CALL_FUNC_SINGLE);
	bgp_init_IPI(cpu, PPC_MSG_DEBUGGER_BREAK);
}

static struct smp_ops_t bluegene_smp_ops = {
	.message_pass = smp_bluegene_message_pass,
	.probe = smp_bluegene_probe,
	.kick_cpu = smp_bluegene_kick_cpu,
	.setup_cpu = smp_bluegene_setup_cpu,
};
#endif

/*
 * Initialize CNS (Common Node Services) in bgp_cns.c.
 * Once we have initialized CNS, we can crudely print messages with
 * udbg_printf().
 */
static void __init bgp_setup_arch(void)
{
	ROOT_DEV = Root_RAM0;

	bgp_enable_icache_prefetch();
	bgp_init_cns();
#ifdef CONFIG_SMP
	smp_ops = &bluegene_smp_ops;
#endif
}

define_machine(bgp) {
	.name			= "bgp",
	.probe			= bgp_probe,
	.setup_arch		= bgp_setup_arch,
	.init_IRQ		= bgp_init_IRQ,
	.get_irq		= bgp_get_irq,
	.restart		= (void (*)(char *))bgp_halt,
	.power_off		= bgp_halt,
	.halt			= bgp_halt,
	.panic			= bgp_panic,
	.calibrate_decr		= bgp_calibrate_decr,
	.progress		= udbg_progress,
};
