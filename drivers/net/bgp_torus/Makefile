# Makefile for BlueGene collective and torus driver

EXTRA_CFLAGS += -I$(BGPHOME)/bgp/arch/include -Iarch/powerpc/syslib/bgdd/ -Iarch/ppc/syslib/bgdd/ -g -dA -D__LINUX_KERNEL__

bgp_torus-y := bgp_dma_tcp_frames.o bgp_dma_tcp.o bgtornic.o torus.o bgp_dma_tcp_diagnose.o bgp_dma_ioctl.o
#  bgp_dma_memcpy.o would need a significant amout of work to make it functional
# bgp_torus-$(CONFIG_BLUEGENE_DMA_MEMCPY) += bgp_dma_memcpy.o

obj-$(CONFIG_BGP_TORUS) += bgp_torus.o
