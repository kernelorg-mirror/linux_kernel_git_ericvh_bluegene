#ifndef __VIRTUALMAP_H_DEFINED__
#define __VIRTUALMAP_H_DEFINED__

#include <common/namespace.h>

__BEGIN_DECLS


#define _BGP_VA_SCRATCH    0xFE000000

#define _BGP_VA_BLIND      0xFFF90000
#define _BGP_VA_BLIND_TRANS  0xFFFA0000

#define _BGP_VA_TORUS0      0xFFFB0000
#define _BGP_VA_TORUS1      0xFFFC0000

#define _BGP_VA_DMA          0xFFFD0000
#define _BGP_VA_DMA0         0xFFFD0000
#define _BGP_VA_DMA1         0xFFFD1000
#define _BGP_VA_DMA2         0xFFFD2000
#define _BGP_VA_DMA3         0xFFFD3000

#define _BGP_VA_TREE0       0xFFFDC000
#define _BGP_VA_TREE1       0xFFFDD000

#define _BGP_VA_SRAM            0xFFFF8000
#define _BGP_VA_SRAM0           0xFFFF8000
#define _BGP_VA_SRAM1           0xFFFFC000
#define _BGP_VA_SRAMECC         0xFFFE0000
#define _BGP_VA_SRAM_UNCORRECTED   0xFFFE0000
#define _BGP_VA_SRAM_ECC_ACCESS       0xFFFE8000
#define _BGP_VA_SRAMERR             0xFFFDFC00

#define _BGP_VA_LOCKBOX     0xFFFF0000
#define _BGP_VA_LOCKBOX_SUP   0xFFFF0000
#define _BGP_VA_LOCKBOX_USR  0xFFFF4000

#define _BGP_VA_UPC        0xFFFDA000
#define _BGP_VA_UPC_CTL     0xFFFDB000

#define _BGP_VA_TOMAL      0xFFFD4000
#define _BGP_VA_XEMAC      0xFFFD8000

#define _BGP_VA_DEVBUS      0xFFFD9000
#define _BGP_VA_BIC        0xFFFDE000


__END_DECLS

#endif  /* #ifndef __VIRTUALMAP_H_DEFINED__ */
