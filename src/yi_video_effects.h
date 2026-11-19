#ifndef YI_VIDEO_EFFECTS_H
#define YI_VIDEO_EFFECTS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum {
  YI_EFFECT_ORIGINAL,
  YI_EFFECT_SCANLINES,
  YI_EFFECT_CRT,
  YI_EFFECT_LCD,
  YI_EFFECT_SHARP,
  YI_EFFECT_COUNT
};
const char *YiEffectName(unsigned mode);
unsigned YiEffectNext(unsigned mode);
bool YiEffectKeyDown(unsigned *mode, int configured, int pressed, bool repeat,
                     bool modified);
bool YiEffectApply(unsigned mode, const uint8_t *input, unsigned width,
                   unsigned height, unsigned pitch, uint8_t *output,
                   size_t capacity);
#endif
