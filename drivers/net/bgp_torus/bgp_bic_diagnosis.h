/* These are defined by the hardware. */
#define NR_BIC_GROUPS 15
#define NR_BIC_GINTS 32
#define NR_BIC_CPUS 4

/* 4-bit target value for target register */
#define BIC_TARGET_MASK (0xf)
#define BIC_TARGET_TYPE_NORMAL (1<<2)
#define BIC_TARGET_NORMAL(cpu) (BIC_TARGET_TYPE_NORMAL|(cpu))
#define BIC_DEFAULT_CPU 0

/* Define the layout of each group's registers.
 * This layout should be 0x80 bytes long (including pad).
 */
struct bic_group_regs {
  uint32_t status;      /* 0x00  RW */
  uint32_t rd_clr_status;     /* 0x04  RO */
  uint32_t status_clr;      /* 0x08  WO */
  uint32_t status_set;      /* 0x0c  WO */
  uint32_t target[4];     /* 0x10  RW */
  uint32_t normal[NR_BIC_CPUS];   /* 0x20  RW */
  uint32_t critical[NR_BIC_CPUS];   /* 0x30  RW */
  uint32_t mcheck[NR_BIC_CPUS];   /* 0x40  RW */
  uint32_t _pad[12];      /* 0x50     */
};

/* Define the layout of the interrupt controller mem mapped regs. */
struct bic_regs {
  struct bic_group_regs group[NR_BIC_GROUPS];   /* 0x000 */
  uint32_t hier_normal[NR_BIC_CPUS];      /* 0x780 */
  uint32_t hier_critical[NR_BIC_CPUS];      /* 0x790 */
  uint32_t hier_mcheck[NR_BIC_CPUS];      /* 0x7a0 */
};

struct bic {
        spinlock_t mask_lock;   /* could be finer grained if necessary */
        struct bic_regs *regs;
} ;

extern volatile struct bic bic;

/* void show_bic_regs(void) ; // diagnostic 'printk' of the BIC */
static void show_bic_group(int g, volatile struct bic_group_regs* gp) __attribute__ ((unused)) ;
static void show_bic_group(int g, volatile struct bic_group_regs* gp)
{
   printk(KERN_NOTICE "bic_group_regs[%d] status=%08x target=[%08x %08x %08x %08x]\n",g,gp->status, gp->target[0], gp->target[1], gp->target[2], gp->target[3]) ;
   printk(KERN_NOTICE "bic_group_regs[%d] normal=[%08x %08x %08x %08x] critical=[%08x %08x %08x %08x] mcheck=[%08x %08x %08x %08x]\n",g, gp->normal[0], gp->normal[1], gp->normal[2], gp->normal[3], gp->critical[0],gp->critical[1],gp->critical[2],gp->critical[3],gp->mcheck[0],gp->mcheck[1],gp->mcheck[2],gp->mcheck[3]) ;
}

static void show_bic_regs(void) __attribute__ ((unused)) ;
static void show_bic_regs(void)
{
  struct bic_regs * bic_regs = bic.regs ;
  int g ;
  for( g = 0 ; g < NR_BIC_GROUPS ; g += 1 )
     {
        show_bic_group(g,bic_regs->group+g) ;
     }
  printk(KERN_NOTICE "BIC hier_normal=%08x %08x %08x %08x\n",
        bic_regs->hier_normal[0],
        bic_regs->hier_normal[1],
        bic_regs->hier_normal[2],
        bic_regs->hier_normal[3]) ;
  printk(KERN_NOTICE "BIC hier_critical=%08x %08x %08x %08x\n",
        bic_regs->hier_critical[0],
        bic_regs->hier_critical[1],
        bic_regs->hier_critical[2],
        bic_regs->hier_critical[3]) ;
  printk(KERN_NOTICE "BIC hier_mcheck=%08x %08x %08x %08x\n",
        bic_regs->hier_mcheck[0],
        bic_regs->hier_mcheck[1],
        bic_regs->hier_mcheck[2],
        bic_regs->hier_mcheck[3]) ;

}
