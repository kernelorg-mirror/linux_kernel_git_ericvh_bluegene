/*
 * Common prep/pmac/chrp boot and setup code.
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/reboot.h>
#include <linux/delay.h>
#include <linux/initrd.h>
#include <linux/tty.h>
#include <linux/bootmem.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>
#include <linux/cpu.h>
#include <linux/console.h>
#include <linux/lmb.h>

#include <asm/io.h>
#include <asm/prom.h>
#include <asm/processor.h>
#include <asm/pgtable.h>
#include <asm/setup.h>
#include <asm/smp.h>
#include <asm/elf.h>
#include <asm/cputable.h>
#include <asm/bootx.h>
#include <asm/btext.h>
#include <asm/machdep.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/pmac_feature.h>
#include <asm/sections.h>
#include <asm/nvram.h>
#include <asm/xmon.h>
#include <asm/time.h>
#include <asm/serial.h>
#include <asm/udbg.h>
#include <asm/mmu_context.h>

#include "setup.h"

#define DBG(fmt...)

#ifdef CONFIG_ZEPTO
#include <linux/utsname.h>
int zepto_debug_level = 1;

#endif


extern void bootx_init(unsigned long r4, unsigned long phys);

int boot_cpuid;
EXPORT_SYMBOL_GPL(boot_cpuid);
int boot_cpuid_phys;

int smp_hw_index[NR_CPUS];

unsigned long ISA_DMA_THRESHOLD;
unsigned int DMA_MODE_READ;
unsigned int DMA_MODE_WRITE;

#ifdef CONFIG_VGA_CONSOLE
unsigned long vgacon_remap_base;
EXPORT_SYMBOL(vgacon_remap_base);
#endif

/*
 * These are used in binfmt_elf.c to put aux entries on the stack
 * for each elf executable being started.
 */
int dcache_bsize;
int icache_bsize;
int ucache_bsize;

#ifdef CONFIG_ZEPTO
/* XXX: this might not be an approrpite location to add this code. */
int zepto_kparam_noPRE;
int zepto_kparam_noU3;
int zepto_kparam_globaltick;
int zepto_kparam_tickdesync;
#endif


/*
 * We're called here very early in the boot.  We determine the machine
 * type and call the appropriate low-level setup functions.
 *  -- Cort <cort@fsmlabs.com>
 *
 * Note that the kernel may be running at an address which is different
 * from the address that it was linked at, so we must use RELOC/PTRRELOC
 * to access static data (including strings).  -- paulus
 */
notrace unsigned long __init early_init(unsigned long dt_ptr)
{
	unsigned long offset = reloc_offset();
	struct cpu_spec *spec;

	/* First zero the BSS -- use memset_io, some platforms don't have
	 * caches on yet */
	memset_io((void __iomem *)PTRRELOC(&__bss_start), 0,
			__bss_stop - __bss_start);

	/*
	 * Identify the CPU type and fix up code sections
	 * that depend on which cpu we have.
	 */
	spec = identify_cpu(offset, mfspr(SPRN_PVR));

	do_feature_fixups(spec->cpu_features,
			  PTRRELOC(&__start___ftr_fixup),
			  PTRRELOC(&__stop___ftr_fixup));

	do_feature_fixups(spec->mmu_features,
			  PTRRELOC(&__start___mmu_ftr_fixup),
			  PTRRELOC(&__stop___mmu_ftr_fixup));

	do_lwsync_fixups(spec->cpu_features,
			 PTRRELOC(&__start___lwsync_fixup),
			 PTRRELOC(&__stop___lwsync_fixup));

	return KERNELBASE + offset;
}


/*
 * Find out what kind of machine we're on and save any data we need
 * from the early boot process (devtree is copied on pmac by prom_init()).
 * This is called very early on the boot process, after a minimal
 * MMU environment has been set up but before MMU_init is called.
 */
notrace void __init machine_init(unsigned long dt_ptr)
{
	/* Enable early debugging if any specified (see udbg.h) */
	udbg_early_init();

	/* Do some early initialization based on the flat device tree */
	early_init_devtree(__va(dt_ptr));

	probe_machine();

	setup_kdump_trampoline();

#ifdef CONFIG_6xx
	if (cpu_has_feature(CPU_FTR_CAN_DOZE) ||
	    cpu_has_feature(CPU_FTR_CAN_NAP))
		ppc_md.power_save = ppc6xx_idle;
#endif

#ifdef CONFIG_E500
	if (cpu_has_feature(CPU_FTR_CAN_DOZE) ||
	    cpu_has_feature(CPU_FTR_CAN_NAP))
		ppc_md.power_save = e500_idle;
#endif
	if (ppc_md.progress)
		ppc_md.progress("id mach(): done", 0x200);
}

