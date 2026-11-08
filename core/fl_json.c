//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// fl_json.c -- A small JSON reader. Imports and the helper protocol only need objects, arrays, and strings.
//
//===============================================================

#include "fl_json.h"

#include <stdlib.h>
#include <string.h>

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

static fl_json_t *parse_value(fl_arena_t *a, const char **pp);

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
static char *parse_string(fl_arena_t *a, const char **pp) {
	const char		*p;
	char			tmp[8192];
	int				n;
	unsigned int	cp;

	p = *pp;
	n = 0;

	if (*p != '"') {
		return NULL;
	}

	p++;

	while (*p && *p != '"') {
		if (*p == '\\') {
			p++;

			if (*p == '"' || *p == '\\' || *p == '/') {
				if (n < (int)sizeof(tmp) - 1) {
					tmp[n++] = *p;
				}
			} else if (*p == 'b') {
				if (n < (int)sizeof(tmp) - 1) {
					tmp[n++] = '\b';
				}
			} else if (*p == 'f') {
				if (n < (int)sizeof(tmp) - 1) {
					tmp[n++] = '\f';
				}
			} else if (*p == 'n') {
				if (n < (int)sizeof(tmp) - 1) {
					tmp[n++] = '\n';
				}
			} else if (*p == 'r') {
				if (n < (int)sizeof(tmp) - 1) {
					tmp[n++] = '\r';
				}
			} else if (*p == 't') {
				if (n < (int)sizeof(tmp) - 1) {
					tmp[n++] = '\t';
				}
			} else if (*p == 'u') {
				if (!hex4(p + 1, &cp)) {
					return NULL;
				}

				utf8_put(tmp, (int)sizeof(tmp), &n, cp);
				p += 4;
			} else {
				return NULL;
			}

			p++;
		} else {
			if (n < (int)sizeof(tmp) - 1) {
				tmp[n++] = *p;
			}

			p++;
		}
	}

	if (*p != '"') {
		return NULL;
	}

	tmp[n] = '\0';
	*pp = p + 1;
	return fl_sdup(a, tmp);
}

/*
===============
make
A zeroed node of the given type.
===============
*/
static fl_json_t *make(fl_arena_t *a, int type) {
	fl_json_t	*v;

	v = (fl_json_t *)fl_alloc(a, (int)sizeof(fl_json_t));

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
static fl_json_t *parse_value(fl_arena_t *a, const char **pp) {
	const char	*p;
	fl_json_t	*v;
	char		*s;
	fl_json_t	*tail;
	fl_json_t	*child;
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

		v = make(a, FL_JSON_STR);

		if (!v) {
			return NULL;
		}

		v->text = s;
		*pp = p;
		return v;
	}

	if (*p == '{') {
		tail = NULL;

		v = make(a, FL_JSON_OBJ);

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

		v = make(a, FL_JSON_ARR);

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
		v = make(a, FL_JSON_BOOL);

		if (!v) {
			return NULL;
		}

		v->boolean = 1;
		v->text = fl_sdup(a, "true");
		*pp = p + 4;
		return v;
	}

	if (strncmp(p, "false", 5) == 0) {
		v = make(a, FL_JSON_BOOL);

		if (!v) {
			return NULL;
		}

		v->text = fl_sdup(a, "false");
		*pp = p + 5;
		return v;
	}

	if (strncmp(p, "null", 4) == 0) {
		v = make(a, FL_JSON_NULL);
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
		v = make(a, FL_JSON_NUM);

		if (!v) {
			return NULL;
		}

		v->text = fl_sdup(a, num);
		*pp = p;
		return v;
	}

	return NULL;
}

/*
===============
fl_json_parse
Parse one value and require nothing after it.
===============
*/
fl_json_t *fl_json_parse(fl_arena_t *a, const char *text) {
	const char	*p;
	fl_json_t	*v;

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
fl_json_get
Child with this key, or NULL.
===============
*/
fl_json_t *fl_json_get(const fl_json_t *obj, const char *key) {
	const fl_json_t	*c;

	if (!obj || obj->type != FL_JSON_OBJ) {
		return NULL;
	}

	for (c = obj->child; c; c = c->next) {
		if (c->key && strcmp(c->key, key) == 0) {
			return (fl_json_t *)c;
		}
	}

	return NULL;
}

/*
===============
fl_json_string
Text of a string, number, or bool. NULL for null, array, object, or empty.
===============
*/
const char *fl_json_string(const fl_json_t *v) {
	if (!v || v->type == FL_JSON_NULL || v->type == FL_JSON_ARR || v->type == FL_JSON_OBJ) {
		return NULL;
	}

	if (!v->text || v->text[0] == '\0') {
		return NULL;
	}

	return v->text;
}

/*
===============
fl_json_strings
Strings from an array, or the one string this value already is.
===============
*/
int fl_json_strings(const fl_json_t *v, const char **out, int cap) {
	int				n;
	const fl_json_t	*c;
	const char		*s;

	n = 0;

	if (!v) {
		return 0;
	}

	if (v->type == FL_JSON_ARR) {
		for (c = v->child; c && n < cap; c = c->next) {
			s = fl_json_string(c);

			if (s) {
				out[n++] = s;
			}
		}

		return n;
	}

	if (fl_json_string(v)) {
		/* caller splits commas; we return the whole string as one */

		if (cap > 0) {
			out[n++] = fl_json_string(v);
		}
	}

	return n;
}
