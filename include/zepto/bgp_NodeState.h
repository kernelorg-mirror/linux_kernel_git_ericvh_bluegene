#ifndef	__BGP_NODESTATE_H_DEFINE__
#define	__BGP_NODESTATE_H_DEFINE__

#include <common/namespace.h>

__BEGIN_DECLS

extern inline int _bgp_GetInitCore(void)
{
    return 0;
}
extern inline int _bgp_GetRunningCores(void)
{
    return 4;
}


__END_DECLS

#endif 
