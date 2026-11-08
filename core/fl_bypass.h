//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// fl_bypass.h -- The route plan for direct mode and through-the-tunnel mode.
//
//===============================================================

#ifndef FL_BYPASS_H
#define FL_BYPASS_H

#define FL_BYPASS_MAX 64

typedef struct fl_bypass_s {
	char routes[FL_BYPASS_MAX][64];
	int route_count;
	char direct[FL_BYPASS_MAX][64];
	int direct_count;
	char warnings[16][256];
	int warning_count;
} fl_bypass_t;

/* policy_selected: 0 exceptions (list stays off the tunnel), 1 selected (only the list uses it). */
void fl_bypass_plan(
	int policy_selected,
	const char **rules, int rule_count,
	const char **learned, int learned_count,
	const char **lan, int lan_count,
	int exclude_lan,
	const char **endpoints, int endpoint_count,
	fl_bypass_t *out);

#endif
