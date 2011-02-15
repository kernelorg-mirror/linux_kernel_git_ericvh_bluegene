# Makefile for BlueGene collective and torus driver

EXTRA_CFLAGS += -I$(BGPHOME)/bgp/arch/include -Iarch/powerpc/syslib/bgdd/ -Iarch/ppc/syslib/bgdd/ -g -dA -D__LINUX_KERNEL__ -save-temps

bgp_memcpy-y := bgp_fpu_memcpy.o

obj-$(CONFIG_WRAP_COPY_TOFROM_USER) += bgp_memcpy.o
