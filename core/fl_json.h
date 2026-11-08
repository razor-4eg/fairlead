//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// fl_json.h -- JSON values kept in an arena. The tree dies with the arena.
//
//===============================================================

#ifndef FL_JSON_H
#define FL_JSON_H

#include "fl.h"

enum {
	FL_JSON_NULL = 0,
	FL_JSON_BOOL,
	FL_JSON_NUM,
	FL_JSON_STR,
	FL_JSON_ARR,
	FL_JSON_OBJ
};

typedef struct fl_json_s {
	int type;
	int boolean;
	char *text;
	char *key;
	struct fl_json_s *child;
	struct fl_json_s *next;
} fl_json_t;

fl_json_t	*fl_json_parse(fl_arena_t *a, const char *text);
fl_json_t	*fl_json_get(const fl_json_t *obj, const char *key);
/* String, number, or bool as text. NULL if missing or null. */
const char	*fl_json_string(const fl_json_t *v);
int			fl_json_strings(const fl_json_t *v, const char **out, int cap);

#endif
