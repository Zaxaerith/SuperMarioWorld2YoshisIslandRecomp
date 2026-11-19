#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct SDL_Renderer;
void YiRaInit(const char *preferences_path);
void YiRaDrawLauncher(const char *rom_path, void *unused);
void YiRaStartGame(const char *rom_path, const uint8_t *ram,
                   const uint8_t *sram, unsigned sram_size);
void YiRaFrame(void);
void YiRaOverlay(struct SDL_Renderer *renderer);
void YiRaPresent(struct SDL_Renderer *renderer);
void YiRaCaptureNextPresent(uint64_t capture_id);
void YiRaOverview(int action);
void YiRaShutdown(void);
#ifdef __cplusplus
}
#endif
