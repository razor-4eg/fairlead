//===============================================================
//
// copyright (c) 2026 razor-4eg.
//
// fl_dns.h -- A normalized host name, and the DNS packet built and parsed for it.
//
//===============================================================

#ifndef FL_DNS_H
#define FL_DNS_H

#include <stdint.h>

#define FL_DNS_A 1
#define FL_DNS_AAAA 28

typedef struct fl_dns_rec_s {
	char address[64];
	uint32_t ttl;
} fl_dns_rec_t;

typedef struct fl_dns_reply_s {
	int rcode;
	int truncated;
	int saw_cname;
	fl_dns_rec_t answers[32];
	int answer_count;
	fl_dns_rec_t additional[32];
	int additional_count;
} fl_dns_reply_t;

/* Lowercase hostname, or NULL-length 0 when rejected. Writes into buf. */
int		fl_domain_normalize(const char *raw, char *buf, int cap);

/* Returns byte count, or 0. */
int		fl_dns_query(uint16_t id, const char *name, uint16_t type, int edns, unsigned char *out, int cap);
int		fl_dns_parse(const unsigned char *data, int len, uint16_t id, fl_dns_reply_t *out);

#endif
