/* hush_proto.c: Nostr-shaped newline-JSON line parser and serializer. */

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hush_json.h"
#include "hush_proto.h"
#include "hush_status.h"

enum {
    HUSH_PROTO_FILTERS_MAX = 4,
    HUSH_PROTO_TYPE_MAX = 16,
    HUSH_PROTO_PATH_MAX = 64,
    HUSH_PROTO_NUMBER_MAX = 32,
    HUSH_PROTO_TAG_ESCAPE_MAX = HUSH_EVENT_MAX_TAG_LEN * HUSH_JSON_U_LEN + 1,
    HUSH_PROTO_CONTENT_ESCAPE_MAX =
        HUSH_EVENT_MAX_CONTENT * HUSH_JSON_U_LEN + 1
};

/* Bounded JSON output cursor plus one escaped-scalar scratch buffer. */
typedef struct {
    char *out;
    size_t capacity;
    size_t offset;
    char *scratch;
    size_t scratch_capacity;
} hush_proto_writer_t;

/* Bounded table of JSON strings addressed as stem/<index>. */
typedef struct {
    char *base;
    size_t stride;
    size_t elem_capacity;
    size_t max;
    size_t *out_count;
} hush_proto_list_t;

/* Filter tag keys the wire parser understands, without the leading '#'. */
static const char *const hush_proto_tag_keys[HUSH_FILTER_MAX_TAGS] = {
    "e", "p", "h", "d"
};

/* Copies the string at path into out, decoding JSON escapes. */
static hush_status_t hush_proto_take_string(char *out, size_t outsz,
                                            const char *json, const char *path);
/* Reads the unsigned integer at path. */
static hush_status_t hush_proto_take_u32(uint32_t *out, const char *json,
                                         const char *path);
/* Reads the signed integer at path. */
static hush_status_t hush_proto_take_i64(int64_t *out, const char *json,
                                         const char *path);
/* Reads stem/<index> as a string. */
static hush_status_t hush_proto_take_at(char *out, size_t outsz,
                                        const char *json, const char *stem,
                                        size_t index);
/* Reads stem/<index> as an unsigned integer. */
static hush_status_t hush_proto_take_u32_at(uint32_t *out, const char *json,
                                            const char *stem, size_t index);
/* Reads a stem array of strings into a bounded table. Missing array is empty. */
static hush_status_t hush_proto_take_list(const hush_proto_list_t *list,
                                          const char *json, const char *stem);
/* Parses the event object at root, including created_at and tags. */
static hush_status_t hush_proto_parse_event(hush_event_t *out,
                                            const char *json,
                                            const char *root);
/* Parses every tag element within the event caps. */
static hush_status_t hush_proto_parse_tags(hush_event_t *out,
                                           const char *json,
                                           const char *root);
/* Parses the filter object at /<slot + 2>. */
static hush_status_t hush_proto_parse_filter(hush_filter_t *out,
                                             const char *json, size_t slot);
/* Appends raw bounded text. FULL on overflow. */
static hush_status_t hush_proto_put_raw(hush_proto_writer_t *writer,
                                        const char *text);
/* Appends one escaped, quoted JSON string. FULL on overflow. */
static hush_status_t hush_proto_put_string(hush_proto_writer_t *writer,
                                           const char *text);
/* Appends an unsigned integer. FULL on overflow. */
static hush_status_t hush_proto_put_u32(hush_proto_writer_t *writer,
                                        uint32_t value);
/* Appends a signed integer. FULL on overflow. */
static hush_status_t hush_proto_put_i64(hush_proto_writer_t *writer,
                                        int64_t value);

