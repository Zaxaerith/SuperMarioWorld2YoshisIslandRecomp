/* Standalone synthetic raster regression tests. No game ROM is required. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game_rtl.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "snes/snes.h"
#include "snes/dma.h"
#include "snes/ppu.h"
#include "snes/interp_bridge.h"

struct SpcPlayer *g_spc_player;
bool g_new_ppu = true;
void RtlApuLock(void) {}
void RtlApuUnlock(void) {}
void debug_on_block_enter(uint32_t pc, uint32_t a, uint32_t x, uint32_t y) {
  (void)pc; (void)a; (void)x; (void)y;
}
void debug_on_wram_write_byte(uint32_t a, uint8_t b, uint8_t c) {
  (void)a; (void)b; (void)c;
}
void debug_on_wram_write_word(uint32_t a, uint16_t b, uint16_t c) {
  (void)a; (void)b; (void)c;
}
void Die(const char *message) { fprintf(stderr, "%s\n", message); exit(1); }

static unsigned assertions;
#define TEST_ASSERT(condition, message) do { \
  ++assertions; \
  if (!(condition)) { \
    fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, message); \
    return 0; \
  } \
} while (0)

typedef struct RasterProbe { unsigned count, line[8], h[8]; uint8_t brightness[8]; } RasterProbe;
static void observe_raster(Snes *s, unsigned line, void *context) {
    RasterProbe *p = context;
    if (p->count < 8) {
        p->line[p->count] = line; p->h[p->count] = s->hPos;
        p->brightness[p->count] = s->ppu->inidisp;
    }
    p->count++;
}

static int test_beam_scanout_before_hdma(void) {
    uint8_t *ram = calloc(1, 0x20000);
    TEST_ASSERT(ram != NULL, "allocate raster WRAM");
    Snes *s = snes_init(ram);
    g_ppu = s->ppu;
    RasterProbe p = {0};
    s->scanlineHook = observe_raster; s->scanlineHookContext = &p;
    s->ppu->inidisp = 15;
    /* Repeat-transfer descriptor: HDMA changes brightness to 0 then 15.
     * A CPU slice can cross all three lines before returning to its host. */
    ram[0x1000] = 0x82; ram[0x1001] = 0; ram[0x1002] = 15; ram[0x1003] = 0;
    DmaChannel *ch = &s->dma->channel[0];
    ch->hdmaActive = true; ch->aBank = 0x7e; ch->aAdr = 0x1000; ch->bAdr = 0;
    dma_initHdma(s->dma);
    snes_advance_master_cycles(s, 1364 * 3);
    TEST_ASSERT(p.count == 3 && p.line[0] == 0 && p.line[1] == 1 && p.line[2] == 2,
                "one scanout per line even when one CPU slice crosses three lines");
    TEST_ASSERT(p.h[0] == 1024 && p.h[1] == 1024 && p.h[2] == 1024,
                "scanout callback observes the real beam boundary");
    TEST_ASSERT(p.brightness[0] == 15 && p.brightness[1] == 0 && p.brightness[2] == 15,
                "each line sees its own state before the following HDMA write");
    /* An IRQ at the HBlank boundary must not omit or double the line. */
    s->hPos = 0; s->beamMasterLast = 0; s->inIrq = false;
    s->dma->channel[0].hdmaActive = false; p = (RasterProbe){0};
    s->vPos = 1; s->hIrqEnabled = s->vIrqEnabled = true;
    s->vTimer = 1; s->hTimer = 256;
    snes_advance_master_cycles(s, 1200);
    TEST_ASSERT(s->inIrq && p.count == 1 && p.line[0] == 1,
                "IRQ at HBlank preserves exactly one completed-line callback");
    snes_advance_master_cycles(s, 100);
    TEST_ASSERT(p.count == 1, "IRQ catch-up does not repeat scanout/HDMA for the same line");
    /* Disabling beam-owned HDMA must not suppress independent scanout. */
    s->hIrqEnabled = s->vIrqEnabled = s->inIrq = false;
    s->hdmaBeamOff = true; s->hPos = 0; s->vPos = 224;
    p = (RasterProbe){0};
    snes_advance_master_cycles(s, 1364 * 2);
    TEST_ASSERT(p.count == 1 && p.line[0] == 224,
                "last visible line renders with HDMA disabled; vblank does not");
    /* Field wrap must resume scanout at line zero, not during vblank. */
    s->hPos = 0; s->vPos = 262 - 1;
    p = (RasterProbe){0};
    snes_advance_master_cycles(s, 1364 * 2);
    TEST_ASSERT(p.count == 1 && p.line[0] == 0,
                "field wrap produces exactly one preline callback");
    s->scanlineHook = NULL;
    snes_advance_master_cycles(s, 1364);
    TEST_ASSERT(p.count == 1, "hosts without a scanline callback remain supported");
    snes_free(s); g_ppu = NULL; free(ram); return 1;
}

