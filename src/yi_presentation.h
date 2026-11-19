#pragma once
#include <SDL.h>
#include <stdbool.h>
typedef struct YiPresentation {
  SDL_Texture *texture;
  unsigned width, height, scale;
  uint8_t filtered[342 * 224 * 36];
} YiPresentation;
bool YiPresentationDraw(YiPresentation *, SDL_Renderer *, const uint8_t *native,
                        const uint8_t *wide, bool is_wide, unsigned effect,
                        int native_logical_w, int native_logical_h,
                        uint64_t *filter_ticks);
void YiPresentationDestroy(YiPresentation *);