hush_status_t hush_proto_parse_line(const char *line, hush_client_msg_t *out_msg)
{
    char type[HUSH_PROTO_TYPE_MAX];
    size_t slot;

    if (line == NULL || out_msg == NULL)
        return HUSH_ERR_ARG;
    memset(out_msg, 0, sizeof(*out_msg));
    if (hush_proto_take_string(type, sizeof(type), line, "/0") != HUSH_OK) {
        out_msg->type = HUSH_MSG_UNKNOWN;
        return HUSH_ERR_PARSE;
    }
    if (strcmp(type, "EVENT") == 0) {
        hush_json_value_t first = {0};
        const char *root = "/1";

        out_msg->type = HUSH_MSG_EVENT;
        /* ["EVENT", sub, {..}] keeps the event at /2; ["EVENT", {..}] at /1. */
        if (hush_json_lookup(&first, line, "/1") == HUSH_OK &&
            first.len > 0 && first.start[0] == '"')
            root = "/2";
        return hush_proto_parse_event(&out_msg->event, line, root);
    }
    if (strcmp(type, "REQ") == 0) {
        out_msg->type = HUSH_MSG_REQ;
        (void)hush_proto_take_string(out_msg->sub_id, sizeof(out_msg->sub_id),
                                     line, "/1");
        for (slot = 0; slot < (size_t)HUSH_PROTO_FILTERS_MAX; ++slot) {
            if (hush_proto_parse_filter(&out_msg->filters[slot], line, slot) !=
                HUSH_OK)
                break;
            out_msg->nfilters++;
        }
        return HUSH_OK;
    }
    if (strcmp(type, "CLOSE") == 0) {
        out_msg->type = HUSH_MSG_CLOSE;
        return HUSH_OK;
    }
    if (strcmp(type, "COUNT") == 0) {
        out_msg->type = HUSH_MSG_COUNT;
        return HUSH_OK;
    }
    out_msg->type = HUSH_MSG_UNKNOWN;
    return HUSH_OK;
}

hush_status_t hush_proto_format_event(const char *sub_id, const hush_event_t *ev,
                                      char *out_buf, size_t bufsz,
                                      size_t *out_written)
{
    char content[HUSH_PROTO_CONTENT_ESCAPE_MAX];
    char scratch[HUSH_PROTO_TAG_ESCAPE_MAX];
    hush_proto_writer_t writer;
    size_t tag;
    size_t elem;

    if (sub_id == NULL || ev == NULL || out_buf == NULL)
        return HUSH_ERR_ARG;
    if (hush_json_escape(ev->content, content, sizeof(content)) == 0 &&
        ev->content[0] != '\0')
        return HUSH_ERR_FULL;
    writer = (hush_proto_writer_t){.out = out_buf, .capacity = bufsz,
                                   .offset = 0, .scratch = scratch,
                                   .scratch_capacity = sizeof(scratch)};
    HUSH_TRY(hush_proto_put_raw(&writer, "[\"EVENT\","));
    HUSH_TRY(hush_proto_put_string(&writer, sub_id));
    HUSH_TRY(hush_proto_put_raw(&writer, ",{\"id\":"));
    HUSH_TRY(hush_proto_put_string(&writer, ev->id));
    HUSH_TRY(hush_proto_put_raw(&writer, ",\"pubkey\":"));
    HUSH_TRY(hush_proto_put_string(&writer, ev->pubkey));
    HUSH_TRY(hush_proto_put_raw(&writer, ",\"kind\":"));
    HUSH_TRY(hush_proto_put_u32(&writer, ev->kind));
    HUSH_TRY(hush_proto_put_raw(&writer, ",\"created_at\":"));
    HUSH_TRY(hush_proto_put_i64(&writer, ev->created_at));
    HUSH_TRY(hush_proto_put_raw(&writer, ",\"content\":\""));
    HUSH_TRY(hush_proto_put_raw(&writer, content));
    HUSH_TRY(hush_proto_put_raw(&writer, "\",\"tags\":["));
    for (tag = 0; tag < ev->tag_count && tag < (size_t)HUSH_EVENT_MAX_TAGS;
         ++tag) {
        if (tag > 0)
            HUSH_TRY(hush_proto_put_raw(&writer, ","));
        HUSH_TRY(hush_proto_put_raw(&writer, "["));
        for (elem = 0; elem < (size_t)HUSH_EVENT_MAX_TAG_ELEMS; ++elem) {
            if (ev->tags[tag][elem][0] == '\0')
                break;
            if (elem > 0)
                HUSH_TRY(hush_proto_put_raw(&writer, ","));
            HUSH_TRY(hush_proto_put_string(&writer, ev->tags[tag][elem]));
        }
        HUSH_TRY(hush_proto_put_raw(&writer, "]"));
    }
    HUSH_TRY(hush_proto_put_raw(&writer, "]}]\n"));
    if (out_written != NULL)
        *out_written = writer.offset;
    return HUSH_OK;
}

