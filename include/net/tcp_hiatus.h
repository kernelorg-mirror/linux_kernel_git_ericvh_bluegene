#ifndef _NET_TCP_HIATUS_H
#define _NET_TCP_HIATUS_H

/*
 * Attempt to streamline TCP. Gather statistics on tx sleeps
 */
enum {
	k_tcp_launched,  /*  Number of frames launched */
	k_tcp_wait_for_sndbuf,
	k_tcp_wait_for_memory,
	k_tcp_defer_mtu_probe,
	k_tcp_defer_cwnd_quota,
	k_tcp_defer_snd_wnd,
	k_tcp_defer_nagle,
	k_tcp_defer_should,
	k_tcp_defer_fragment,
	k_tcp_launch_failed,
	k_tcp_hiatus_reasons
};
#if defined(CONFIG_TCP_HIATUS_COUNTS)
extern int tcp_hiatus_counts[k_tcp_hiatus_reasons] ;
#endif

static inline void increment_tcp_hiatus_count(int X)
{
#if defined(CONFIG_TCP_HIATUS_COUNTS)
	tcp_hiatus_counts[X] += 1 ;
#endif
}

#endif
