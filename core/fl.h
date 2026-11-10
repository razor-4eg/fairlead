//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// fl.h -- Bump arena and the small string helpers the rest of the kernel calls.
// Windows and the Mac app both link this directory. fl_abi.c is Windows only.
//
//===============================================================

#ifndef FL_H
#define FL_H

#include <stddef.h>
#include <stdint.h>

typedef struct fl_arena_s {
	char	*base;
	int		used;
	int		cap;
} fl_arena_t;

void	fl_arena_init(fl_arena_t *a, int cap);
void	fl_arena_free(fl_arena_t *a);
void	*fl_alloc(fl_arena_t *a, int size);
char	*fl_sdup(fl_arena_t *a, const char *s);

char	*fl_dup(const char *s);
int		fl_icmp(const char *a, const char *b);
void	fl_trim(char *s);
void	fl_lower(char *s);

#endif