hush_status_t hush_proto_format_ok(const char *ev_id, int ok, const char *msg,
                                   char *out_buf, size_t bufsz,
                                   size_t *out_written)
{
    char scratch[HUSH_PROTO_TAG_ESCAPE_MAX];
    hush_proto_writer_t writer;

    if (ev_id == NULL || out_buf == NULL)
        return HUSH_ERR_ARG;
    if (msg == NULL)
        msg = "";
    writer = (hush_proto_writer_t){.out = out_buf, .capacity = bufsz,
                                   .offset = 0, .scratch = scratch,
                                   .scratch_capacity = sizeof(scratch)};
    HUSH_TRY(hush_proto_put_raw(&writer, "[\"OK\","));
    HUSH_TRY(hush_proto_put_string(&writer, ev_id));
    HUSH_TRY(hush_proto_put_raw(&writer, ok ? ",true," : ",false,"));
    HUSH_TRY(hush_proto_put_string(&writer, msg));
    HUSH_TRY(hush_proto_put_raw(&writer, "]\n"));
    if (out_written != NULL)
        *out_written = writer.offset;
    return HUSH_OK;
}

hush_status_t hush_proto_format_eose(const char *sub_id, char *out_buf,
                                     size_t bufsz, size_t *out_written)
{
    char scratch[HUSH_PROTO_TAG_ESCAPE_MAX];
    hush_proto_writer_t writer;

    if (sub_id == NULL || out_buf == NULL)
        return HUSH_ERR_ARG;
    writer = (hush_proto_writer_t){.out = out_buf, .capacity = bufsz,
                                   .offset = 0, .scratch = scratch,
                                   .scratch_capacity = sizeof(scratch)};
    HUSH_TRY(hush_proto_put_raw(&writer, "[\"EOSE\","));
    HUSH_TRY(hush_proto_put_string(&writer, sub_id));
    HUSH_TRY(hush_proto_put_raw(&writer, "]\n"));
    if (out_written != NULL)
        *out_written = writer.offset;
    return HUSH_OK;
}

static hush_status_t hush_proto_parse_event(hush_event_t *out,
                                            const char *json,
                                            const char *root)
{
    char path[HUSH_PROTO_PATH_MAX];
    uint32_t kind = 0;
    int64_t created = 0;
    int n;

    assert(out != NULL);
    assert(root != NULL);
    memset(out, 0, sizeof(*out));
    n = snprintf(path, sizeof(path), "%s/id", root);
    if (n <= 0 || (size_t)n >= sizeof(path))
        return HUSH_ERR_FULL;
    HUSH_TRY(hush_proto_take_string(out->id, sizeof(out->id), json, path));
    n = snprintf(path, sizeof(path), "%s/pubkey", root);
    if (n <= 0 || (size_t)n >= sizeof(path))
        return HUSH_ERR_FULL;
    HUSH_TRY(hush_proto_take_string(out->pubkey, sizeof(out->pubkey), json,
                                    path));
    n = snprintf(path, sizeof(path), "%s/kind", root);
    if (n <= 0 || (size_t)n >= sizeof(path))
        return HUSH_ERR_FULL;
    if (hush_proto_take_u32(&kind, json, path) == HUSH_OK)
        out->kind = kind;
    n = snprintf(path, sizeof(path), "%s/created_at", root);
    if (n <= 0 || (size_t)n >= sizeof(path))
        return HUSH_ERR_FULL;
    if (hush_proto_take_i64(&created, json, path) == HUSH_OK)
        out->created_at = created;
    n = snprintf(path, sizeof(path), "%s/content", root);
    if (n <= 0 || (size_t)n >= sizeof(path))
        return HUSH_ERR_FULL;
    (void)hush_proto_take_string(out->content, sizeof(out->content), json,
                                 path);
    return hush_proto_parse_tags(out, json, root);
}

static hush_status_t hush_proto_parse_tags(hush_event_t *out, const char *json,
                                           const char *root)
{
    size_t tag;

    assert(out != NULL);
    assert(root != NULL);
    for (tag = 0; tag < (size_t)HUSH_EVENT_MAX_TAGS; ++tag) {
        char stem[HUSH_PROTO_PATH_MAX];
        size_t elem;
        int any = 0;
        int n = snprintf(stem, sizeof(stem), "%s/tags/%zu", root, tag);

        if (n <= 0 || (size_t)n >= sizeof(stem))
            return HUSH_ERR_FULL;
        for (elem = 0; elem < (size_t)HUSH_EVENT_MAX_TAG_ELEMS; ++elem) {
            if (hush_proto_take_at(out->tags[tag][elem],
                                   sizeof(out->tags[tag][elem]), json, stem,
                                   elem) != HUSH_OK) {
                out->tags[tag][elem][0] = '\0';
                break;
            }
            any = 1;
        }
        if (!any)
            break;
        out->tag_count = tag + 1;
    }
    return HUSH_OK;
}

