#ifndef YI_TITLE_RESTART_H
#define YI_TITLE_RESTART_H
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game_rtl.h"
#include "common_rtl.h"
/* Read before touching the live machine. A failed read leaves it resumable.
 * A not-yet-created save is a legitimate new game, not an IO error. */
static bool YiTitleRestartReady(const char *path, YiRomProfile profile) {
  if(!g_sram || g_sram_size<=0)return false;
  unsigned char *saved=calloc(1,(size_t)g_sram_size);
  if(!saved)return false;
  FILE *f=fopen(path,"rb");
  if(f) {
    size_t n=fread(saved,1,(size_t)g_sram_size,f);
    bool ok=n==(size_t)g_sram_size && !ferror(f);
    if(fclose(f))ok=false;
    if(!ok){free(saved);return false;}
  } else if(errno!=ENOENT){free(saved);return false;}
  GameMachineReset(profile);
  memcpy(g_sram,saved,(size_t)g_sram_size);
  free(saved);return true;
}
#endif
