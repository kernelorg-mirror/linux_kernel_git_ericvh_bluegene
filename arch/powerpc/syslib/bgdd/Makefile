#CFLAGS += -Wa,-m450

EXTRA_CFLAGS := -D__LINUX_KERNEL__ -Wno-declaration-after-statement

obj-$(CONFIG_BGP_DMA)	+= bgp_dma_spi.o


bgp_dma_spi-y := bgp_dma_base.o
bgp_dma_spi-y += spi/DMA_InjFifo.o
bgp_dma_spi-y += spi/DMA_RecFifo.o
bgp_dma_spi-y += spi/DMA_Descriptors.o
