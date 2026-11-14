//================================================================
//
// copyright (c) 2026 razor-4eg.
//
// base.h -- Arena, small string helpers, and arena-backed JSON.
//
//================================================================

#ifndef BASE_H
#define BASE_H

#include <stddef.h>
#include <stdint.h>
typedef struct arena_s {
	char	*base;
	int		used;
	int		cap;
} arena_t;

#ifdef __cplusplus
extern "C" {
#endif

void	arena_init(arena_t *a, int cap);
void	arena_free(arena_t *a);
void	*arena_alloc(arena_t *a, int size);
char	*arena_sdup(arena_t *a, const char *s);

char	*str_dup(const char *s);
int		str_icmp(const char *a, const char *b);
void	str_trim(char *s);
void	str_lower(char *s);

#ifdef __cplusplus
}
#endif

enum {
	JSON_NULL = 0,
	JSON_BOOL,
	JSON_NUM,
	JSON_STR,
	JSON_ARR,
	JSON_OBJ
};

typedef struct json_s {
	int type;
	int boolean;
	char *text;
	char *key;
	struct json_s *child;
	struct json_s *next;
} json_t;

#ifdef __cplusplus
extern "C" {
#endif

json_t	*json_parse(arena_t *a, const char *text);
json_t	*json_get(const json_t *obj, const char *key);
/* String, number, or bool as text. NULL if missing or null. */
const char	*json_string(const json_t *v);
int			json_strings(const json_t *v, const char **out, int cap);

#ifdef __cplusplus
}
#endif

#endif