static hush_status_t hush_proto_parse_filter(hush_filter_t *out,
                                             const char *json, size_t slot)
{
    char stem[8];
    char section[HUSH_PROTO_PATH_MAX];
    hush_json_value_t probe = {0};
    hush_proto_list_t list;
    size_t index;
    size_t key;
    int n;

    assert(out != NULL);
    memset(out, 0, sizeof(*out));
    n = snprintf(stem, sizeof(stem), "/%zu", slot + 2);
    if (n <= 0 || (size_t)n >= sizeof(stem))
        return HUSH_ERR_FULL;
    if (hush_json_lookup(&probe, json, stem) != HUSH_OK)
        return HUSH_ERR_NOT_FOUND;
    if (snprintf(section, sizeof(section), "%s/kinds", stem) >=
        (int)sizeof(section))
        return HUSH_ERR_FULL;
    for (index = 0; index < (size_t)HUSH_FILTER_MAX_KINDS; ++index) {
        uint32_t kind = 0;

        if (hush_proto_take_u32_at(&kind, json, section, index) != HUSH_OK)
            break;
        out->kinds[out->kinds_len++] = kind;
    }
    list = (hush_proto_list_t){.base = &out->ids[0][0],
                               .stride = sizeof(out->ids[0]),
                               .elem_capacity = sizeof(out->ids[0]),
                               .max = (size_t)HUSH_FILTER_MAX_IDS,
                               .out_count = &out->ids_len};
    if (snprintf(section, sizeof(section), "%s/ids", stem) >=
        (int)sizeof(section))
        return HUSH_ERR_FULL;
    HUSH_TRY(hush_proto_take_list(&list, json, section));
    list = (hush_proto_list_t){.base = &out->authors[0][0],
                               .stride = sizeof(out->authors[0]),
                               .elem_capacity = sizeof(out->authors[0]),
                               .max = (size_t)HUSH_FILTER_MAX_AUTHORS,
                               .out_count = &out->authors_len};
    if (snprintf(section, sizeof(section), "%s/authors", stem) >=
        (int)sizeof(section))
        return HUSH_ERR_FULL;
    HUSH_TRY(hush_proto_take_list(&list, json, section));
    if (snprintf(section, sizeof(section), "%s/since", stem) >=
        (int)sizeof(section))
        return HUSH_ERR_FULL;
    (void)hush_proto_take_i64(&out->since, json, section);
    if (snprintf(section, sizeof(section), "%s/until", stem) >=
        (int)sizeof(section))
        return HUSH_ERR_FULL;
    (void)hush_proto_take_i64(&out->until, json, section);
    for (key = 0; key < (size_t)HUSH_FILTER_MAX_TAGS; ++key) {
        if (snprintf(section, sizeof(section), "%s/#%s", stem,
                     hush_proto_tag_keys[key]) >= (int)sizeof(section))
            return HUSH_ERR_FULL;
        if (hush_json_lookup(&probe, json, section) != HUSH_OK)
            continue;
        list = (hush_proto_list_t){
            .base = &out->tag_vals[out->tag_count][0][0],
            .stride = sizeof(out->tag_vals[0][0]),
            .elem_capacity = sizeof(out->tag_vals[0][0]),
            .max = (size_t)HUSH_FILTER_MAX_VALUES,
            .out_count = &out->tag_vals_len[out->tag_count]};
        HUSH_TRY(hush_proto_take_list(&list, json, section));
        memcpy(out->tag_keys[out->tag_count], hush_proto_tag_keys[key],
               strlen(hush_proto_tag_keys[key]) + 1);
        out->tag_count++;
    }
    return HUSH_OK;
}

static hush_status_t hush_proto_take_string(char *out, size_t outsz,
                                            const char *json, const char *path)
{
    hush_json_value_t value = {0};

    assert(out != NULL);
    assert(outsz > 0);
    HUSH_TRY(hush_json_lookup(&value, json, path));
    return hush_json_decode(out, outsz, &value);
}

static hush_status_t hush_proto_take_u32(uint32_t *out, const char *json,
                                         const char *path)
{
    hush_json_value_t value = {0};
    char text[HUSH_PROTO_NUMBER_MAX];
    char *end = NULL;
    unsigned long number;
    size_t len;

    assert(out != NULL);
    HUSH_TRY(hush_json_lookup(&value, json, path));
    len = value.len;
    if (len == 0 || len >= sizeof(text))
        return HUSH_ERR_PARSE;
    memcpy(text, value.start, len);
    text[len] = '\0';
    errno = 0;
    number = strtoul(text, &end, 10);
    if (end == NULL || *end != '\0' || errno == ERANGE ||
        number > 0xfffffffful)
        return HUSH_ERR_PARSE;
    *out = (uint32_t)number;
    return HUSH_OK;
}

