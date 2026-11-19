#include "yi_video_effects.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                               \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(x)) {                                                                \
      fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                          \
      return 1;                                                                \
    }                                                                          \
  } while (0)
int main(void) {
  unsigned checks = 0, mode = 0;
  uint8_t source[32], original[32], output[4 * 2 * 36 + 32];
  for (unsigned i = 0; i < sizeof(source); ++i)
    source[i] = (uint8_t)(i * 7);
  memcpy(original, source, sizeof(source));
  CHECK(!YiEffectKeyDown(&mode, 7, 8, false, false) && mode == 0);
  const unsigned cycle[] = {0, 4, 1, 2, 3};
  for (unsigned i = 1; i <= 2 * YI_EFFECT_COUNT; ++i) {
    CHECK(YiEffectKeyDown(&mode, 7, 7, false, false) &&
          mode == cycle[i % YI_EFFECT_COUNT]);
    CHECK(YiEffectKeyDown(&mode, 7, 7, true, false) &&
          mode == cycle[i % YI_EFFECT_COUNT]);
  }
  CHECK(!YiEffectKeyDown(&mode, 7, 7, false, true) && mode == 0);
  CHECK(!YiEffectApply(0, source, 4, 2, 16, output, sizeof(output)));
  for (unsigned effect = 1; effect < YI_EFFECT_COUNT; ++effect) {
    memset(output, 0xa5, sizeof(output));
    CHECK(YiEffectApply(effect, source, 4, 2, 16, output, sizeof(output) - 32));
    CHECK(!memcmp(original, source, sizeof(source)));
    for (unsigned i = 4 * 2 * 36; i < sizeof(output); ++i)
      CHECK(output[i] == 0xa5);
    for (unsigned i = 3; i < 4 * 2 * 36; i += 4)
      CHECK(output[i] == 255);
  }
  /* Sharp preserves every source RGB value in its complete 3x3 footprint,
   * including image boundaries; it must not introduce halos or scanlines. */
  CHECK(
      YiEffectApply(YI_EFFECT_SHARP, source, 4, 2, 16, output, sizeof(output)));
  CHECK(!memcmp(source, original, sizeof(source)));
  for (unsigned y = 0; y < 6; ++y)
    for (unsigned x = 0; x < 12; ++x)
      for (unsigned c = 0; c < 3; ++c)
        CHECK(output[(y * 12 + x) * 4 + c] ==
              source[((y / 3) * 4 + x / 3) * 4 + c]);
  CHECK(!strcmp(YiEffectName(YI_EFFECT_SHARP), "Sharp"));
  CHECK(YiEffectNext(YI_EFFECT_SHARP) == YI_EFFECT_SCANLINES);
  CHECK(!YiEffectApply(1, source, 4, 2, 16, output, 4 * 2 * 36 - 1));
  CHECK(!YiEffectApply(1, source, 343, 224, 1372, output, sizeof(output)));
  /* Full-size filter must handle native/wide edges and write exactly its
   * budget. */
  size_t count = 342u * 224u;
  uint8_t *frame = malloc(count * 4), *wide = malloc(count * 36 + 16);
  CHECK(frame && wide);
  memset(frame, 255, count * 4);
  for (unsigned effect = 1; effect < YI_EFFECT_COUNT; ++effect) {
    memset(wide, 0xa5, count * 36 + 16);
    CHECK(YiEffectApply(effect, frame, 342, 224, 342 * 4, wide, count * 36));
    for (unsigned i = 0; i < 16; ++i)
      CHECK(wide[count * 36 + i] == 0xa5);
    CHECK((effect == YI_EFFECT_SHARP ? wide[342 * 3 * 2 * 4] == 255
                                     : wide[342 * 3 * 2 * 4] < 255) &&
          frame[0] == 255);
  }
  free(frame);
  free(wide);
  printf("Video effects: %u assertions passed\n", checks);
  return 0;
}
