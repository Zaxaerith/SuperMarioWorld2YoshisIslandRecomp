/* Offline diagnostic tool: reads an existing screenshot or a synthetic fixture.
 * Never runs game code, opens a window or uses an account. */
#include "yi_effect_diagnostics.h"
#include "yi_video_effects.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int review(SDL_Surface *source, unsigned id) {
  SDL_Surface *s = SDL_ConvertSurfaceFormat(source, SDL_PIXELFORMAT_BGRA32, 0);
  if (!s)
    return 1;
  size_t size = (size_t)s->pitch * s->h;
  uint8_t *original = malloc(size), *out = malloc((size_t)s->w * s->h * 36);
  if (!original || !out)
    return 1;
  memcpy(original, s->pixels, size);
  SDL_Surface *canvas = SDL_CreateRGBSurfaceWithFormat(
      0, s->w * 3, s->h * 3, 32, SDL_PIXELFORMAT_BGRA32);
  SDL_Renderer *renderer = canvas ? SDL_CreateSoftwareRenderer(canvas) : NULL;
  SDL_Texture *texture =
      renderer ? SDL_CreateTextureFromSurface(renderer, s) : NULL;
  if (!texture || SDL_RenderCopy(renderer, texture, NULL, NULL) != 0)
    return 1;
  SDL_DestroyTexture(texture);
  if (!renderer ||
      !YiEffectsCapture(renderer, s->pixels, s->w, s->h, s->pitch, id, -1))
    return 1;
  for (unsigned mode = 1; mode < YI_EFFECT_COUNT; ++mode) {
    char name[128];
    snprintf(name, sizeof(name), "effects-%u-%s.bmp", id, YiEffectName(mode));
    SDL_Surface *saved = SDL_LoadBMP(name);
    if (!saved || saved->w != s->w * 3 || saved->h != s->h * 3)
      return 1;
    SDL_FreeSurface(saved);
    uint64_t begin = SDL_GetPerformanceCounter();
    for (unsigned frame = 0; frame < 120; ++frame)
      if (!YiEffectApply(mode, s->pixels, s->w, s->h, s->pitch, out,
                         (size_t)s->w * s->h * 36))
        return 1;
    double ms = (SDL_GetPerformanceCounter() - begin) * 1000.0 /
                SDL_GetPerformanceFrequency() / 120;
    printf("[EffectsBench] offline pixels only; source=%dx%d mode=%s "
           "frames=120 filter_ms=%.4f\n",
           s->w, s->h, YiEffectName(mode), ms);
  }
  if (memcmp(original, s->pixels, size))
    return 1;
  SDL_DestroyRenderer(renderer);
  SDL_FreeSurface(canvas);
  SDL_FreeSurface(s);
  free(out);
  free(original);
  return 0;
}
int main(int argc, char **argv) {
  if (argc != 2)
    return 2;
  if (strcmp(argv[1], "--synthetic")) {
    SDL_Surface *source = SDL_LoadBMP(argv[1]);
    if (!source)
      return 2;
    int result = review(source, 1);
    SDL_FreeSurface(source);
    return result;
  }
  const unsigned widths[] = {256, 342};
  for (unsigned i = 0; i < 2; ++i) {
    SDL_Surface *source = SDL_CreateRGBSurfaceWithFormat(
        0, widths[i], 224, 32, SDL_PIXELFORMAT_BGRA32);
    if (!source)
      return 1;
    for (unsigned y = 0; y < 224; ++y)
      for (unsigned x = 0; x < widths[i]; ++x)
        ((uint32_t *)((uint8_t *)source->pixels + y * source->pitch))[x] =
            0xff000000u | (x * 17u << 8) | (y * 31u);
    int result = review(source, i + 1);
    if(!result) {
      const unsigned selected_id=100+i;
      if(!YiEffectsCapture(NULL,source->pixels,source->w,source->h,source->pitch,selected_id,YI_EFFECT_CRT))return 1;
      char name[128];snprintf(name,sizeof(name),"effects-%u-CRT.bmp",selected_id);
      SDL_Surface* captured=SDL_LoadBMP(name);if(!captured)return 1;SDL_FreeSurface(captured);
      snprintf(name,sizeof(name),"effects-%u-Sharp.bmp",selected_id);
      FILE* unexpected=fopen(name,"rb");if(unexpected){fclose(unexpected);return 1;}
    }
    SDL_FreeSurface(source);
    if (result)
      return result;
  }
  puts("PASS native/wide same-frame capture files, 3x sizes, source "
       "preservation and offline filter benchmark.");
  return 0;
}
