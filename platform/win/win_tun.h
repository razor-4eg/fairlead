//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// win_tun.h -- The tunnel the helper process owns. One session, brought up and down from the pipe.
//
//===============================================================

#ifndef WIN_TUN_H
#define WIN_TUN_H

/* One userspace tunnel. The helper process owns this. */

typedef struct win_tun_result_s {
	int ok;
	char error[768];
	char iface[64];
	char gateway_v4[64];
	char gateway_iface[128];
	char lan[8][64];
	int lan_count;
	char endpoints[4][64];
	int endpoint_count;
	char warnings[12][256];
	int warning_count;
	char owned[48][64];
	int owned_count;
	unsigned long long rx;
	unsigned long long tx;
	int handshake;
	char live_endpoint[160];
	char public_key[128];
} win_tun_result_t;

int		win_tun_up(const char *slug, const char *config, const char **cidrs, int cidr_count, int exclude_lan, int selected, const char *exe_dir, win_tun_result_t *out);
int		win_tun_down(win_tun_result_t *out);
int		win_tun_bypass(const char **cidrs, int cidr_count, int exclude_lan, int selected, win_tun_result_t *out);
int		win_tun_active(void);
void	win_tun_status(win_tun_result_t *out);
void	win_tun_reconcile(void);

#endif
