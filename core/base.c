//================================================================
//
// copyright (c) 2026 razor-4eg.
//
// base.c -- Arena, strings, JSON.
//
//================================================================

#include "base.h"
#include <stdlib.h>
#include <string.h>
/*
===============
arena_init
Give the arena a heap block. cap is raised to at least 4096 bytes.
===============
*/
void arena_init(arena_t *a, int cap) {
	if (cap < 4096) {
		cap = 4096;
	}

	a->base = (char *)malloc((size_t)cap);
	a->used = 0;
	a->cap = a->base ? cap : 0;
}

/*
===============
arena_free
===============
*/
void arena_free(arena_t *a) {
	free(a->base);
	a->base = NULL;
	a->used = 0;
	a->cap = 0;
}

/*
===============
arena_alloc
Bump-allocate size bytes, 8-byte aligned. Grows the block when it must.
===============
*/
void *arena_alloc(arena_t *a, int size) {
	int		align;
	int		next;
	char	*grown;
	int		cap;

	if (size < 0) {
		return NULL;
	}

	align = (a->used + 7) & ~7;
	next = align + size;

	if (next > a->cap) {
		cap = a->cap < 4096 ? 4096 : a->cap;

		while (cap < next) {
			cap *= 2;
		}

		grown = (char *)realloc(a->base, (size_t)cap);

		if (!grown) {
			return NULL;
		}

		a->base = grown;
		a->cap = cap;
	}

	a->used = next;
	return a->base + align;
}

/*
===============
arena_sdup
Copy a C string into the arena.
===============
*/
char *arena_sdup(arena_t *a, const char *s) {
	int		n;
	char	*out;

	if (!s) {
		s = "";
	}

	n = (int)strlen(s);
	out = (char *)arena_alloc(a, n + 1);

	if (!out) {
		return NULL;
	}

	memcpy(out, s, (size_t)n + 1);
	return out;
}

/*
===============
str_dup
Malloc a copy. The caller frees it.
===============
*/
char *str_dup(const char *s) {
	size_t	n;
	char	*out;

	if (!s) {
		s = "";
	}

	n = strlen(s);
	out = (char *)malloc(n + 1);

	if (!out) {
		return NULL;
	}

	memcpy(out, s, n + 1);
	return out;
}

/*
===============
str_icmp
Case-insensitive compare. Missing strings compare as empty.
===============
*/
int str_icmp(const char *a, const char *b) {
	unsigned char	ca;
	unsigned char	cb;

	if (!a) {
		a = "";
	}

	if (!b) {
		b = "";
	}

	for (;;) {
		ca = (unsigned char)*a++;
		cb = (unsigned char)*b++;

		if (ca >= 'A' && ca <= 'Z') {
			ca = (unsigned char)(ca - 'A' + 'a');
		}

		if (cb >= 'A' && cb <= 'Z') {
			cb = (unsigned char)(cb - 'A' + 'a');
		}

		if (ca != cb || ca == 0) {
			return (int)ca - (int)cb;
		}
	}
}

/*
===============
str_trim
Strip leading and trailing ASCII space from s, in place.
===============
*/
void str_trim(char *s) {
	char	*start;
	char	*end;
	size_t	n;

	if (!s) {
		return;
	}

	start = s;

	while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
		start++;
	}

	n = strlen(start);
	end = start + n;

	while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
		end--;
	}

	*end = '\0';

	if (start != s) {
		memmove(s, start, (size_t)(end - start) + 1);
	}
}

/*
===============
str_lower
ASCII lowercase, in place.
===============
*/
void str_lower(char *s) {
	unsigned char	*p;

	if (!s) {
		return;
	}

	for (p = (unsigned char *)s; *p; p++) {
		if (*p >= 'A' && *p <= 'Z') {
			*p = (unsigned char)(*p - 'A' + 'a');
		}
	}
}



/*
===============
skip
Past ASCII space.
===============
*/
static const char *skip(const char *p) {
	while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
		p++;
	}

	return p;
}

static json_t *parse_value(arena_t *a, const char **pp);

/*
===============
hex4
Four hex digits, or 0.
===============
*/
static int hex4(const char *p, unsigned int *v) {
	int		i;
	char	c;

	*v = 0;

	for (i = 0; i < 4; i++) {
		c = p[i];

		*v <<= 4;

		if (c >= '0' && c <= '9') {
			*v |= (unsigned)(c - '0');
		} else if (c >= 'a' && c <= 'f') {
			*v |= (unsigned)(c - 'a' + 10);
		} else if (c >= 'A' && c <= 'F') {
			*v |= (unsigned)(c - 'A' + 10);
		} else {
			return 0;
		}
	}

	return 1;
}

