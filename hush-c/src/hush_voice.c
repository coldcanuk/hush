/* hush_voice.c: owns the voice capability probe: STT (Whisper) and server TTS
 * engine detection plus the Piper model count. Detection only; no audio. */

#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hush_home.h"
#include "hush_turn.h"
#include "hush_voice.h"

/* Binary each engine needs on PATH, indexed by hush_voice_engine_t. */
static const char *const hush_voice_engine_bins[HUSH_VOICE_ENGINE_COUNT] = {
    [HUSH_VOICE_ENGINE_PIPER] = HUSH_VOICE_NAME_PIPER,
    [HUSH_VOICE_ENGINE_ESPEAK] = "espeak-ng"
};

/* JSON name each engine reports in /api/status "tts". */
static const char *const hush_voice_engine_names[HUSH_VOICE_ENGINE_COUNT] = {
    [HUSH_VOICE_ENGINE_PIPER] = HUSH_VOICE_NAME_PIPER,
    [HUSH_VOICE_ENGINE_ESPEAK] = "espeak"
};

/* Appends every engine whose binary is on PATH, in chain order. */
static void hush_voice_probe_tts(hush_voice_caps_t *caps);

/* True when caps lists engine. Pure. */
static int hush_voice_has_engine(const hush_voice_caps_t *caps,
                                 hush_voice_engine_t engine);

/* True when prog (no slash) is executable in some PATH entry. */
static int hush_voice_on_path(const char *prog);

/* True when dir[0..dirlen) joined with prog is executable. */
static int hush_voice_dir_has(const char *dir, size_t dirlen, const char *prog);

/* Writes <hush home>/voices/piper into out. Empty on overflow. */
static void hush_voice_models_dir(char *out, size_t outsz);

/* Counts *.onnx entries in <hush home>/voices/piper. 0 when it is absent. */
static size_t hush_voice_count_models(void);

/* Counts model entries left in an open dir, scanning a bounded number. */
static size_t hush_voice_count_entries(DIR *dir);

/* True when name ends in HUSH_VOICE_MODEL_EXT with a non-empty stem. Pure. */
static int hush_voice_is_model(const char *name);

/* Writes the "tts" JSON array for caps into out. FULL when out is short. */
static hush_status_t hush_voice_format_tts(char *out, size_t outsz,
                                           const hush_voice_caps_t *caps);

hush_status_t hush_voice_probe(hush_voice_caps_t *out_caps)
{
    if (out_caps == NULL)
        return HUSH_ERR_ARG;
    memset(out_caps, 0, sizeof(*out_caps));
    out_caps->stt = hush_turn_whisper_available();
    hush_voice_probe_tts(out_caps);
    if (hush_voice_has_engine(out_caps, HUSH_VOICE_ENGINE_PIPER))
        out_caps->voices = hush_voice_count_models();
    return HUSH_OK;
}

hush_status_t hush_voice_format(char *out, size_t outsz,
                                const hush_voice_caps_t *caps)
{
    char tts[HUSH_VOICE_JSON_MAX] = {0};

    if (out == NULL || outsz == 0 || caps == NULL)
        return HUSH_ERR_ARG;
    HUSH_TRY(hush_voice_format_tts(tts, sizeof(tts), caps));
    int n = snprintf(out, outsz, "\"stt\":%s,\"tts\":%s,\"voices\":%zu",
                     caps->stt ? "true" : "false", tts, caps->voices);
    if (n < 0 || (size_t)n >= outsz)
        return HUSH_ERR_FULL;
    return HUSH_OK;
}

const char *hush_voice_engine_name(hush_voice_engine_t engine)
{
    if ((size_t)engine >= (size_t)HUSH_VOICE_ENGINE_COUNT)
        return "";
    return hush_voice_engine_names[engine];
}

static void hush_voice_probe_tts(hush_voice_caps_t *caps)
{
    assert(caps != NULL);
    assert(caps->tts_count == 0);
    for (size_t i = 0; i < (size_t)HUSH_VOICE_ENGINE_COUNT; ++i) {
        if (!hush_voice_on_path(hush_voice_engine_bins[i]))
            continue;
        caps->tts[caps->tts_count] = (hush_voice_engine_t)i;
        caps->tts_count++;
    }
    assert(caps->tts_count <= (size_t)HUSH_VOICE_ENGINE_COUNT);
}

