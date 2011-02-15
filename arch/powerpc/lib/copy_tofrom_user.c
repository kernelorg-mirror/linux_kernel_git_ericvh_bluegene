#include <linux/kernel.h>

extern unsigned long __real__copy_tofrom_user(void  *to,
		const void __user *from, unsigned long size) ;

#if defined(CONFIG_BGP_TORUS)
extern unsigned long bgp_fpu_instrument_copy_tofrom_user(void  *to,
		const void __user *from, unsigned long size) ;
#endif

unsigned long __copy_tofrom_user(void  *to,
		const void __user *from, unsigned long size)
{
#if defined(CONFIG_BGP_TORUS)
	int rc=bgp_fpu_instrument_copy_tofrom_user(to, from, size) ;
	if( 0 == rc) return 0 ;
#endif
	return __real__copy_tofrom_user(to, from, size) ;
}
