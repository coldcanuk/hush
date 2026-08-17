/* hush_filter.c: owns NIP-01 filter parsing representation and matching logic for Hush. */

#include <stdbool.h>
#include <string.h>

#include "hush_filter.h"

/* True if needle exactly matches one of the first n entries in the fixed-width array. Pure. */
static bool hush_str_in_array(const char *needle, const char arr[][65], size_t n);

/* True if k is present in the first n entries of arr. Pure. */
static bool hush_kind_in_array(uint32_t k, const uint32_t *arr, size_t n);

bool hush_filter_match(const hush_filter_t *f, const hush_event_t *ev)
{
    if (f == NULL || ev == NULL)
        return false;

    if (f->kinds_len > 0 && !hush_kind_in_array(ev->kind, f->kinds, f->kinds_len))
        return false;

    if (f->authors_len > 0 && !hush_str_in_array(ev->pubkey, f->authors, f->authors_len))
        return false;

    if (f->since != 0 && ev->created_at < f->since)
        return false;
    if (f->until != 0 && ev->created_at > f->until)
        return false;

    if (f->ids_len > 0 && !hush_str_in_array(ev->id, f->ids, f->ids_len))
        return false;

    /* tag match simplified: only first tag key "h" for MVP */
    for (size_t ti = 0; ti < f->tag_count; ++ti) {
        if (strcmp(f->tag_keys[ti], "h") == 0) {
            bool matched = false;
            for (size_t vi = 0; vi < f->tag_vals_len[ti]; ++vi) {
                for (size_t ei = 0; ei < ev->tag_count; ++ei) {
                    if (strcmp(ev->tags[ei][0], "h") == 0 &&
                        strcmp(ev->tags[ei][1], f->tag_vals[ti][vi]) == 0) {
                        matched = true;
                    }
                }
            }
            if (!matched)
                return false;
        }
    }
    return true;
}

static bool hush_str_in_array(const char *needle, const char arr[][65], size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        if (strcmp(needle, arr[i]) == 0)
            return true;
    }
    return false;
}

static bool hush_kind_in_array(uint32_t k, const uint32_t *arr, size_t n)
{
    for (size_t i = 0; i < n; ++i) {
        if (arr[i] == k)
            return true;
    }
    return false;
}
