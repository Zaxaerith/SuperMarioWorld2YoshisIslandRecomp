#ifndef GAME_RTL_H
#define GAME_RTL_H

#include "common_cpu_infra.h"
#include "rom_profile.h"

void GameConfigureRom(YiRomProfile profile);

/* One frame of Super Mario World 2: Yoshi's Island */
void GameRunOneFrame(void);
void GameDrawPpuFrame(void);
void GameSessionReset(void);
/* Call at a stopped frame boundary with audio paused. SRAM is preserved. */
void GameMachineReset(YiRomProfile profile);
uint32_t GameGetResumePc(void);

extern const RtlGameInfo kGameInfo;

#endif /* GAME_RTL_H */