static int test_irq_registers_and_frame_scanout(void) {
    uint8_t *rom = calloc(1, (2u * 1024u * 1024u));
    TEST_ASSERT(rom != NULL, "allocate synthetic IRQ cartridge");
    memcpy(rom + 0x7fc0, "RASTER IRQ FIXTURE    ", 21);
    rom[0x7fd5] = 0x20; rom[0x7fd7] = 11; rom[0x7fd9] = 1;
    rom[0x7fdc] = rom[0x7fdd] = 0xff;
    rom[0x7ffc] = 0; rom[0x7ffd] = 0x80;
    rom[0x7ffe] = rom[0x7fee] = 0; rom[0x7fff] = rom[0x7fef] = 0x81;
    const uint8_t mainline[] = {0x58, 0xcb, 0x80, 0xfd}; /* CLI; WAI; BRA WAI */
    const uint8_t irq[] = {
        0xa9,0x22,0x8d,0x01,0x43, /* DMA0 destination */
        0xa9,0x90,0x8d,0x02,0x43, 0xa9,0x12,0x8d,0x03,0x43,
        0xa9,0x7e,0x8d,0x04,0x43,
        0xa9,0x34,0x8d,0x05,0x43, 0xa9,0x12,0x8d,0x06,0x43,
        0xa9,0x01,0x8d,0x00,0x43,
        0xad,0x11,0x42,0x40 /* acknowledge IRQ; RTI */
    };
    memcpy(rom, mainline, sizeof(mainline)); memcpy(rom + 0x100, irq, sizeof(irq));
    RtlRegisterGame(&kGameInfo);
    TEST_ASSERT(SnesInit(rom, (2u * 1024u * 1024u)) != NULL, "initialize synthetic IRQ machine");
    free(rom); GameSessionReset();
    /* This cartridge contains fixture opcodes, never the game's AOT routines. */
    interp_bridge_set_scheduler_aot_policy(0);
    uint8_t *pixels = calloc(224, 256 * 4);
    TEST_ASSERT(pixels != NULL, "allocate synthetic framebuffer");
    PpuBeginDrawing(g_ppu, pixels, 256 * 4, 0);
    g_snes->vIrqEnabled = true; g_snes->vTimer = 10;
    RtlRunFrame(0);
    DmaChannel *ch = &g_snes->dma->channel[0];
    TEST_ASSERT(ch->bAdr == 0x22 && ch->aAdr == 0x1290 && ch->aBank == 0x7e &&
                ch->size == 0x1234 && ch->mode == 1,
                "RTI must retain guest-written DMA0 registers");
    TEST_ASSERT(g_cpu.master_cycles >= 262u * 1364u &&
                g_cpu.master_cycles < 262u * 1364u + 100,
                "frame follows the existing NTSC field length");
    RasterProbe probe = {0};
    TEST_ASSERT(g_snes->scanlineHook != NULL, "frame engine binds beam scanout");
    g_snes->scanlineHook = observe_raster;
    g_snes->scanlineHookContext = &probe;
    RtlRunFrame(0);
    TEST_ASSERT(probe.count == 225 && probe.line[0] == 0 && probe.line[1] == 1,
                "each field emits the preline and 224 visible lines exactly once");
    interp_bridge_set_scheduler_aot_policy(-1);
    free(pixels); snes_free(g_snes); g_snes = NULL; g_ppu = NULL;
    return 1;
}

int main(void) {
  if (!test_beam_scanout_before_hdma() || !test_irq_registers_and_frame_scanout())
    return 1;
  printf("Raster regression tests: %u assertions passed\n", assertions);
  return 0;
}
