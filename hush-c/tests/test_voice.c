/* tests/test_voice.c: VOICE V1 capability probe pins (stt, tts, voices). */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hush_voice.h"

enum {
    TV_ROOT_MAX = 64,
    TV_PATH_MAX = 512,
    TV_JSON_MAX = 256,
    TV_SHORT_BUF = 8,
    TV_MODE_EXEC = 0755,
    TV_MODE_PLAIN = 0644,
    TV_DIR_MODE = 0700
};

static int g_fail;
static char g_root[TV_ROOT_MAX];

/* Records a failed pin by name. */
static void tv_expect(int cond, const char *pin)
{
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", pin);
        g_fail = 1;
    }
}

/* Writes "<root>/<rel>" into out. */
static void tv_path(char *out, size_t outsz, const char *rel)
{
    int n = snprintf(out, outsz, "%s/%s", g_root, rel);
    if (n < 0 || (size_t)n >= outsz)
        out[0] = '\0';
}

/* Creates an empty file at <root>/<rel> with mode. */
static void tv_touch(const char *rel, mode_t mode)
{
    char full[TV_PATH_MAX];
    tv_path(full, sizeof(full), rel);
    FILE *f = fopen(full, "w");
    if (f == NULL) {
        tv_expect(0, "fixture file");
        return;
    }
    (void)fputs("#!/bin/sh\nexit 0\n", f);
    (void)fclose(f);
    (void)chmod(full, mode);
}

/* Creates <root>/<rel> as a directory. */
static void tv_mkdir(const char *rel)
{
    char full[TV_PATH_MAX];
    tv_path(full, sizeof(full), rel);
    (void)mkdir(full, TV_DIR_MODE);
}

/* Points PATH at <root>/<rel> only. */
static void tv_set_path(const char *rel)
{
    char full[TV_PATH_MAX];
    tv_path(full, sizeof(full), rel);
    (void)setenv("PATH", full, 1);
}

/* Probes and formats the current host into json. */
static hush_voice_caps_t tv_probe(char *json, size_t jsonsz)
{
    hush_voice_caps_t caps = {0};
    tv_expect(hush_voice_probe(&caps) == HUSH_OK, "probe ok");
    tv_expect(hush_voice_format(json, jsonsz, &caps) == HUSH_OK, "format ok");
    return caps;
}

static void tv_pin_format(void)
{
    char json[TV_JSON_MAX];
    char tiny[TV_SHORT_BUF];
    hush_voice_caps_t none = {0};
    hush_voice_caps_t both = {
        .stt = 1, .tts_count = 2,
        .tts = { HUSH_VOICE_ENGINE_PIPER, HUSH_VOICE_ENGINE_ESPEAK },
        .voices = 12
    };

    tv_expect(hush_voice_format(json, sizeof(json), &none) == HUSH_OK, "V-F1 ok");
    tv_expect(strcmp(json, "\"stt\":false,\"tts\":[],\"voices\":0") == 0,
              "V-F1 absent json");
    tv_expect(hush_voice_format(json, sizeof(json), &both) == HUSH_OK, "V-F2 ok");
    tv_expect(strcmp(json, "\"stt\":true,\"tts\":[\"piper\",\"espeak\"],\"voices\":12")
                  == 0, "V-F2 both json");
    tv_expect(hush_voice_format(tiny, sizeof(tiny), &both) == HUSH_ERR_FULL,
              "V-F3 short buffer FULL");
    tv_expect(hush_voice_format(NULL, sizeof(json), &both) == HUSH_ERR_ARG,
              "V-F4 NULL out ARG");
    tv_expect(hush_voice_probe(NULL) == HUSH_ERR_ARG, "V-F5 NULL probe ARG");
    tv_expect(strcmp(hush_voice_engine_name(HUSH_VOICE_ENGINE_COUNT), "") == 0,
              "V-F6 unknown engine name empty");
}

