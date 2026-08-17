/* hush_proto.c: owns minimal parser for Nostr-like array messages over lines. */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hush_proto.h"

enum {
    HUSH_PROTO_MAX_SUB_ID = 256
};

/* Very small tokenizer for ["TYPE", ...] lines. No full JSON. */
static hush_status_t hush_parse_event_object(const char *s, hush_event_t *out);
static hush_status_t hush_parse_filter_object(const char *s, hush_filter_t *out);
static void hush_parse_kinds(const char *p, hush_filter_t *f);
static void hush_parse_authors(const char *p, hush_filter_t *f);
static void hush_parse_h_tag(const char *p, hush_filter_t *f);

/* Rejects NULL line or out_msg. Parses only the supported minimal Nostr array shapes. */
hush_status_t hush_proto_parse_line(const char *line, hush_client_msg_t *out_msg)
{
    if (line == NULL || out_msg == NULL)
        return HUSH_ERR_ARG;

    memset(out_msg, 0, sizeof(*out_msg));

    const char *p = strchr(line, '"');
    if (p == NULL)
        return HUSH_ERR_PARSE;

    p++;
    char typ[16] = {0};
    size_t ti = 0;
    while (*p && *p != '"' && ti < sizeof(typ) - 1) {
        typ[ti++] = *p++;
    }

    if (strcmp(typ, "EVENT") == 0) {
        out_msg->type = HUSH_MSG_EVENT;
        const char *obj = strchr(p, '{');
        if (obj != NULL)
            return hush_parse_event_object(obj, &out_msg->event);
        return HUSH_ERR_PARSE;
    }
    if (strcmp(typ, "REQ") == 0) {
        out_msg->type = HUSH_MSG_REQ;
        const char *q = strchr(p, '"');
        if (q != NULL) {
            q++;
            size_t si = 0;
            while (*q && *q != '"' && si < sizeof(out_msg->sub_id) - 1)
                out_msg->sub_id[si++] = *q++;
        }
        const char *fobj = strchr(p, '{');
        if (fobj != NULL) {
            if (hush_parse_filter_object(fobj, &out_msg->filters[0]) == HUSH_OK)
                out_msg->nfilters = 1;
        }
        return HUSH_OK;
    }
    if (strcmp(typ, "CLOSE") == 0) {
        out_msg->type = HUSH_MSG_CLOSE;
        return HUSH_OK;
    }

    out_msg->type = HUSH_MSG_UNKNOWN;
    return HUSH_OK;
}

/* Rejects NULL pointers. Writes a compact EVENT frame. */
hush_status_t hush_proto_format_event(const char *sub_id,
                                      const hush_event_t *ev,
                                      char *out_buf,
                                      size_t bufsz,
                                      size_t *out_written)
{
    if (sub_id == NULL || ev == NULL || out_buf == NULL)
        return HUSH_ERR_ARG;

    int n = snprintf(out_buf, bufsz,
                     "[\"EVENT\",\"%s\",{\"id\":\"%s\",\"pubkey\":\"%s\",\"kind\":%u,\"content\":\"%s\"}]\n",
                     sub_id, ev->id, ev->pubkey, ev->kind, ev->content);
    if (n < 0 || (size_t)n >= bufsz)
        return HUSH_ERR_FULL;

    if (out_written != NULL)
        *out_written = (size_t)n;
    return HUSH_OK;
}

static hush_status_t hush_parse_event_object(const char *s, hush_event_t *out)
{
    if (s == NULL || out == NULL)
        return HUSH_ERR_ARG;

    memset(out, 0, sizeof(*out));

    const char *idp = strstr(s, "\"id\":\"");
    if (idp != NULL)
        sscanf(idp + 6, "%64[^\"]", out->id);

    const char *pp = strstr(s, "\"pubkey\":\"");
    if (pp != NULL)
        sscanf(pp + 10, "%64[^\"]", out->pubkey);

    const char *kp = strstr(s, "\"kind\":");
    if (kp != NULL)
        out->kind = (uint32_t)atoi(kp + 7);

    const char *cp = strstr(s, "\"content\":\"");
    if (cp != NULL)
        sscanf(cp + 11, "%4096[^\"]", out->content);

    out->created_at = 1720000000;
    return HUSH_OK;
}

static hush_status_t hush_parse_filter_object(const char *s, hush_filter_t *out)
{
    if (s == NULL || out == NULL)
        return HUSH_ERR_ARG;

    memset(out, 0, sizeof(*out));
    hush_parse_kinds(s, out);
    hush_parse_authors(s, out);
    hush_parse_h_tag(s, out);
    return HUSH_OK;
}

static void hush_parse_kinds(const char *p, hush_filter_t *f)
{
    const char *kinds = strstr(p, "\"kinds\":[");
    if (kinds == NULL)
        return;
    kinds += 9;
    int k;
    while (sscanf(kinds, "%u", &k) == 1 && f->kinds_len < HUSH_FILTER_MAX_KINDS) {
        f->kinds[f->kinds_len++] = (uint32_t)k;
        while (*kinds && *kinds != ',' && *kinds != ']')
            ++kinds;
        if (*kinds == ',')
            ++kinds;
    }
}

static void hush_parse_authors(const char *p, hush_filter_t *f)
{
    const char *ap = strstr(p, "\"authors\":[");
    if (ap == NULL)
        return;
    ap += 11;
    char a[65];
    if (sscanf(ap, "\"%64[^\"]", a) == 1) {
        strcpy(f->authors[0], a);
        f->authors_len = 1;
    }
}

static void hush_parse_h_tag(const char *p, hush_filter_t *f)
{
    const char *hp = strstr(p, "\"#h\":[");
    if (hp == NULL)
        return;
    hp += 6;
    char v[256];
    if (sscanf(hp, "\"%255[^\"]", v) == 1) {
        strcpy(f->tag_keys[0], "h");
        strcpy(f->tag_vals[0][0], v);
        f->tag_vals_len[0] = 1;
        f->tag_count = 1;
    }
}
