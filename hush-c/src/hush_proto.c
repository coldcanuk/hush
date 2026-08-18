/* hush_proto.c: owns minimal parser for Nostr-like array messages over lines. */

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hush_proto.h"
#include "hush_status.h"

static hush_status_t hush_parse_event_object(const char *s, hush_event_t *out);
static hush_status_t hush_parse_filter_object(const char *s, hush_filter_t *out);
static hush_status_t hush_extract_quoted(const char *p, char *out, size_t outsz);

hush_status_t hush_proto_parse_line(const char *line, hush_client_msg_t *out_msg)
{
    if (line == NULL || out_msg == NULL)
        return HUSH_ERR_ARG;
    memset(out_msg, 0, sizeof(*out_msg));

    const char *p = strchr(line, '"');
    if (!p)
        return HUSH_ERR_PARSE;
    p++;

    char typ[16] = {0};
    size_t ti = 0;
    while (*p && *p != '"' && ti < sizeof(typ)-1) {
        typ[ti++] = *p++;
    }

    if (strcmp(typ, "EVENT") == 0) {
        out_msg->type = HUSH_MSG_EVENT;
        const char *obj = strchr(p, '{');
        if (obj)
            return hush_parse_event_object(obj, &out_msg->event);
        return HUSH_ERR_PARSE;
    } else if (strcmp(typ, "REQ") == 0) {
        out_msg->type = HUSH_MSG_REQ;
        const char *q = strchr(p + 1, '"');
        if (q) {
            q++;
            (void)hush_extract_quoted(q, out_msg->sub_id, sizeof(out_msg->sub_id));
        }
        const char *fobj = strchr(p, '{');
        if (fobj) {
            hush_status_t st = hush_parse_filter_object(fobj, &out_msg->filters[0]);
            if (st == HUSH_OK)
                out_msg->nfilters = 1;
        }
        return HUSH_OK;
    } else if (strcmp(typ, "CLOSE") == 0) {
        out_msg->type = HUSH_MSG_CLOSE;
        return HUSH_OK;
    }
    out_msg->type = HUSH_MSG_UNKNOWN;
    return HUSH_OK;
}

hush_status_t hush_proto_format_event(const char *sub_id, const hush_event_t *ev,
                                      char *out_buf, size_t bufsz, size_t *out_written)
{
    if (sub_id == NULL || ev == NULL || out_buf == NULL)
        return HUSH_ERR_ARG;
    int n = snprintf(out_buf, bufsz,
                     "[\"EVENT\",\"%s\",{\"id\":\"%s\",\"pubkey\":\"%s\",\"kind\":%u,\"content\":\"%s\"}]\n",
                     sub_id, ev->id, ev->pubkey, ev->kind, ev->content);
    if (n < 0 || (size_t)n >= bufsz)
        return HUSH_ERR_FULL;
    if (out_written)
        *out_written = (size_t)n;
    return HUSH_OK;
}

hush_status_t hush_proto_format_ok(const char *ev_id, int ok, const char *msg,
                                   char *out_buf, size_t bufsz, size_t *out_written)
{
    int n;

    if (ev_id == NULL || out_buf == NULL)
        return HUSH_ERR_ARG;
    if (msg == NULL)
        msg = "";
    n = snprintf(out_buf, bufsz, "[\"OK\",\"%s\",%s,\"%s\"]\n",
                 ev_id, ok ? "true" : "false", msg);
    if (n < 0 || (size_t)n >= bufsz)
        return HUSH_ERR_FULL;
    if (out_written)
        *out_written = (size_t)n;
    return HUSH_OK;
}

hush_status_t hush_proto_format_eose(const char *sub_id, char *out_buf, size_t bufsz,
                                     size_t *out_written)
{
    int n;

    if (sub_id == NULL || out_buf == NULL)
        return HUSH_ERR_ARG;
    n = snprintf(out_buf, bufsz, "[\"EOSE\",\"%s\"]\n", sub_id);
    if (n < 0 || (size_t)n >= bufsz)
        return HUSH_ERR_FULL;
    if (out_written)
        *out_written = (size_t)n;
    return HUSH_OK;
}

static hush_status_t hush_extract_quoted(const char *p, char *out, size_t outsz)
{
    if (!p || !out || outsz == 0)
        return HUSH_ERR_ARG;
    size_t i = 0;
    while (*p && *p != '"' && i < outsz-1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return HUSH_OK;
}

static hush_status_t hush_parse_event_object(const char *s, hush_event_t *out)
{
    if (!s || !out)
        return HUSH_ERR_ARG;
    memset(out, 0, sizeof(*out));
    const char *idp = strstr(s, "\"id\":\"");
    if (idp) sscanf(idp + 6, "%64[^\"]", out->id);
    const char *pp = strstr(s, "\"pubkey\":\"");
    if (pp) sscanf(pp + 10, "%64[^\"]", out->pubkey);
    const char *kp = strstr(s, "\"kind\":");
    if (kp) out->kind = (uint32_t)atoi(kp + 7);
    const char *cp = strstr(s, "\"content\":\"");
    if (cp) sscanf(cp + 11, "%4096[^\"]", out->content);
    out->created_at = 1720000000;
    return HUSH_OK;
}

static hush_status_t hush_parse_filter_object(const char *s, hush_filter_t *out)
{
    if (!s || !out)
        return HUSH_ERR_ARG;
    memset(out, 0, sizeof(*out));
    const char *kinds = strstr(s, "\"kinds\":[");
    if (kinds) {
        kinds += 9;
        uint32_t k;
        while (sscanf(kinds, "%u", &k) == 1 && out->kinds_len < HUSH_FILTER_MAX_KINDS) {
            out->kinds[out->kinds_len++] = k;
            while (*kinds && *kinds != ',' && *kinds != ']') ++kinds;
            if (*kinds == ',') ++kinds;
        }
    }
    const char *ap = strstr(s, "\"authors\":[");
    if (ap) {
        ap += 11;
        char a[65];
        if (sscanf(ap, "\"%64[^\"]", a) == 1) {
            strcpy(out->authors[0], a);
            out->authors_len = 1;
        }
    }
    const char *hp = strstr(s, "\"#h\":[");
    if (hp) {
        hp += 6;
        char v[256];
        if (sscanf(hp, "\"%255[^\"]", v) == 1) {
            strcpy(out->tag_keys[0], "h");
            strcpy(out->tag_vals[0][0], v);
            out->tag_vals_len[0] = 1;
            out->tag_count = 1;
        }
    }
    return HUSH_OK;
}
