#ifndef KSYMS_H
#define KSYMS_H

#include "keymap.h"

typedef struct {
	const unsigned short uni;
	const char *name;
} sym;

typedef struct {
	const char * const *table;
	const int size;
} syms_entry;

extern syms_entry const syms[];

struct syn {
	const char *synonym;
	const char *official_name;
};
extern struct syn const synonyms[];

extern const unsigned int syms_size;
extern const unsigned int syn_size;

/* Returned by ksymtocode to report an unknown symbol */
#define CODE_FOR_UNKNOWN_KSYM (-1)

/* Directions for converting keysyms */
#define TO_AUTO (-1)		/* use LK_FLAG_PREFER_UNICODE */
#define TO_8BIT 0
#define TO_UNICODE 1

const char *codetoksym(struct lk_ctx *ctx, int code);
int ksymtocode(struct lk_ctx *ctx, const char *s, int direction);
int convert_code(struct lk_ctx *ctx, int code, int direction);
int add_capslock(struct lk_ctx *ctx, int code);

#endif
