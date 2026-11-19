#include "yi_effect_diagnostics.h"
#include "yi_video_effects.h"
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#endif
static bool save_pixels(const char *name, void *pixels, int w, int h,
                        int pitch) {
  SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(
      pixels, w, h, 32, pitch, SDL_PIXELFORMAT_BGRA32);
  if (!surface)
    return false;
  bool ok = SDL_SaveBMP(surface, name) == 0;
  SDL_FreeSurface(surface);
  return ok;
}
bool YiEffectsCapture(SDL_Renderer *renderer, const uint8_t *source, unsigned w,
                      unsigned h, unsigned pitch, uint64_t id) {
  if (!source || !w || w > 342 || !h || h > 224 || pitch < w * 4)
    return false;
  uint8_t *pixels = malloc((size_t)w * h * 36);
  if (!pixels)
    return false;
  char path[160];
  bool ok = true;
  snprintf(path, sizeof(path), "effects-%llu-Original.bmp",
           (unsigned long long)id);
  ok = save_pixels(path, (void *)source, w, h, pitch) && ok;
  for (unsigned mode = 1; mode < YI_EFFECT_COUNT; ++mode) {
    snprintf(path, sizeof(path), "effects-%llu-%s.bmp", (unsigned long long)id,
             YiEffectName(mode));
    ok = YiEffectApply(mode, source, w, h, pitch, pixels, (size_t)w * h * 36) &&
         save_pixels(path, pixels, w * 3, h * 3, w * 12) && ok;
  }
  free(pixels);
  int rw = 0, rh = 0;
  if (renderer && SDL_GetRendererOutputSize(renderer, &rw, &rh) == 0 &&
      rw > 0 && rh > 0) {
    SDL_Surface *present =
        SDL_CreateRGBSurfaceWithFormat(0, rw, rh, 32, SDL_PIXELFORMAT_BGRA32);
    bool saved =
        present && SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_BGRA32,
                                        present->pixels, present->pitch) == 0;
    snprintf(path, sizeof(path), "effects-%llu-present.bmp",
             (unsigned long long)id);
    saved = saved && SDL_SaveBMP(present, path) == 0;
    if (present)
      SDL_FreeSurface(present);
    ok = saved && ok;
  }
  printf("[EffectsCapture] id=%llu source=%ux%u result=%s; same-frame modes, "
         "before RA overlay\n",
         (unsigned long long)id, w, h, ok ? "ok" : "failed");
  return ok;
}
static double process_seconds(void) {
#ifdef _WIN32
  FILETIME create, exit, kernel, user;
  ULARGE_INTEGER k, u;
  if (!GetProcessTimes(GetCurrentProcess(), &create, &exit, &kernel, &user))
    return -1;
  k.LowPart = kernel.dwLowDateTime;
  k.HighPart = kernel.dwHighDateTime;
  u.LowPart = user.dwLowDateTime;
  u.HighPart = user.dwHighDateTime;
  return (double)(k.QuadPart + u.QuadPart) / 10000000.0;
#else
  return -1;
#endif
}
void YiEffectsRecord(unsigned mode, unsigned w, unsigned h, uint64_t ticks,
                     bool vsync) {
  static uint64_t start = 0, total = 0;
  static unsigned frames = 0, last_mode = 0, last_w = 0, last_h = 0;
  static bool last_vsync = false;
  static double cpu_start = 0;
  uint64_t now = SDL_GetPerformanceCounter(),
           frequency = SDL_GetPerformanceFrequency();
  if (!start || mode != last_mode || w != last_w || h != last_h ||
      vsync != last_vsync) {
    start = now;
    total = 0;
    frames = 0;
    cpu_start = process_seconds();
    last_mode = mode;
    last_w = w;
    last_h = h;
    last_vsync = vsync;
    return;
  }
  total += ticks;
  ++frames;
  double seconds = (double)(now - start) / frequency;
  if (seconds < 5)
    return;
  double cpu_end = process_seconds();
  double percent = cpu_start >= 0 && cpu_end >= 0
                       ? (cpu_end - cpu_start) * 100 / seconds
                       : -1;
  printf("[EffectsPerf] mode=%s source=%ux%u vsync=%d frames=%u seconds=%.3f "
         "fps=%.3f filter_ms=%.4f cpu_one_core_pct=%.2f\n",
         YiEffectName(mode), w, h, vsync, frames, seconds, frames / seconds,
         (double)total * 1000 / frequency / frames, percent);
  fflush(stdout);
  start = now;
  total = 0;
  frames = 0;
  cpu_start = cpu_end;
}
