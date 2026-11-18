/* Stateless presentation filters. Source pixels and guest state remain
 * untouched. */
#include "yi_video_effects.h"
const char *YiEffectName(unsigned mode) {
  static const char *const names[] = {"Original", "Scanlines", "CRT",
                                      "LCD grid", "Sharp"};
  _Static_assert(sizeof(names) / sizeof(names[0]) == YI_EFFECT_COUNT,
                 "effect names match modes");
  return names[mode < YI_EFFECT_COUNT ? mode : 0];
}
unsigned YiEffectNext(unsigned mode) {
  static const unsigned next[] = {YI_EFFECT_SHARP, YI_EFFECT_CRT, YI_EFFECT_LCD,
                                  YI_EFFECT_ORIGINAL, YI_EFFECT_SCANLINES};
  return mode < YI_EFFECT_COUNT ? next[mode] : YI_EFFECT_ORIGINAL;
}
bool YiEffectKeyDown(unsigned *mode, int configured, int pressed, bool repeat,
                     bool modified) {
  if (!mode || !configured || configured != pressed || modified)
    return false;
  if (!repeat)
    *mode = YiEffectNext(*mode);
  return true;
}
bool YiEffectApply(unsigned mode, const uint8_t *input, unsigned width,
                   unsigned height, unsigned pitch, uint8_t *output,
                   size_t capacity) {
  if (mode == 0 || mode >= YI_EFFECT_COUNT || !input || !output || width == 0 ||
      height == 0 || width > 342 || height > 224 || pitch < width * 4 ||
      capacity < (size_t)width * height * 36)
    return false;
  /* Sharp uses color-exact nearest 3x prescaling before SDL's final resize.
   * This narrows the bilinear transition between source pixels without edge
   * overshoot, new colors, temporal history or a fixed monitor resolution.
   * The other filters use the same grid for stable display masks. */
  for (unsigned y = 0; y < height; ++y) {
    for (unsigned sy = 0; sy < 3; ++sy) {
      uint8_t *dst = output + ((size_t)y * 3 + sy) * width * 12;
      for (unsigned x = 0; x < width; ++x) {
        const uint8_t *pixel = input + (size_t)y * pitch + x * 4;
        for (unsigned sx = 0; sx < 3; ++sx) {
          unsigned light =
              mode == YI_EFFECT_SHARP ? 256 : (sy == 2 ? 165 : 256);
          if (mode == YI_EFFECT_LCD)
            light = (sx == 2 || sy == 2) ? 190 : 256;
          if (mode == YI_EFFECT_CRT) {
            unsigned edge_x = x < width / 2 ? x : width - 1 - x;
            unsigned edge_y = y < height / 2 ? y : height - 1 - y;
            unsigned edge = edge_x < edge_y ? edge_x : edge_y;
            light = light * (edge < 24 ? 224 + edge : 248) / 256;
          }
          for (unsigned c = 0; c < 3; ++c) {
            unsigned mask = mode == YI_EFFECT_CRT && c != 2 - sx ? 220 : 256;
            dst[c] = (uint8_t)((unsigned)pixel[c] * light * mask / 65536);
          }
          dst[3] = 255;
          dst += 4;
        }
      }
    }
  }
  return true;
}