#ifdef CONFIG_BOOKE_WDT
/* Checks wdt=x and wdt_period=xx command-line option */
notrace int __init early_parse_wdt(char *p)
{
	if (p && strncmp(p, "0", 1) != 0)
	       booke_wdt_enabled = 1;

	return 0;
}
early_param("wdt", early_parse_wdt);

int __init early_parse_wdt_period (char *p)
{
	if (p)
		booke_wdt_period = simple_strtoul(p, NULL, 0);

	return 0;
}
early_param("wdt_period", early_parse_wdt_period);
#endif	/* CONFIG_BOOKE_WDT */

/* Checks "l2cr=xxxx" command-line option */
int __init ppc_setup_l2cr(char *str)
{
	if (cpu_has_feature(CPU_FTR_L2CR)) {
		unsigned long val = simple_strtoul(str, NULL, 0);
		printk(KERN_INFO "l2cr set to %lx\n", val);
		_set_L2CR(0);		/* force invalidate by disable cache */
		_set_L2CR(val);		/* and enable it */
	}
	return 1;
}
__setup("l2cr=", ppc_setup_l2cr);

/* Checks "l3cr=xxxx" command-line option */
int __init ppc_setup_l3cr(char *str)
{
	if (cpu_has_feature(CPU_FTR_L3CR)) {
		unsigned long val = simple_strtoul(str, NULL, 0);
		printk(KERN_INFO "l3cr set to %lx\n", val);
		_set_L3CR(val);		/* and enable it */
	}
	return 1;
}
__setup("l3cr=", ppc_setup_l3cr);

#ifdef CONFIG_GENERIC_NVRAM

/* Generic nvram hooks used by drivers/char/gen_nvram.c */
unsigned char nvram_read_byte(int addr)
{
	if (ppc_md.nvram_read_val)
		return ppc_md.nvram_read_val(addr);
	return 0xff;
}
EXPORT_SYMBOL(nvram_read_byte);

void nvram_write_byte(unsigned char val, int addr)
{
	if (ppc_md.nvram_write_val)
		ppc_md.nvram_write_val(addr, val);
}
EXPORT_SYMBOL(nvram_write_byte);

void nvram_sync(void)
{
	if (ppc_md.nvram_sync)
		ppc_md.nvram_sync();
}
EXPORT_SYMBOL(nvram_sync);

#endif /* CONFIG_NVRAM */

int __init ppc_init(void)
{
	/* clear the progress line */
	if (ppc_md.progress)
		ppc_md.progress("             ", 0xffff);

	/* call platform init */
	if (ppc_md.init != NULL) {
		ppc_md.init();
	}
	return 0;
}

arch_initcall(ppc_init);

#ifdef CONFIG_IRQSTACKS
static void __init irqstack_early_init(void)
{
	unsigned int i;

	/* interrupt stacks must be in lowmem, we get that for free on ppc32
	 * as the lmb is limited to lowmem by LMB_REAL_LIMIT */
	for_each_possible_cpu(i) {
		softirq_ctx[i] = (struct thread_info *)
			__va(lmb_alloc(THREAD_SIZE, THREAD_SIZE));
		hardirq_ctx[i] = (struct thread_info *)
			__va(lmb_alloc(THREAD_SIZE, THREAD_SIZE));
	}
}
#else
#define irqstack_early_init()
#endif

#if defined(CONFIG_BOOKE) || defined(CONFIG_40x)
static void __init exc_lvl_early_init(void)
{
	unsigned int i;

	/* interrupt stacks must be in lowmem, we get that for free on ppc32
	 * as the lmb is limited to lowmem by LMB_REAL_LIMIT */
	for_each_possible_cpu(i) {
		critirq_ctx[i] = (struct thread_info *)
			__va(lmb_alloc(THREAD_SIZE, THREAD_SIZE));
#ifdef CONFIG_BOOKE
		dbgirq_ctx[i] = (struct thread_info *)
			__va(lmb_alloc(THREAD_SIZE, THREAD_SIZE));
		mcheckirq_ctx[i] = (struct thread_info *)
			__va(lmb_alloc(THREAD_SIZE, THREAD_SIZE));
#endif
	}
}
#else
#define exc_lvl_early_init()
#endif

