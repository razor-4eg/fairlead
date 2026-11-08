//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// fl_bypass.c -- Turn the route list into the CIDRs that go through the tunnel and the ones that stay direct.
//
//===============================================================

#include "fl_bypass.h"
#include "fl_ip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
===============
warn
Keep a plan warning when there is room.
===============
*/
static void warn(fl_bypass_t *out, const char *text) {
	if (out->warning_count >= 16) {
		return;
	}

	snprintf(out->warnings[out->warning_count], 256, "%s", text);
	out->warning_count++;
}

/*
===============
push_cidr
Append one network.
===============
*/
static int push_cidr(fl_cidr_t *list, int *count, int cap, const fl_cidr_t *cidr) {
	if (*count >= cap) {
		return 0;
	}

	list[(*count)++] = *cidr;
	return 1;
}

/*
===============
cmp_str
qsort helper for the formatted CIDR list.
===============
*/
static int cmp_str(const void *a, const void *b) {
	return strcmp((const char *)a, (const char *)b);
}

/*
===============
unique_sort
Format, drop duplicates, and sort.
===============
*/
static int unique_sort(const fl_cidr_t *list, int count, char out[][64], int cap) {
	int		i;
	int		n;
	int		k;
	int		seen;

	n = 0;

	for (i = 0; i < count && n < cap; i++) {
		char text[64];
		seen = 0;

		fl_cidr_format(&list[i], text, (int)sizeof(text));

		for (k = 0; k < n; k++) {
			if (strcmp(out[k], text) == 0) {
				seen = 1;
			}
		}

		if (!seen) {
			snprintf(out[n], 64, "%s", text);
			n++;
		}
	}

	if (n > 1) {
		qsort(out, (size_t)n, 64, cmp_str);
	}

	return n;
}

/*
===============
take
Accept one rule, or record why it was skipped.
===============
*/
static void take(fl_bypass_t *out, int selected, const char *raw, const char *origin,
	const fl_ip_t *endpoints, int endpoint_count, fl_cidr_t *list, int *count) {
	fl_cidr_t cidr;
	char msg[256];
	int i;

	if (!fl_cidr_parse(raw, &cidr)) {
		snprintf(msg, sizeof(msg), "Skipped invalid address %s.", raw);
		warn(out, msg);
		return;
	}

	if (cidr.prefix == 0) {
		if (selected) {
			snprintf(msg, sizeof(msg), "Address %s covers the whole internet. Use the other filter for that.", raw);
		} else {
			snprintf(msg, sizeof(msg), "Address %s is too broad and would disable the whole tunnel.", raw);
		}

		warn(out, msg);
		return;
	}

	for (i = 0; i < endpoint_count; i++) {
		if (fl_cidr_contains(&cidr, &endpoints[i])) {
			if (selected) {
				snprintf(msg, sizeof(msg), "%s overlaps the WireGuard server and stays on the regular network.", origin);
			} else {
				snprintf(msg, sizeof(msg), "%s overlaps the WireGuard server and stays in the tunnel.", origin);
			}

			warn(out, msg);
			return;
		}
	}

	push_cidr(list, count, 128, &cidr);
}

/*
===============
fl_bypass_plan
Build the tunnel routes and the direct routes for one policy.
===============
*/
void fl_bypass_plan(
	int policy_selected,
	const char **rules, int rule_count,
	const char **learned, int learned_count,
	const char **lan, int lan_count,
	int exclude_lan,
	const char **endpoints, int endpoint_count,
	fl_bypass_t *out)
{
	fl_cidr_t cidrs[128];
	fl_cidr_t direct[128];
	int cidr_count = 0;
	int direct_count = 0;
	fl_ip_t eps[16];
	int ep_count = 0;
	int i;

	memset(out, 0, sizeof(*out));

	for (i = 0; i < endpoint_count && ep_count < 16; i++) {
		fl_ip_t ip;

		if (endpoints[i] && fl_ip_parse(endpoints[i], &ip)) {
			eps[ep_count++] = ip;
		}
	}

	for (i = 0; i < rule_count; i++) {
		take(out, policy_selected, rules[i], rules[i], eps, ep_count, cidrs, &cidr_count);
	}

	for (i = 0; i < learned_count; i++) {
		take(out, policy_selected, learned[i], learned[i], eps, ep_count, cidrs, &cidr_count);
	}

	if (exclude_lan) {
		for (i = 0; i < lan_count; i++) {
			char origin[128];

			snprintf(origin, sizeof(origin), "Local network %s", lan[i]);

			if (policy_selected) {
				take(out, policy_selected, lan[i], origin, eps, ep_count, direct, &direct_count);
			} else {
				take(out, policy_selected, lan[i], origin, eps, ep_count, cidrs, &cidr_count);
			}
		}
	}

	out->route_count = unique_sort(cidrs, cidr_count, out->routes, FL_BYPASS_MAX);
	out->direct_count = unique_sort(direct, direct_count, out->direct, FL_BYPASS_MAX);

	if (policy_selected) {
		int w = 0;

		for (i = 0; i < out->route_count; i++) {
			int k;
			int drop = 0;

			for (k = 0; k < out->direct_count; k++) {
				if (strcmp(out->routes[i], out->direct[k]) == 0) {
					drop = 1;
				}
			}

			if (!drop) {
				if (w != i) {
					snprintf(out->routes[w], 64, "%s", out->routes[i]);
				}

				w++;
			}
		}

		out->route_count = w;
	}
}
