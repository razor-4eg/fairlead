//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// fl_amnezia.h -- AmneziaWG checks and the import that turns a share into a WireGuard config.
//
//===============================================================

#ifndef FL_AMNEZIA_H
#define FL_AMNEZIA_H

#include "fl_doc.h"

enum {
	FL_IMPORT_WG = 0,
	FL_IMPORT_UNSUPPORTED = 1,
	FL_IMPORT_UNREADABLE = 2
};

int		fl_amnezia_is(const fl_doc_t *doc);
int		fl_amnezia_uint_range(const char *text, unsigned long long maxv);
/* 1 and writes lines (possibly empty). 0 if a present value cannot be encoded. */
int		fl_amnezia_uapi(const fl_doc_t *doc, char *out, int cap);
int		fl_amnezia_present(const fl_doc_t *doc, const char **out, int cap);
int		fl_amnezia_issues(const fl_doc_t *doc, char out[][192], int cap);
int		fl_config_issues(const fl_doc_t *doc, char out[][192], int cap);

/* config receives the WireGuard text (original text when it is already one).
   name may be empty. Both buffers are always terminated. */
int		fl_import_normalize(const char *text, char *config, int config_cap, char *name, int name_cap);

/* Internet halves used as tunnel routes for 0.0.0.0/0 and ::/0. */
int		fl_tunnel_destinations(const fl_doc_t *doc, char families[][8], char dests[][64], int cap);

#endif
