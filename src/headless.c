/* Single-threaded diagnostic headless host for Yoshi's Island (SNESRecomp Native AOT). */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include "game_rtl.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "snes/snes.h"
#include "snes/cart.h"
#include "snes/superfx.h"
#include "snes/ppu.h"
#include "snes/interp_bridge.h"

struct SpcPlayer *g_spc_player;
bool g_new_ppu = true;
extern uint64_t yi_nmi_count, yi_irq_count, yi_max_late, yi_total_slices;

void RtlApuLock(void) {}
void RtlApuUnlock(void) {}
void debug_on_block_enter(uint32_t pc, uint32_t a, uint32_t x, uint32_t y) {(void)pc;(void)a;(void)x;(void)y;}
void debug_on_wram_write_byte(uint32_t a, uint8_t b, uint8_t c) {(void)a;(void)b;(void)c;}
void debug_on_wram_write_word(uint32_t a, uint16_t b, uint16_t c) {(void)a;(void)b;(void)c;}

void NORETURN Die(const char *s) { fprintf(stderr, "FATAL: %s\n", s); exit(1); }

static uint32_t hash(const void *p, size_t n) {
  const uint8_t *b = (const uint8_t *)p;
  uint32_t h = 2166136261u;
  while (n--) h = (h ^ *b++) * 16777619u;
  return h;
}

static void save_ppm(const char *path, const uint8_t *pixels) {
  FILE *out = fopen(path, "wb");
  if (!out) return;
  fprintf(out, "P6\n256 224\n255\n");
  for (int p = 0; p < 256 * 224; p++) {
    unsigned char rgb[] = { pixels[p * 4 + 2], pixels[p * 4 + 1], pixels[p * 4] };
    fwrite(rgb, 1, 3, out);
  }
  fclose(out);
  printf("[Screenshot] Saved %s\n", path);
}

int main(int argc, char **argv) {

  const char *rom_path = (argc > 1) ? argv[1] : "Super Mario World 2 - Yoshi's Island (USA).sfc";
  int frames = (argc > 2) ? atoi(argv[2]) : 60;
  if (frames < 1) frames = 60;

  printf("=================================================================\n");
  printf("  Yoshi's Island - Diagnostic Headless Runner (SNESRecomp AOT)   \n");
  printf("=================================================================\n");
  printf("[Host] Loading ROM: %s\n", rom_path);

  FILE *f = fopen(rom_path, "rb");
  if (!f) {
    fprintf(stderr, "Error: cannot open ROM file: %s\n", rom_path);
    return 1;
  }
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  rewind(f);
  if (size != 2097152) {
    printf("[Warning] Expected 2,097,152 bytes, got %ld bytes\n", size);
  }
  uint8_t *rom = (uint8_t *)malloc(size);
  if (!rom || fread(rom, 1, size, f) != (size_t)size) {
    fprintf(stderr, "Error: failed to read ROM\n");
    fclose(f);
    return 2;
  }
  fclose(f);

  RtlRegisterGame(&kGameInfo);
  if (!SnesInit(rom, (int)size)) {
    fprintf(stderr, "Error: SnesInit failed!\n");
    free(rom);
    return 3;
  }
  free(rom);

  if (!g_snes || !g_snes->cart || !g_snes->cart->superfx) {
    fprintf(stderr, "Error: SuperFX coprocessor was not initialized!\n");
    return 4;
  }
  printf("[Host] SuperFX GSU coprocessor initialized successfully!\n");

  static uint8_t pixels[256 * 4 * 256];
  PpuBeginDrawing(g_ppu, pixels, 256 * 4, 0);

  printf("[Host] Running %d frames of recompiled ROM code...\n", frames);
  static int16_t dummy_audio[1024 * 2];
  RtlSetAudioOutputRate(32040);
  for (int i = 0; i < frames; i++) {
    uint32_t pad = 0;
    int cur_f = i + 1;
    /* Skip intro if desired: press START at f600..f610 */
    if (cur_f >= 600 && cur_f <= 620) pad = (1 << 3);
    /* Press START at Title Screen: f750..f780 */
    // if (cur_f >= 750 && cur_f <= 780) pad = (1 << 3);
    /* Press START / A on File 1: f920..f940 */
    if (cur_f >= 920 && cur_f <= 940) pad = (1 << 3) | (1 << 8);
    /* Press START / A on Stage confirmation: f1050..f1070 */
    if (cur_f >= 1050 && cur_f <= 1070) pad = (1 << 3) | (1 << 8);
    /* Advance through cutscene / dialog: */
    if (cur_f >= 1650 && cur_f <= 1680) pad = (1 << 3) | (1 << 8);
    if (cur_f >= 1800 && cur_f <= 1820) pad = (1 << 3) | (1 << 8);
    if (cur_f >= 1950 && cur_f <= 1980) pad = (1 << 3) | (1 << 8);
    RtlRunFrame(pad);
    GameDrawPpuFrame();
    RtlRenderAudio(dummy_audio, 534, 2);


    if (cur_f == 150) save_ppm("screenshot_f150.ppm", pixels);
    if (cur_f == 330) save_ppm("screenshot_f330.ppm", pixels);
    if (cur_f == 420) save_ppm("screenshot_f420.ppm", pixels);
    if (cur_f == 600) save_ppm("screenshot_f600.ppm", pixels);
    if (cur_f == 800) save_ppm("screenshot_f800.ppm", pixels);
    if (cur_f == 900) save_ppm("screenshot_f900.ppm", pixels);
    if (cur_f == 1000) save_ppm("screenshot_f1000.ppm", pixels);
    if (cur_f == 1100) save_ppm("screenshot_f1100.ppm", pixels);
    if (cur_f == 1150) save_ppm("screenshot_f1150.ppm", pixels);
    if (cur_f == 1350) save_ppm("screenshot_f1350.ppm", pixels);
    if (cur_f == 1550) save_ppm("screenshot_f1550.ppm", pixels);
    if (cur_f == 1800) save_ppm("screenshot_f1800.ppm", pixels);
    if (cur_f == 2100) save_ppm("screenshot_f2100.ppm", pixels);
    if (cur_f == 2400) save_ppm("screenshot_f2400.ppm", pixels);

    if ((cur_f >= 640 && cur_f <= 760) || cur_f % 30 == 0 || i == frames - 1) {
      uint32_t ram_hash = hash(g_ram, 128 * 1024);
      uint32_t vram_hash = hash(g_ppu->vram, sizeof(g_ppu->vram));
      int non_black = 0;
      for (int p = 0; p < 256 * 224; p++) {
        if (pixels[p * 4] || pixels[p * 4 + 1] || pixels[p * 4 + 2]) non_black++;
      }
      printf("[Frame %04d] Mode: 0x%02X, PC: 0x%06X, INIDISP: 0x%02X, TM: 0x%02X, BGMode: %d, NonBlack: %d, NMI: %llu, IRQ: %llu\n",
             cur_f, g_ram[0x0118], GameGetResumePc(), g_ppu->inidisp, g_ppu->screenEnabled[0],
             g_ppu->bgmode, non_black,
             (unsigned long long)yi_nmi_count, (unsigned long long)yi_irq_count);
      fflush(stdout);
    }


  }

  printf("\n[Success] %d frames executed successfully through authentic SNES recompilation!\n", frames);
  save_ppm("screenshot_recomp.ppm", pixels);
  return 0;
}

