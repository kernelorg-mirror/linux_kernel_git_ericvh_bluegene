#ifndef _LINUX_ALIGNMENT_HISTOGRAM_H
#define _LINUX_ALIGNMENT_HISTOGRAM_H

#include <linux/autoconf.h>

#if defined(CONFIG_DEBUG_ALIGNMENT_HISTOGRAM)

enum {
	k_histogram_size=16
};
struct alignment_histogram {
	int src_alignment_histogram_crc[k_histogram_size] ;
	int dst_alignment_histogram_crc[k_histogram_size] ;
	int rel_alignment_histogram_crc[k_histogram_size] ;
	int src_alignment_histogram_copy[k_histogram_size] ;
	int dst_alignment_histogram_copy[k_histogram_size] ;
	int rel_alignment_histogram_copy[k_histogram_size] ;
	int tagged[k_histogram_size] ;
	long long int qcopybytes ;
	long long int copybytes ;
	long long int copybytesshort ;
	long long int copybytesmisalign ;
	long long int copybytesbroke ;
	long long int crcbytes ;
	long long int csumpartialbytes ;
	int min_size_of_interest ;
};
extern struct alignment_histogram al_histogram ;

#define INC_AL_HISTOGRAM(Name,Address,Size) \
	{ if((Size) >= al_histogram.min_size_of_interest) { al_histogram.Name[(Address)&(k_histogram_size-1)] += 1 ; } }
#define AL_HISTOGRAM(Name,Index) (al_histogram.Name[(Index)&(k_histogram_size-1)])
#else
#define INC_AL_HISTOGRAM(Name,Address,Size)
#define AL_HISTOGRAM(Name,Index) 0
#endif

#endif