static int hush_voice_has_engine(const hush_voice_caps_t *caps,
                                 hush_voice_engine_t engine)
{
    assert(caps != NULL);
    assert(caps->tts_count <= (size_t)HUSH_VOICE_ENGINE_COUNT);
    for (size_t i = 0; i < caps->tts_count; ++i) {
        if (caps->tts[i] == engine)
            return 1;
    }
    return 0;
}

static int hush_voice_on_path(const char *prog)
{
    assert(prog != NULL);
    assert(strchr(prog, '/') == NULL);
    const char *path = getenv("PATH");
    if (path == NULL || path[0] == '\0')
        return 0;
    const char *cur = path;
    size_t steps = strlen(path) + 1;
    /* Bounded: each step consumes one ':'-separated entry, at most
     * strlen(path)+1 entries exist. */
    for (size_t step = 0; step < steps; ++step) {
        const char *end = strchr(cur, ':');
        size_t dirlen = (end != NULL) ? (size_t)(end - cur) : strlen(cur);
        if (hush_voice_dir_has(cur, dirlen, prog))
            return 1;
        if (end == NULL)
            return 0;
        cur = end + 1;
    }
    return 0;
}

static int hush_voice_dir_has(const char *dir, size_t dirlen, const char *prog)
{
    char full[HUSH_VOICE_PATH_MAX] = {0};

    assert(dir != NULL);
    assert(prog != NULL);
    /* An empty PATH entry means the cwd; a capability probe ignores it. */
    if (dirlen == 0)
        return 0;
    size_t proglen = strlen(prog);
    if (dirlen + proglen + 2 > sizeof(full))
        return 0;
    memcpy(full, dir, dirlen);
    full[dirlen] = '/';
    memcpy(full + dirlen + 1, prog, proglen + 1);
    return access(full, X_OK) == 0;
}

static void hush_voice_models_dir(char *out, size_t outsz)
{
    char root[HUSH_HOME_PATH_MAX] = {0};
    char voices[HUSH_HOME_PATH_MAX] = {0};

    assert(out != NULL);
    assert(outsz > 0);
    hush_home_root(root, sizeof(root));
    hush_home_join(voices, sizeof(voices), root, HUSH_VOICE_DIR);
    hush_home_join(out, outsz, voices, HUSH_VOICE_PIPER_SUBDIR);
}

static size_t hush_voice_count_models(void)
{
    char piper[HUSH_HOME_PATH_MAX] = {0};

    hush_voice_models_dir(piper, sizeof(piper));
    if (piper[0] == '\0')
        return 0;
    DIR *dir = opendir(piper);
    if (dir == NULL)
        return 0;
    size_t count = hush_voice_count_entries(dir);
    (void)closedir(dir);
    return count;
}

static size_t hush_voice_count_entries(DIR *dir)
{
    assert(dir != NULL);
    size_t count = 0;
    for (size_t seen = 0; seen < (size_t)HUSH_VOICE_SCAN_MAX; ++seen) {
        const struct dirent *ent = readdir(dir);
        if (ent == NULL)
            break;
        if (hush_voice_is_model(ent->d_name))
            count++;
        if (count == (size_t)HUSH_VOICE_MODELS_MAX)
            break;
    }
    assert(count <= (size_t)HUSH_VOICE_MODELS_MAX);
    return count;
}

static int hush_voice_is_model(const char *name)
{
    assert(name != NULL);
    size_t len = strlen(name);
    size_t extlen = strlen(HUSH_VOICE_MODEL_EXT);
    if (len <= extlen)
        return 0;
    return strcmp(name + len - extlen, HUSH_VOICE_MODEL_EXT) == 0;
}

static hush_status_t hush_voice_format_tts(char *out, size_t outsz,
                                           const hush_voice_caps_t *caps)
{
    assert(out != NULL);
    assert(outsz > 2);
    assert(caps != NULL);
    size_t used = 0;
    out[used++] = '[';
    for (size_t i = 0; i < caps->tts_count; ++i) {
        const char *sep = (i == 0) ? "" : ",";
        int n = snprintf(out + used, outsz - used, "%s\"%s\"", sep,
                         hush_voice_engine_name(caps->tts[i]));
        if (n < 0 || (size_t)n >= outsz - used)
            return HUSH_ERR_FULL;
        used += (size_t)n;
    }
    if (used + 2 > outsz)
        return HUSH_ERR_FULL;
    out[used++] = ']';
    out[used] = '\0';
    return HUSH_OK;
}