/*
===============
utf8_put
Encode one code point into out.
===============
*/
static void utf8_put(char *out, int cap, int *n, unsigned int cp) {
	if (cp < 0x80) {
		if (*n + 1 < cap) {
			out[(*n)++] = (char)cp;
		}
	} else if (cp < 0x800) {
		if (*n + 2 < cap) {
			out[(*n)++] = (char)(0xc0 | (cp >> 6));
			out[(*n)++] = (char)(0x80 | (cp & 0x3f));
		}
	} else if (cp < 0x10000) {
		if (*n + 3 < cap) {
			out[(*n)++] = (char)(0xe0 | (cp >> 12));
			out[(*n)++] = (char)(0x80 | ((cp >> 6) & 0x3f));
			out[(*n)++] = (char)(0x80 | (cp & 0x3f));
		}
	} else {
		if (*n + 4 < cap) {
			out[(*n)++] = (char)(0xf0 | (cp >> 18));
			out[(*n)++] = (char)(0x80 | ((cp >> 12) & 0x3f));
			out[(*n)++] = (char)(0x80 | ((cp >> 6) & 0x3f));
			out[(*n)++] = (char)(0x80 | (cp & 0x3f));
		}
	}
}

/*
===============
parse_string
A JSON string, unescaped into the arena.
===============
*/
static int grow_tmp(char **tmp, int *cap, int n, int extra) {
	int		want;
	int		next;
	char	*grown;

	want = n + extra + 1;

	if (*cap >= want) {
		return 1;
	}

	next = *cap > 0 ? *cap * 2 : 256;

	while (next < want) {
		next *= 2;
	}

	grown = (char *)realloc(*tmp, (size_t)next);

	if (!grown) {
		return 0;
	}

	*tmp = grown;
	*cap = next;
	return 1;
}

/*
===============
parse_string
A JSON string, unescaped into the arena. The buffer grows, so a config
stored in tunnels.json is not cut off at 8 KB.
===============
*/
static char *parse_string(arena_t *a, const char **pp) {
	const char		*p;
	char			*tmp;
	int				cap;
	int				n;
	unsigned int	cp;
	char			*out;

	p = *pp;
	tmp = NULL;
	cap = 0;
	n = 0;

	if (*p != '"') {
		return NULL;
	}

	p++;

	while (*p && *p != '"') {
		if (*p == '\\') {
			p++;

			if (*p == '"' || *p == '\\' || *p == '/') {
				if (!grow_tmp(&tmp, &cap, n, 1)) {
					free(tmp);
					return NULL;
				}

				tmp[n++] = *p;
			} else if (*p == 'b' || *p == 'f' || *p == 'n' || *p == 'r' || *p == 't') {
				if (!grow_tmp(&tmp, &cap, n, 1)) {
					free(tmp);
					return NULL;
				}

				if (*p == 'b') {
					tmp[n++] = '\b';
				} else if (*p == 'f') {
					tmp[n++] = '\f';
				} else if (*p == 'n') {
					tmp[n++] = '\n';
				} else if (*p == 'r') {
					tmp[n++] = '\r';
				} else {
					tmp[n++] = '\t';
				}
			} else if (*p == 'u') {
				if (!hex4(p + 1, &cp) || !grow_tmp(&tmp, &cap, n, 4)) {
					free(tmp);
					return NULL;
				}

				utf8_put(tmp, cap, &n, cp);
				p += 4;
			} else {
				free(tmp);
				return NULL;
			}

			p++;
		} else {
			if (!grow_tmp(&tmp, &cap, n, 1)) {
				free(tmp);
				return NULL;
			}

			tmp[n++] = *p;
			p++;
		}
	}

	if (*p != '"') {
		free(tmp);
		return NULL;
	}

	if (!grow_tmp(&tmp, &cap, n, 0)) {
		free(tmp);
		return NULL;
	}

	tmp[n] = '\0';
	*pp = p + 1;
	out = arena_sdup(a, tmp);
	free(tmp);
	return out;
}

/*
===============
make
A zeroed node of the given type.
===============
*/
static json_t *make(arena_t *a, int type) {
	json_t	*v;

	v = (json_t *)arena_alloc(a, (int)sizeof(json_t));

	if (!v) {
		return NULL;
	}

	memset(v, 0, sizeof(*v));
	v->type = type;
	return v;
}

