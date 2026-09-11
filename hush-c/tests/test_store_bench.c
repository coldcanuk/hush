/* tests/test_store_bench.c: insert-latency benchmark for the event store.
 * Run: make tests/test_store_bench && ./tests/test_store_bench [events] [bytes]
 * Also runs as part of make test; it fails only on an insert error. */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "hush_store.h"

static double now_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0.0;
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a;
    double y = *(const double *)b;

    return x < y ? -1 : (x > y ? 1 : 0);
}

static void fill(hush_event_t *ev, unsigned index, size_t content_len)
{
    memset(ev, 0, sizeof(*ev));
    (void)snprintf(ev->id, sizeof(ev->id), "%064x", index + 1u);
    memset(ev->pubkey, 'a', sizeof(ev->pubkey) - 1);
    ev->kind = 1;
    ev->created_at = (int64_t)index;
    memset(ev->content, 'x', content_len);
    ev->content[content_len] = '\0';
}

int main(int argc, char **argv)
{
    hush_store_t *store = NULL;
    hush_event_t ev;
    double *samples;
    double compact_ms;
    char home[] = "/tmp/hush-store-bench-XXXXXX";
    size_t total = 1200;
    size_t content_len = 1024;
    size_t i;

    if (argc > 1)
        total = (size_t)strtoul(argv[1], NULL, 10);
    if (argc > 2)
        content_len = (size_t)strtoul(argv[2], NULL, 10);
    if (total == 0 || content_len == 0 ||
        content_len > (size_t)HUSH_EVENT_MAX_CONTENT)
        return 1;
    if (mkdtemp(home) == NULL)
        return 1;
    if (setenv("HUSH_HOME", home, 1) != 0)
        return 1;
    if (hush_store_create(&store) != HUSH_OK)
        return 1;
    if (hush_store_persist_open(store) != HUSH_OK)
        return 1;
    samples = calloc(total, sizeof(*samples));
    if (samples == NULL)
        return 1;
    for (i = 0; i < total; ++i) {
        double before;

        fill(&ev, (unsigned)i, content_len);
        before = now_ms();
        if (hush_store_insert(store, &ev) != HUSH_OK) {
            free(samples);
            return 1;
        }
        samples[i] = now_ms() - before;
    }
    {
        double before = now_ms();

        hush_store_destroy(store);
        compact_ms = now_ms() - before;
    }
    qsort(samples, total, sizeof(*samples), cmp_double);
    printf("store bench: events=%zu content=%zuB median=%.3fms p95=%.3fms "
           "max=%.3fms final-compact=%.1fms\n",
           total, content_len, samples[total / 2],
           samples[(total * 95) / 100], samples[total - 1], compact_ms);
    free(samples);
    return 0;
}
