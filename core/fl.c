//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// fl.c -- Bump arena and the string helpers shared by the kernel.
//
//===============================================================

#include "fl.h"

#include <stdlib.h>
#include <string.h>

/*
===============
fl_arena_init
Give the arena a heap block. cap is raised to at least 4096 bytes.
===============
*/
void fl_arena_init(fl_arena_t *a, int cap) {
	if (cap < 4096) {
		cap = 4096;
	}

	a->base = (char *)malloc((size_t)cap);
	a->used = 0;
	a->cap = a->base ? cap : 0;
}

/*
===============
fl_arena_free
===============
*/
void fl_arena_free(fl_arena_t *a) {
	free(a->base);
	a->base = NULL;
	a->used = 0;
	a->cap = 0;
}

/*
===============
fl_alloc
Bump-allocate size bytes, 8-byte aligned. Grows the block when it must.
===============
*/
void *fl_alloc(fl_arena_t *a, int size) {
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
fl_sdup
Copy a C string into the arena.
===============
*/
char *fl_sdup(fl_arena_t *a, const char *s) {
	int		n;
	char	*out;

	if (!s) {
		s = "";
	}

	n = (int)strlen(s);
	out = (char *)fl_alloc(a, n + 1);

	if (!out) {
		return NULL;
	}

	memcpy(out, s, (size_t)n + 1);
	return out;
}

/*
===============
fl_dup
Malloc a copy. The caller frees it.
===============
*/
char *fl_dup(const char *s) {
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
fl_icmp
Case-insensitive compare. Missing strings compare as empty.
===============
*/
int fl_icmp(const char *a, const char *b) {
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
fl_trim
Strip leading and trailing ASCII space from s, in place.
===============
*/
void fl_trim(char *s) {
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
fl_lower
ASCII lowercase, in place.
===============
*/
void fl_lower(char *s) {
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
