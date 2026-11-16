//================================================================
//
// copyright (c) 2026 razor-4eg.
//
// mihomo.h -- Build a mihomo (Clash Meta) config from a WireGuard/AmneziaWG document and Fairlead rules.
//
//================================================================

#ifndef MIHOMO_H
#define MIHOMO_H

typedef struct mihomo_rules_s {
	const char	**cidrs;
	int			cidr_count;
	const char	**domains;
	int			domain_count;
	const char	**apps;
	int			app_count;
} mihomo_rules_t;

/* Malloc'd YAML. Caller frees. NULL on failure; err is always terminated when err_cap > 0.
 * block_quic: REJECT UDP/443 (HTTP/3) for tunnel traffic. Direct process hits stay before it. */
char	*mihomo_config(const char *wg_text, int policy_selected, int exclude_lan, int block_quic,
	const mihomo_rules_t *rules, const char *controller, const char *secret,
	char *err, int err_cap);

#endif
