/* hush_dir.c: owns shared private-directory creation for state and cwd owners. */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hush_dir.h"

hush_status_t hush_dir_ensure_private(const char *path)
{
    struct stat st;

    if (path == NULL || path[0] == '\0')
        return HUSH_ERR_ARG;
    if (mkdir(path, (mode_t)HUSH_DIR_MODE_PRIVATE) == 0)
        return HUSH_OK;
    if (errno != EEXIST)
        return HUSH_ERR_IO;
    if (lstat(path, &st) != 0)
        return HUSH_ERR_IO;
    if (!S_ISDIR(st.st_mode) || st.st_uid != getuid())
        return HUSH_ERR_DENIED;
    if ((st.st_mode & (S_IRWXG | S_IRWXO)) != 0 &&
        chmod(path, (mode_t)HUSH_DIR_MODE_PRIVATE) != 0)
        return HUSH_ERR_DENIED;
    return HUSH_OK;
}
