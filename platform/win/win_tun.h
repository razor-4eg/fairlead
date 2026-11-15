//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// win_tun.h -- The mihomo process the helper owns. One session, brought up and down from the pipe.
//
//===============================================================

#ifndef WIN_TUN_H
#define WIN_TUN_H

/* One mihomo session. mihomo owns the TUN adapter, the routes, DNS, and WireGuard. */

typedef struct win_tun_result_s {
	int ok;
	char error[768];
	char iface[64];
	char gateway_v4[64];
	char lan[8][64];
	int lan_count;
	char warnings[12][256];
	int warning_count;
	unsigned long long rx;
	unsigned long long tx;
	unsigned long long down_bps;	/* Instant download rate from helper samples. */
	unsigned long long up_bps;	/* Instant upload rate from helper samples. */
	int handshake;	/* Unix time of the last successful HTTP probe through the tunnel. 0 when stale/failed. */
	int delay_ms;	/* Last HTTP probe RTT through the tunnel. Used for liveness only. */
	int ping_ms;	/* ICMP echo RTT to the WireGuard endpoint. Shown in the UI. */
	char live_endpoint[160];
	char public_key[128];
	char stage[96];	/* Progress while Connecting. Empty once the tunnel is reachable. */
} win_tun_result_t;

int		win_tun_up(const char *slug, const char *config,
			const char **cidrs, int cidr_count,
			const char **domains, int domain_count,
			const char **apps, int app_count,
			int exclude_lan, int selected, const char *exe_dir, win_tun_result_t *out);
int		win_tun_down(win_tun_result_t *out);
int		win_tun_bypass(
			const char **cidrs, int cidr_count,
			const char **domains, int domain_count,
			const char **apps, int app_count,
			int exclude_lan, int selected, win_tun_result_t *out);
int		win_tun_active(void);
void	win_tun_status(win_tun_result_t *out);
void	win_tun_reconcile(void);

#endif