/*
===============
parse_value
One JSON value. Advances the cursor.
===============
*/
static json_t *parse_value(arena_t *a, const char **pp) {
	const char	*p;
	json_t	*v;
	char		*s;
	json_t	*tail;
	json_t	*child;
	char		*key;
	const char	*start;
	char		num[64];
	int			n;

	p = skip(*pp);

	if (*p == '"') {
		s = parse_string(a, &p);

		if (!s) {
			return NULL;
		}

		v = make(a, JSON_STR);

		if (!v) {
			return NULL;
		}

		v->text = s;
		*pp = p;
		return v;
	}

	if (*p == '{') {
		tail = NULL;

		v = make(a, JSON_OBJ);

		if (!v) {
			return NULL;
		}

		p++;
		p = skip(p);

		if (*p == '}') {
			*pp = p + 1;
			return v;
		}

		for (;;) {
			p = skip(p);
			key = parse_string(a, &p);

			if (!key) {
				return NULL;
			}

			p = skip(p);

			if (*p != ':') {
				return NULL;
			}

			p++;
			child = parse_value(a, &p);

			if (!child) {
				return NULL;
			}

			child->key = key;

			if (!v->child) {
				v->child = child;
			} else {
				tail->next = child;
			}

			tail = child;
			p = skip(p);

			if (*p == ',') {
				p++;
				continue;
			}

			if (*p == '}') {
				*pp = p + 1;
				return v;
			}

			return NULL;
		}
	}

	if (*p == '[') {
		tail = NULL;

		v = make(a, JSON_ARR);

		if (!v) {
			return NULL;
		}

		p++;
		p = skip(p);

		if (*p == ']') {
			*pp = p + 1;
			return v;
		}

		for (;;) {
			child = parse_value(a, &p);

			if (!child) {
				return NULL;
			}

			if (!v->child) {
				v->child = child;
			} else {
				tail->next = child;
			}

			tail = child;
			p = skip(p);

			if (*p == ',') {
				p++;
				continue;
			}

			if (*p == ']') {
				*pp = p + 1;
				return v;
			}

			return NULL;
		}
	}

	if (strncmp(p, "true", 4) == 0) {
		v = make(a, JSON_BOOL);

		if (!v) {
			return NULL;
		}

		v->boolean = 1;
		v->text = arena_sdup(a, "true");
		*pp = p + 4;
		return v;
	}

	if (strncmp(p, "false", 5) == 0) {
		v = make(a, JSON_BOOL);

		if (!v) {
			return NULL;
		}

		v->text = arena_sdup(a, "false");
		*pp = p + 5;
		return v;
	}

	if (strncmp(p, "null", 4) == 0) {
		v = make(a, JSON_NULL);
		*pp = p + 4;
		return v;
	}

	if (*p == '-' || (*p >= '0' && *p <= '9')) {
		start = p;
		n = 0;

		if (*p == '-') {
			p++;
		}

		while (*p >= '0' && *p <= '9') {
			p++;
		}

		if (*p == '.') {
			p++;

			while (*p >= '0' && *p <= '9') {
				p++;
			}
		}

		n = (int)(p - start);

		if (n <= 0 || n >= (int)sizeof(num)) {
			return NULL;
		}

		memcpy(num, start, (size_t)n);
		num[n] = '\0';
		v = make(a, JSON_NUM);

		if (!v) {
			return NULL;
		}

		v->text = arena_sdup(a, num);
		*pp = p;
		return v;
	}

	return NULL;
}

/*
===============
json_parse
Parse one value and require nothing after it.
===============
*/
json_t *json_parse(arena_t *a, const char *text) {
	const char	*p;
	json_t	*v;

	p = text ? text : "";

	v = parse_value(a, &p);

	if (!v) {
		return NULL;
	}

	p = skip(p);

	if (*p != '\0') {
		return NULL;
	}

	return v;
}

/*
===============
json_get
Child with this key, or NULL.
===============
*/
json_t *json_get(const json_t *obj, const char *key) {
	const json_t	*c;

	if (!obj || obj->type != JSON_OBJ) {
		return NULL;
	}

	for (c = obj->child; c; c = c->next) {
		if (c->key && strcmp(c->key, key) == 0) {
			return (json_t *)c;
		}
	}

	return NULL;
}

/*
===============
json_string
Text of a string, number, or bool. NULL for null, array, object, or empty.
===============
*/
const char *json_string(const json_t *v) {
	if (!v || v->type == JSON_NULL || v->type == JSON_ARR || v->type == JSON_OBJ) {
		return NULL;
	}

	if (!v->text || v->text[0] == '\0') {
		return NULL;
	}

	return v->text;
}

/*
===============
json_strings
Strings from an array, or the one string this value already is.
===============
*/
int json_strings(const json_t *v, const char **out, int cap) {
	int				n;
	const json_t	*c;
	const char		*s;

	n = 0;

	if (!v) {
		return 0;
	}

	if (v->type == JSON_ARR) {
		for (c = v->child; c && n < cap; c = c->next) {
			s = json_string(c);

			if (s) {
				out[n++] = s;
			}
		}

		return n;
	}

	if (json_string(v)) {
		/* caller splits commas; we return the whole string as one */

		if (cap > 0) {
			out[n++] = json_string(v);
		}
	}

	return n;
}