static hush_status_t hush_proto_take_i64(int64_t *out, const char *json,
                                         const char *path)
{
    hush_json_value_t value = {0};
    char text[HUSH_PROTO_NUMBER_MAX];
    char *end = NULL;
    long long number;
    size_t len;

    assert(out != NULL);
    HUSH_TRY(hush_json_lookup(&value, json, path));
    len = value.len;
    if (len == 0 || len >= sizeof(text))
        return HUSH_ERR_PARSE;
    memcpy(text, value.start, len);
    text[len] = '\0';
    errno = 0;
    number = strtoll(text, &end, 10);
    if (end == NULL || *end != '\0' || errno == ERANGE)
        return HUSH_ERR_PARSE;
    *out = (int64_t)number;
    return HUSH_OK;
}

static hush_status_t hush_proto_take_at(char *out, size_t outsz,
                                        const char *json, const char *stem,
                                        size_t index)
{
    char path[HUSH_PROTO_PATH_MAX];
    int n = snprintf(path, sizeof(path), "%s/%zu", stem, index);

    if (n <= 0 || (size_t)n >= sizeof(path))
        return HUSH_ERR_FULL;
    return hush_proto_take_string(out, outsz, json, path);
}

static hush_status_t hush_proto_take_u32_at(uint32_t *out, const char *json,
                                            const char *stem, size_t index)
{
    char path[HUSH_PROTO_PATH_MAX];
    int n = snprintf(path, sizeof(path), "%s/%zu", stem, index);

    if (n <= 0 || (size_t)n >= sizeof(path))
        return HUSH_ERR_FULL;
    return hush_proto_take_u32(out, json, path);
}

static hush_status_t hush_proto_take_list(const hush_proto_list_t *list,
                                          const char *json, const char *stem)
{
    size_t i;

    assert(list != NULL);
    assert(list->base != NULL);
    assert(list->out_count != NULL);
    *list->out_count = 0;
    for (i = 0; i < list->max; ++i) {
        char *slot = list->base + i * list->stride;
        hush_status_t st = hush_proto_take_at(slot, list->elem_capacity, json,
                                              stem, i);

        if (st == HUSH_ERR_NOT_FOUND)
            break;
        if (st != HUSH_OK)
            return st;
        *list->out_count = i + 1;
    }
    return HUSH_OK;
}

static hush_status_t hush_proto_put_raw(hush_proto_writer_t *writer,
                                        const char *text)
{
    size_t len;
    int n;

    assert(writer != NULL);
    assert(text != NULL);
    len = strlen(text);
    if (writer->offset + len + 1 > writer->capacity)
        return HUSH_ERR_FULL;
    n = snprintf(writer->out + writer->offset,
                 writer->capacity - writer->offset, "%s", text);
    if (n < 0 || (size_t)n != len)
        return HUSH_ERR_FULL;
    writer->offset += len;
    return HUSH_OK;
}

static hush_status_t hush_proto_put_string(hush_proto_writer_t *writer,
                                           const char *text)
{
    int n;

    assert(writer != NULL);
    assert(text != NULL);
    if (hush_json_escape(text, writer->scratch, writer->scratch_capacity) == 0 &&
        text[0] != '\0')
        return HUSH_ERR_FULL;
    n = snprintf(writer->out + writer->offset,
                 writer->capacity - writer->offset, "\"%s\"",
                 writer->scratch);
    if (n < 0 || (size_t)n >= writer->capacity - writer->offset)
        return HUSH_ERR_FULL;
    writer->offset += (size_t)n;
    return HUSH_OK;
}

static hush_status_t hush_proto_put_u32(hush_proto_writer_t *writer,
                                        uint32_t value)
{
    char text[HUSH_PROTO_NUMBER_MAX];
    int n = snprintf(text, sizeof(text), "%u", (unsigned)value);

    if (n <= 0 || (size_t)n >= sizeof(text))
        return HUSH_ERR_FULL;
    return hush_proto_put_raw(writer, text);
}

static hush_status_t hush_proto_put_i64(hush_proto_writer_t *writer,
                                        int64_t value)
{
    char text[HUSH_PROTO_NUMBER_MAX];
    int n = snprintf(text, sizeof(text), "%lld", (long long)value);

    if (n <= 0 || (size_t)n >= sizeof(text))
        return HUSH_ERR_FULL;
    return hush_proto_put_raw(writer, text);
}
