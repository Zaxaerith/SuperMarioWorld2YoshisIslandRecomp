#ifndef YI_MENU_GAME_INPUT_H
#define YI_MENU_GAME_INPUT_H
#include <stdbool.h>
#include <stdint.h>
#include "rom_profile.h"
/* Mirror the original $01:C0FF pause gate and $01:C14B cleared-stage check.
 * All supported ROMs keep these WRAM/SuperFX-RAM fields at the same offsets. */
static bool YiMenuCanExitLevel(const uint8_t *ram,const uint8_t *sram,YiRomProfile profile) {
  int shift=YiRomIsPal(profile)?4:0;
  unsigned stage=ram[0x21a];
  if(ram[0x118]!=0x0f+shift || stage>=72 || !(ram[0x222+stage]&0x7f) || ram[0xd0f])return false;
  if(ram[0xb0f])return true; /* Native pause is already open. */
  return !(sram[0x1fea] || ram[0xb65] || ram[0xb59] || ram[0x398] ||
    sram[0x1ae] || sram[0x1b0]) && sram[0xac]<6;
}
typedef struct YiMenuGameInput { unsigned exit_ticks; } YiMenuGameInput;
static uint32_t YiMenuGameInputNext(YiMenuGameInput *s,uint32_t input) {
  if(s->exit_ticks) {
    unsigned tick=6-s->exit_ticks--;
    return tick>=2 && tick<4 ? (1u<<3)|(1u<<2) : 0;
  }
  return input;
}
#endif
