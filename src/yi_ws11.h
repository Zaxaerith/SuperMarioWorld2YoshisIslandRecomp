#ifndef YI_WS11_H
#define YI_WS11_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "rom_profile.h"
#include "snes/ppu.h"

enum { YI_WS_EXTRA = 43, YI_WS_WIDTH = 342, YI_WS_HEIGHT = 224 };
void YiWs11Init(YiRomProfile profile);
void YiWs11Toggle(void);
void YiWs11CaptureTerrain(const char *path);
void YiWs11SetEnabled(bool enabled);
void YiWs11RenderLine(const Ppu *native, unsigned line);
bool YiWs11Active(void);
bool YiWs11PresentationWide(void);
bool YiWs11Supported(void);
/* Compatibility query: native vertical bounds are always retained. */
bool YiWs11VerticalActive(void);
const char *YiWs11SceneName(void);
const uint8_t *YiWs11Pixels(void);
/* Read-only shared Map16 layout decoder, also exercised by synthetic contracts. */
bool YiWs11WorldTile(const uint8_t *wram, const uint8_t *gsu_ram, const uint8_t *rom, size_t rom_size,
                     int x, int y, uint16_t *tile);
#endif