static void tv_pin_tts(void)
{
    char json[TV_JSON_MAX];

    tv_mkdir("empty");
    tv_set_path("empty");
    hush_voice_caps_t caps = tv_probe(json, sizeof(json));
    tv_expect(caps.tts_count == 0, "V-T1 piper absent -> tts empty");
    tv_expect(strstr(json, "\"tts\":[]") != NULL, "V-T1 json tts []");

    tv_mkdir("plain");
    tv_touch("plain/piper", TV_MODE_PLAIN);
    tv_set_path("plain");
    caps = tv_probe(json, sizeof(json));
    tv_expect(caps.tts_count == 0, "V-T2 non-executable piper not detected");

    tv_mkdir("piper");
    tv_touch("piper/piper", TV_MODE_EXEC);
    tv_set_path("piper");
    caps = tv_probe(json, sizeof(json));
    tv_expect(strstr(json, "\"tts\":[\"piper\"]") != NULL, "V-T3 piper present");

    tv_mkdir("espeak");
    tv_touch("espeak/espeak-ng", TV_MODE_EXEC);
    tv_set_path("espeak");
    caps = tv_probe(json, sizeof(json));
    tv_expect(strstr(json, "\"tts\":[\"espeak\"]") != NULL,
              "V-T4 piper absent -> next engine espeak");

    char both[TV_PATH_MAX * 2];
    int n = snprintf(both, sizeof(both), "%s/espeak::%s/piper", g_root, g_root);
    tv_expect(n > 0 && (size_t)n < sizeof(both), "V-T5 PATH fixture");
    (void)setenv("PATH", both, 1);
    caps = tv_probe(json, sizeof(json));
    tv_expect(strstr(json, "\"tts\":[\"piper\",\"espeak\"]") != NULL,
              "V-T5 chain order piper then espeak");
}

static void tv_pin_voices(void)
{
    char json[TV_JSON_MAX];
    char home[TV_PATH_MAX];

    tv_mkdir("home");
    tv_mkdir("home/voices");
    tv_mkdir("home/voices/piper");
    tv_touch("home/voices/piper/en_US-amy-low.onnx", TV_MODE_PLAIN);
    tv_touch("home/voices/piper/en_GB-alan-low.onnx", TV_MODE_PLAIN);
    tv_touch("home/voices/piper/en_GB-alan-low.onnx.json", TV_MODE_PLAIN);
    tv_touch("home/voices/piper/.onnx", TV_MODE_PLAIN);
    tv_path(home, sizeof(home), "home");
    (void)setenv("HUSH_HOME", home, 1);

    tv_set_path("piper");
    hush_voice_caps_t caps = tv_probe(json, sizeof(json));
    tv_expect(caps.voices == 2, "V-V1 piper present counts 2 .onnx models");
    tv_expect(strstr(json, "\"voices\":2") != NULL, "V-V1 json voices 2");

    tv_set_path("empty");
    caps = tv_probe(json, sizeof(json));
    tv_expect(caps.voices == 0, "V-V2 piper absent -> voices 0");
}

static void tv_pin_stt(void)
{
    char json[TV_JSON_MAX];
    int system_whisper = access("/usr/bin/whisper", X_OK) == 0 ||
                         access("/usr/local/bin/whisper", X_OK) == 0;

    (void)setenv("HUSH_WHISPER", "1", 1);
    hush_voice_caps_t caps = tv_probe(json, sizeof(json));
    tv_expect(caps.stt == 1, "V-S1 HUSH_WHISPER=1 -> stt true");
    tv_expect(strstr(json, "\"stt\":true") != NULL, "V-S1 json stt true");

    (void)unsetenv("HUSH_WHISPER");
    if (system_whisper) {
        printf("SKIP V-S2: a system whisper binary is installed\n");
        return;
    }
    caps = tv_probe(json, sizeof(json));
    tv_expect(caps.stt == 0, "V-S2 no whisper -> stt false");
    tv_expect(strstr(json, "\"stt\":false") != NULL, "V-S2 json stt false");
}

int main(void)
{
    char tmpl[] = "/tmp/hush-test-voice-XXXXXX";
    if (mkdtemp(tmpl) == NULL) {
        fprintf(stderr, "FAIL mkdtemp\n");
        return 1;
    }
    memcpy(g_root, tmpl, sizeof(tmpl));
    tv_pin_format();
    tv_pin_tts();
    tv_pin_voices();
    tv_pin_stt();
    char cmd[TV_PATH_MAX];
    int n = snprintf(cmd, sizeof(cmd), "/bin/rm -rf '%s'", g_root);
    tv_expect(n > 0 && (size_t)n < sizeof(cmd) && system(cmd) == 0,
              "remove own temp dir");
    if (g_fail)
        return 1;
    printf("test_voice ok\n");
    return 0;
}
