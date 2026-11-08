//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// fl_doc.h -- A WireGuard config as sections and fields. Parse, edit, and render text.
// Comma lists stay one field until a reader asks for the pieces.
//
//===============================================================

#ifndef FL_DOC_H
#define FL_DOC_H

typedef struct fl_entry_s {
	int is_field;
	char *raw;
	char *key;
	char *value;
} fl_entry_t;

typedef struct fl_section_s {
	char *name;
	fl_entry_t *entries;
	int count;
	int cap;
} fl_section_t;

typedef struct fl_doc_s {
	char **preamble;
	int preamble_count;
	int preamble_cap;
	fl_section_t *sections;
	int section_count;
	int section_cap;
} fl_doc_t;

void	fl_doc_init(fl_doc_t *doc);
void	fl_doc_free(fl_doc_t *doc);
void	fl_doc_parse(fl_doc_t *doc, const char *text);
/* Caller frees the result. Always ends with a newline. */
char	*fl_doc_render(const fl_doc_t *doc);

int		fl_doc_interface(const fl_doc_t *doc);
int		fl_doc_peers(const fl_doc_t *doc, int *out, int cap);
void	fl_doc_value(const fl_doc_t *doc, int section, const char *key, char *buf, int cap);
void	fl_doc_set(fl_doc_t *doc, int section, const char *key, const char *value);
int		fl_doc_ensure_interface(fl_doc_t *doc);
int		fl_doc_add_peer(fl_doc_t *doc);

void	fl_doc_addresses(const fl_doc_t *doc, char *buf, int cap);
void	fl_doc_dns(const fl_doc_t *doc, char *buf, int cap);
void	fl_doc_peer_allowed(const fl_doc_t *doc, int section, char *buf, int cap);
void	fl_doc_blank(fl_doc_t *doc, const char *private_key);

/* Split a comma list into trimmed tokens. Returns count. */
int		fl_split_commas(const char *text, char out[][256], int cap);

#endif
