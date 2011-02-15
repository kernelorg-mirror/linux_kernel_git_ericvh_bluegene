#EXTRA_CFLAGS += -DOFA_VERSION=141 -DCHECK_DMA_CAPABILITIES -DSIW_TX_FULLSEGS
EXTRA_CFLAGS += -DOFA_VERSION=141 
ifdef CONFIG_BLUEGENE
EXTRA_CFLAGS += -DSIW_ON_BGP
endif

obj-$(CONFIG_INFINIBAND_SOFTIWARP) += siw.o

siw-y :=  siw_main.o siw_cm.o siw_verbs.o siw_obj.o siw_qp.o siw_qp_tx.o siw_qp_rx.o siw_cq.o siw_debug.o siw_ae.o

