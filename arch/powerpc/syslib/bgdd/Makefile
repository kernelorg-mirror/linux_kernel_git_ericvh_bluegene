#CFLAGS += -Wa,-m450
 
EXTRA_CFLAGS := -D__LINUX_KERNEL__ -Wno-declaration-after-statement

obj-$(CONFIG_ZEPTO)             += zepto_bluegene_lockbox.o
obj-$(CONFIG_ZEPTO_MEMORY)      += zepto_bigmem_explicit_mmap.o zepto_task.o zepto_setup_treeroute.o 
obj-$(CONFIG_ZEPTO_COMPUTENODE) += zepto_bluegene_dma.o 
obj-$(CONFIG_BGP_DMA)	        += bgp_dma_spi.o

ZSPI_INC=-Iarch/powerpc/include/zspi/
CFLAGS_zepto_bigmem_explicit_mmap.o=$(ZSPI_INC)
CFLAGS_zepto_task.o=$(ZSPI_INC)
CFLAGS_zepto_bluegene_dma.o=$(ZSPI_INC)
CFLAGS_zepto_bluegene_lockbox.o=$(ZSPI_INC)
CFLAGS_zepto_setup_treeroute.o=$(ZSPI_INC)



bgp_dma_spi-y := bgp_dma_base.o 
bgp_dma_spi-y += spi/DMA_InjFifo.o
bgp_dma_spi-y += spi/DMA_RecFifo.o  
bgp_dma_spi-y += spi/DMA_Descriptors.o 
