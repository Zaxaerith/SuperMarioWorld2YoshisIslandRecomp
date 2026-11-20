#include "yi_presentation.h"
#include "yi_video_effects.h"
void YiPresentationDestroy(YiPresentation *state) {
  if (state->texture)
    SDL_DestroyTexture(state->texture);
  state->texture = NULL;
  state->width = state->height = state->scale = 0;
}
// Guest pixels -> native/wide selection -> optional 3x filter -> aspect-correct
// scene. Overlays may be drawn by the caller after scene presentation.
bool YiPresentationDraw(YiPresentation *state, SDL_Renderer *renderer,
                        const uint8_t *native, const uint8_t *wide,
                        bool is_wide, unsigned effect, int native_w,
                        int native_h, uint64_t *filter_ticks) {
  const unsigned width = is_wide ? 342 : 256, height = 224;
  const uint8_t *source = is_wide ? wide : native;
  if (!source || !renderer || !state || !filter_ticks ||
      effect >= YI_EFFECT_COUNT)
    return false;
  *filter_ticks = 0;
  unsigned scale = effect ? 3 : 1;
  const uint8_t *output = source;
  if (effect) {
    uint64_t start = SDL_GetPerformanceCounter();
    bool ok = YiEffectApply(effect, source, width, height, width * 4,
                            state->filtered, sizeof(state->filtered));
    *filter_ticks = SDL_GetPerformanceCounter() - start;
    if (!ok)
      return false;
    output = state->filtered;
  }
  if (!state->texture || state->width != width || state->height != height ||
      state->scale != scale) {
    YiPresentationDestroy(state);
    state->texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGRA32,
                                       SDL_TEXTUREACCESS_STREAMING,
                                       width * scale, height * scale);
    if (!state->texture)
      return false;
    state->width = width;
    state->height = height;
    state->scale = scale;
    SDL_SetTextureBlendMode(state->texture, SDL_BLENDMODE_NONE);
  }
  SDL_Rect picture = {3, 3, 2394, 1344};
  return SDL_RenderSetLogicalSize(renderer, is_wide ? 2400 : native_w,
                                  is_wide ? 1350 : native_h) == 0 &&
         SDL_UpdateTexture(state->texture, NULL, output, width * scale * 4) ==
             0 &&
         SDL_RenderClear(renderer) == 0 &&
         SDL_RenderCopy(renderer, state->texture, NULL,
                        is_wide ? &picture : NULL) == 0;
}
