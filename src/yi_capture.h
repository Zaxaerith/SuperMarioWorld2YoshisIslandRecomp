#ifndef YI_CAPTURE_H
#define YI_CAPTURE_H
#include <SDL.h>

/* SDL reads only the current viewport. An explicit output rectangle keeps its
 * offset in the destination; initialize the untouched letterbox pixels. */
static inline SDL_Surface *YiCaptureRenderer(SDL_Renderer *renderer) {
  int w = 0, h = 0;
  if (!renderer || SDL_GetRendererOutputSize(renderer, &w, &h) != 0 || w <= 0 || h <= 0)
    return NULL;
  SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_BGRA32);
  if (!surface) return NULL;
  SDL_Rect output = {0, 0, w, h};
  if (SDL_FillRect(surface, NULL, SDL_MapRGBA(surface->format, 0, 0, 0, 255)) != 0 ||
      SDL_RenderReadPixels(renderer, &output, SDL_PIXELFORMAT_BGRA32,
                           surface->pixels, surface->pitch) != 0) {
    SDL_FreeSurface(surface);
    return NULL;
  }
  /* Window backbuffer alpha is not meaningful; exported images are opaque. */
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      ((Uint8 *)surface->pixels)[y * surface->pitch + x * 4 + 3] = 255;
  return surface;
}
#endif
