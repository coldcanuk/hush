/* tests/test_dir.c: private-directory creation rejects files and symlinks. */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hush_dir.h"

static int g_fail;

static void expect(int cond, const char *msg)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail = 1;
    }
}

int main(void)
{
    char root[] = "/tmp/hush-dir-XXXXXX";
    char path[256];
    char link[256];
    struct stat st;
    FILE *fp;

    if (mkdtemp(root) == NULL)
        return 1;
    snprintf(path, sizeof(path), "%s/target", root);
    expect(hush_dir_ensure_private(path) == HUSH_OK, "creates dir");
    expect(stat(path, &st) == 0 && S_ISDIR(st.st_mode), "is dir");
    expect((st.st_mode & (S_IRWXG | S_IRWXO)) == 0, "mode 0700");
    expect(hush_dir_ensure_private(path) == HUSH_OK, "existing dir ok");
    snprintf(path, sizeof(path), "%s/file", root);
    fp = fopen(path, "w");
    if (fp != NULL)
        fclose(fp);
    expect(hush_dir_ensure_private(path) == HUSH_ERR_DENIED, "file rejected");
    snprintf(link, sizeof(link), "%s/link", root);
    if (symlink(root, link) == 0)
        expect(hush_dir_ensure_private(link) == HUSH_ERR_DENIED,
               "symlink rejected");
    expect(hush_dir_ensure_private(NULL) == HUSH_ERR_ARG, "NULL rejected");
    expect(hush_dir_ensure_private("") == HUSH_ERR_ARG, "empty rejected");
    if (g_fail)
        return 1;
    printf("test_dir ok\n");
    return 0;
}
