#include "yi_presentation.h"
#include "yi_video_effects.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                          \
      return 1;                                                                \
    }                                                                          \
  } while (0)
static YiPresentation state;
static uint32_t native[256 * 224], wide[342 * 224];
int main(void) {
  for (unsigned i = 0; i < 256 * 224; ++i)
    native[i] = 0xff112233u;
  for (unsigned i = 0; i < 342 * 224; ++i)
    wide[i] = 0xff445566u;
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  SDL_Surface *surface =
      SDL_CreateRGBSurfaceWithFormat(0, 2400, 1350, 32, SDL_PIXELFORMAT_BGRA32);
  CHECK(surface);
  SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surface);
  CHECK(renderer);
  const bool sequence[] = {true, false, true, false};
  for (unsigned mode = 0; mode < YI_EFFECT_COUNT; ++mode)
    for (unsigned step = 0; step < 4; ++step) {
      bool is_wide = sequence[step];
      uint64_t ticks;
      CHECK(YiPresentationDraw(&state, renderer, (uint8_t *)native,
                               (uint8_t *)wide, is_wide, mode, 2400, 1800,
                               &ticks));
      CHECK(state.width == (is_wide ? 342 : 256) &&
            state.scale == (mode ? 3 : 1));
      CHECK(SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_BGRA32,
                                 surface->pixels, surface->pitch) == 0);
      uint32_t center = ((uint32_t *)((uint8_t *)surface->pixels +
                                      675 * surface->pitch))[1200];
      if (mode == YI_EFFECT_ORIGINAL || mode == YI_EFFECT_SHARP)
        CHECK((center & 0xffffff) == (is_wide ? 0x445566 : 0x112233));
      if (is_wide)
        CHECK((((uint32_t *)surface->pixels)[0] & 0xffffff) == 0);
    }
  CHECK(native[0] == 0xff112233 && wide[0] == 0xff445566);
  YiPresentationDestroy(&state);
  SDL_DestroyRenderer(renderer);
  SDL_FreeSurface(surface);
  puts("PASS actual combined presenter: all modes, native/wide/fallback "
       "transitions, texture sizing, aspect border and source preservation. No "
       "game/window/account.");
  return 0;
}