/* Warning, IO base is not yet inited */
void __init setup_arch(char **cmdline_p)
{
	*cmdline_p = cmd_line;

	/* so udelay does something sensible, assume <= 1000 bogomips */
	loops_per_jiffy = 500000000 / HZ;

	unflatten_device_tree();
	check_for_initrd();

	if (ppc_md.init_early)
		ppc_md.init_early();

#ifdef CONFIG_ZEPTO
	{
	    char* optstr = "zepto_debug=";
	    if(strstr(cmd_line, optstr) ) {
		char* p;
		p = strstr( cmd_line, optstr );
		if( p && (strlen(p)-strlen(optstr))>0 ) { 
		    p=p+strlen(optstr);
		    zepto_debug_level=simple_strtoul(p,&p,0);
		}
	    }
	}

	if(strstr(cmd_line,"noPRE")) zepto_kparam_noPRE = 1;	
	else  		zepto_kparam_noPRE = 0;

	if(strstr(cmd_line,"noU3")) zepto_kparam_noU3 = 1;	
	else  		zepto_kparam_noU3 = 0;

#ifdef  CONFIG_ZEPTO_COMPUTENODE
	if(strstr(cmd_line,"globaltick")) zepto_kparam_globaltick = 1;
	else  		zepto_kparam_globaltick = 0;

	if(strstr(cmd_line,"tickdesync")) zepto_kparam_tickdesync = 1;
	else  		zepto_kparam_tickdesync = 0;
#endif

#endif


	find_legacy_serial_ports();

	smp_setup_cpu_maps();

	/* Register early console */
	register_early_udbg_console();

	xmon_setup();

	/*
	 * Set cache line size based on type of cpu as a default.
	 * Systems with OF can look in the properties on the cpu node(s)
	 * for a possibly more accurate value.
	 */
	dcache_bsize = cur_cpu_spec->dcache_bsize;
	icache_bsize = cur_cpu_spec->icache_bsize;
	ucache_bsize = 0;
	if (cpu_has_feature(CPU_FTR_UNIFIED_ID_CACHE))
		ucache_bsize = icache_bsize = dcache_bsize;

	/* reboot on panic */
	panic_timeout = 180;

	if (ppc_md.panic)
		setup_panic();

	init_mm.start_code = (unsigned long)_stext;
	init_mm.end_code = (unsigned long) _etext;
	init_mm.end_data = (unsigned long) _edata;
	init_mm.brk = klimit;

	exc_lvl_early_init();

	irqstack_early_init();

	/* set up the bootmem stuff with available memory */
	do_init_bootmem();
	if ( ppc_md.progress ) ppc_md.progress("setup_arch: bootmem", 0x3eab);

#ifdef CONFIG_DUMMY_CONSOLE
	conswitchp = &dummy_con;
#endif

	if (ppc_md.setup_arch)
		ppc_md.setup_arch();

#ifdef CONFIG_ZEPTO

	/* CNS is initialized in setup_arch(). we can start using print functions from here*/ 
	{
	    extern unsigned long __bigmem_size; /* defined in  arch/powerpc/mm/init_32.c */

	    printk("Z: Zepto patched BGP %s %s %s\n", 
		   utsname()->sysname,
		   utsname()->release,
		   utsname()->version);
	    printk("Z: zepto_debug_level=%d\n",zepto_debug_level);
	    printk("Z: __bigmem_size=%ld\n", __bigmem_size);
	    printk("Z: options: %s%s%s%s\n",
		   zepto_kparam_noPRE?"noPRE ":"", 
		   zepto_kparam_noU3?"noU3 ":"", 
		   zepto_kparam_globaltick?"globaltick ":"",
		   zepto_kparam_tickdesync?"tickdesync ":"");
	    lmb_dump_all();
	    printk("Z: lmb_phys_mem_size()=>%08x  lmb_end_of_DRAM()=>%08x\n",
		   (unsigned)lmb_phys_mem_size(), (unsigned)lmb_end_of_DRAM() );
	}
#endif

	if ( ppc_md.progress ) ppc_md.progress("arch: exit", 0x3eab);


	paging_init();

	/* Initialize the MMU context management stuff */
	mmu_context_init();

}
