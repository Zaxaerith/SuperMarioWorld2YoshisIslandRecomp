#pragma once
#include <SDL.h>
#include <stdbool.h>
bool YiEffectsCapture(SDL_Renderer *, const uint8_t *, unsigned width,
                      unsigned height, unsigned pitch, uint64_t id,
                      int selected_mode);
void YiEffectsRecord(unsigned mode, unsigned width, unsigned height,
                     uint64_t filter_ticks, bool vsync);
