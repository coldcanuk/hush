/* hush_json.h: RFC 8259 string escape for Hush JSON writers. */

#ifndef HUSH_JSON_H
#define HUSH_JSON_H

#include <stddef.h>

enum {
    HUSH_JSON_U_LEN = 6
};

/* Writes a JSON string body (no quotes) into out. NUL in, empty out. */
size_t hush_json_escape(const char *in, char *out, size_t outsz);

#endif /* HUSH_JSON_H */
