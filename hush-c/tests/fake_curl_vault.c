/* tests/fake_curl_vault.c: no-network libcurl stub for vault wipe tests. */

#define _POSIX_C_SOURCE 200809L
/* Skip curl typecheck macros so we can define curl_easy_setopt ourselves. */
#define CURL_DISABLE_TYPECHECK

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>

enum {
    FAKE_CURL_BODY_MAX = 8192,
    FAKE_CURL_URL_MAX = 256,
    FAKE_CURL_METHOD_MAX = 16
};

struct FakeEasy {
    char url[FAKE_CURL_URL_MAX];
    char method[FAKE_CURL_METHOD_MAX];
    struct curl_slist *headers;
    curl_write_callback write_cb;
    void *write_data;
    const char *post;
    long post_size;
};

static char g_reply_body[FAKE_CURL_BODY_MAX];
static long g_reply_code = 200;
static int g_fail_perform;
static int g_slist_saw_live_token_on_free;
static int g_slist_freed_token_header;
static int g_slist_token_hdr_wiped;

enum { FAKE_TOKEN_HDR_MAX = 8 };
static char *g_token_hdr_ptr[FAKE_TOKEN_HDR_MAX];
static size_t g_token_hdr_len[FAKE_TOKEN_HDR_MAX];
static int g_token_hdr_n;

void fake_curl_set_reply(long code, const char *body)
{
    size_t len = 0;

    g_reply_code = code;
    g_reply_body[0] = '\0';
    g_fail_perform = 0;
    if (body == NULL)
        return;
    len = strlen(body);
    if (len >= sizeof(g_reply_body))
        len = sizeof(g_reply_body) - 1;
    memcpy(g_reply_body, body, len);
    g_reply_body[len] = '\0';
}

void fake_curl_fail_perform(void)
{
    g_fail_perform = 1;
}

void fake_curl_reset_slist_stats(void)
{
    g_slist_saw_live_token_on_free = 0;
    g_slist_freed_token_header = 0;
    g_slist_token_hdr_wiped = 0;
    g_token_hdr_n = 0;
}

int fake_curl_slist_saw_live_token(void)
{
    return g_slist_saw_live_token_on_free;
}

int fake_curl_slist_freed_token_header(void)
{
    return g_slist_freed_token_header;
}

int fake_curl_slist_token_hdr_wiped(void)
{
    return g_slist_token_hdr_wiped;
}

CURL *curl_easy_init(void)
{
    return (CURL *)calloc(1, sizeof(struct FakeEasy));
}

void curl_easy_cleanup(CURL *easy)
{
    free(easy);
}

CURLcode curl_easy_setopt(CURL *easy, CURLoption opt, ...)
{
    struct FakeEasy *fe = (struct FakeEasy *)easy;
    va_list ap;

    if (fe == NULL)
        return CURLE_FAILED_INIT;
    va_start(ap, opt);
    if (opt == CURLOPT_URL) {
        strncpy(fe->url, va_arg(ap, char *), sizeof(fe->url) - 1);
    } else if (opt == CURLOPT_CUSTOMREQUEST) {
        strncpy(fe->method, va_arg(ap, char *), sizeof(fe->method) - 1);
    } else if (opt == CURLOPT_HTTPHEADER) {
        fe->headers = va_arg(ap, struct curl_slist *);
    } else if (opt == CURLOPT_WRITEFUNCTION) {
        fe->write_cb = va_arg(ap, curl_write_callback);
    } else if (opt == CURLOPT_WRITEDATA) {
        fe->write_data = va_arg(ap, void *);
    } else if (opt == CURLOPT_POSTFIELDS) {
        fe->post = va_arg(ap, const char *);
    } else if (opt == CURLOPT_POSTFIELDSIZE) {
        fe->post_size = va_arg(ap, long);
    } else if (opt == CURLOPT_USERAGENT || opt == CURLOPT_SSL_EC_CURVES
               || opt == CURLOPT_PROTOCOLS_STR
               || opt == CURLOPT_REDIR_PROTOCOLS_STR) {
        (void)va_arg(ap, const char *);
    } else {
        /* CURLOPT_SSL_*, FOLLOWLOCATION, TIMEOUT, SSLVERSION, etc. */
        (void)va_arg(ap, long);
    }
    va_end(ap);
    return CURLE_OK;
}

CURLcode curl_easy_perform(CURL *easy)
{
    struct FakeEasy *fe = (struct FakeEasy *)easy;
    size_t len = 0;

    if (fe == NULL)
        return CURLE_FAILED_INIT;
    if (g_fail_perform)
        return CURLE_COULDNT_CONNECT;
    len = strlen(g_reply_body);
    if (fe->write_cb != NULL && len > 0) {
        if (fe->write_cb(g_reply_body, 1, len, fe->write_data) != len)
            return CURLE_WRITE_ERROR;
    }
    return CURLE_OK;
}

CURLcode curl_easy_getinfo(CURL *easy, CURLINFO info, ...)
{
    va_list ap;
    long *out = NULL;

    (void)easy;
    if (info != CURLINFO_RESPONSE_CODE)
        return CURLE_UNKNOWN_OPTION;
    va_start(ap, info);
    out = va_arg(ap, long *);
    va_end(ap);
    if (out == NULL)
        return CURLE_BAD_FUNCTION_ARGUMENT;
    *out = g_reply_code;
    return CURLE_OK;
}

struct curl_slist *curl_slist_append(struct curl_slist *list, const char *data)
{
    struct curl_slist *node = NULL;
    struct curl_slist *walk = list;

    if (data == NULL)
        return NULL;
    node = calloc(1, sizeof(*node));
    if (node == NULL)
        return NULL;
    node->data = strdup(data);
    if (node->data == NULL) {
        free(node);
        return NULL;
    }
    if (strncmp(data, "X-Vault-Token:", 14) == 0
        && g_token_hdr_n < FAKE_TOKEN_HDR_MAX) {
        g_token_hdr_ptr[g_token_hdr_n] = node->data;
        g_token_hdr_len[g_token_hdr_n] = strlen(data) + 1;
        g_token_hdr_n++;
        g_slist_freed_token_header = 1; /* appended; free will confirm wipe */
    }
    if (list == NULL)
        return node;
    while (walk->next != NULL)
        walk = walk->next;
    walk->next = node;
    return list;
}

void curl_slist_free_all(struct curl_slist *list)
{
    while (list != NULL) {
        struct curl_slist *next = list->next;
        int idx = 0;

        if (list->data != NULL) {
            for (idx = 0; idx < g_token_hdr_n; ++idx) {
                if (g_token_hdr_ptr[idx] != list->data)
                    continue;
                /* hush should have wiped strlen+1 bytes before free. */
                {
                    size_t j = 0;
                    int live = 0;
                    int all_zero = 1;

                    for (j = 0; j < g_token_hdr_len[idx]; ++j) {
                        if (list->data[j] != '\0') {
                            all_zero = 0;
                            live = 1;
                        }
                    }
                    if (all_zero)
                        g_slist_token_hdr_wiped = 1;
                    if (live)
                        g_slist_saw_live_token_on_free = 1;
                }
            }
            free(list->data);
        }
        free(list);
        list = next;
    }
}
