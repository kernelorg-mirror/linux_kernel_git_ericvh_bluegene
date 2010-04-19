# Makefile for BlueGene collective and torus driver

EXTRA_CFLAGS += -I$(BGPHOME)/bgp/arch/include -Iarch/powerpc/syslib/bgdd/ -Iarch/ppc/syslib/bgdd/ -g -dA -D__LINUX_KERNEL__

bgp_collective-y := bgcol.o bgnet.o

obj-$(CONFIG_BGP_COLLECTIVE) += bgp_collective.o
