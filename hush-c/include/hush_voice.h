/* hush_voice.h: voice capability probe (STT and TTS detection only). */

#ifndef HUSH_VOICE_H
#define HUSH_VOICE_H

#include <stddef.h>

#include "hush_status.h"

/* Server TTS engines in fallback-chain order (PLAN_VOICE section 2.3, D5):
 * Piper first, espeak-ng next. Browser and provider TTS are not probed here. */
typedef enum {
    HUSH_VOICE_ENGINE_PIPER = 0,
    HUSH_VOICE_ENGINE_ESPEAK = 1,
    HUSH_VOICE_ENGINE_COUNT = 2
} hush_voice_engine_t;

enum {
    HUSH_VOICE_PATH_MAX = 512,
    HUSH_VOICE_MODELS_MAX = 256,
    HUSH_VOICE_SCAN_MAX = 4096,
    HUSH_VOICE_JSON_MAX = 96
};

/* Piper: binary, JSON engine name and model subdir share one spelling. */
#define HUSH_VOICE_NAME_PIPER "piper"

/* Piper voice models live as <hush home>/voices/piper/<name>.onnx. */
#define HUSH_VOICE_DIR "voices"
#define HUSH_VOICE_PIPER_SUBDIR HUSH_VOICE_NAME_PIPER
#define HUSH_VOICE_MODEL_EXT ".onnx"

/* Invariant: tts[0..tts_count) holds distinct engines in chain order, and
 * voices is 0 unless HUSH_VOICE_ENGINE_PIPER is among them. */
typedef struct {
    int                 stt;
    size_t              tts_count;
    hush_voice_engine_t tts[HUSH_VOICE_ENGINE_COUNT];
    size_t              voices;
} hush_voice_caps_t;

/* Fills caps from the host: stt reuses hush_turn_whisper_available; each TTS
 * engine is listed only when its binary is executable on PATH; voices counts
 * Piper models (capped at HUSH_VOICE_MODELS_MAX). Fails HUSH_ERR_ARG on NULL. */
hush_status_t hush_voice_probe(hush_voice_caps_t *out_caps);

/* Writes the JSON members "stt":b,"tts":[..],"voices":n (no braces) into out.
 * Fails HUSH_ERR_ARG on NULL or zero size, HUSH_ERR_FULL when out is short. */
hush_status_t hush_voice_format(char *out, size_t outsz,
                                const hush_voice_caps_t *caps);

/* Stable engine name used in JSON ("piper", "espeak"). "" when unknown. */
const char *hush_voice_engine_name(hush_voice_engine_t engine);

#endif /* HUSH_VOICE_H */
