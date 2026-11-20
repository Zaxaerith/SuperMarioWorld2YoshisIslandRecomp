#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
struct SDL_Renderer;
void YiAchievementsInit(const char *preferences);
void YiAchievementsDrawLauncher(const char *rom);
void YiAchievementsStart(int profile,const char *save_path,const uint8_t *ram,const uint8_t *sram,unsigned size);
void YiAchievementsFrame(void);
void YiAchievementsOverview(int action);
void YiAchievementsPresent(struct SDL_Renderer *renderer);
void YiAchievementsShutdown(void);
#ifdef __cplusplus
}
#endif
