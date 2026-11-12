/* PAL timing, ROM identity and interpreter regression tests. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game_rtl.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "snes/snes.h"
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

static int test_pal_timing(void) {
    uint8_t *ram = calloc(1, 0x20000);
    TEST_ASSERT(ram != NULL, "allocate synthetic WRAM");
    Snes *machine = snes_init(ram);
    TEST_ASSERT(machine != NULL, "allocate synthetic SNES");
    TEST_ASSERT(snes_frame_lines(machine) == 262, "default stays NTSC");
    snes_set_pal(machine, true);
    TEST_ASSERT(snes_frame_lines(machine) == 312, "PAL field has 312 lines");
    TEST_ASSERT((ppu_read(machine->ppu, 0x3f) & 0x10) != 0, "STAT78 advertises PAL");
    snes_reset(machine, true);
    TEST_ASSERT(machine->pal && machine->ppu->pal, "region survives hardware reset");
    machine->vPos = 311; machine->hPos = 1360;
    snes_advance_master_cycles(machine, 8);
    TEST_ASSERT(machine->vPos == 0 && machine->hPos == 4, "PAL beam wraps after line 311");
    machine->vPos = 310; machine->hPos = 0;
    TEST_ASSERT(snes_master_clocks_until_line(machine, 1) == 3 * 1364,
                "line deadline wraps at PAL field boundary");
    machine->vIrqEnabled = true; machine->vTimer = 300;
    TEST_ASSERT(snes_master_clocks_until_irq(machine) == 302 * 1364,
                "PAL IRQ deadline can target line 300 next field");
    uint64_t deadline = 0;
    TEST_ASSERT(snes_next_irq_master(machine, 100, &deadline) && deadline == 100 + 302 * 1364,
                "absolute IRQ deadline agrees with relative one");
    machine->vPos = 299; machine->hPos = 1360; machine->inIrq = false;
    snes_advance_master_cycles(machine, 8);
    TEST_ASSERT(machine->inIrq && machine->vPos == 300, "IRQ fires beyond NTSC line limit");
    TEST_ASSERT(YiRomFrameRate(YI_ROM_EU_11) > 50 && YiRomFrameRate(YI_ROM_EU_11) < 50.1,
                "PAL host pacing is near 50 Hz");
    TEST_ASSERT(snes_apu_per_master(machine) * 21281370.0 > 1025279.9 &&
                snes_apu_per_master(machine) * 21281370.0 < 1025280.1,
                "PAL produces 32040 DSP samples per real second");
    snes_set_pal(machine, false);
    TEST_ASSERT(!(ppu_read(machine->ppu, 0x3f) & 0x10), "NTSC status preserved");
    machine->vIrqEnabled = false; machine->vPos = 261; machine->hPos = 1360;
    snes_advance_master_cycles(machine, 8);
    TEST_ASSERT(machine->vPos == 0 && machine->hPos == 4, "NTSC wrap unchanged");
    snes_free(machine); free(ram);
    return 1;
}

/* The fixture calls a PC for which the US build has an AOT body. The fixture's
 * bytes intentionally differ: PAL must execute them even in an interrupt and
 * even when scheduler acceleration was explicitly requested. */
static int test_revision_execution_floor(void) {
    uint8_t *rom = calloc(1, YI_ROM_SIZE);
    TEST_ASSERT(rom != NULL, "allocate synthetic ROM");
    memcpy(rom + 0x7fc0, "PAL CONTRACT FIXTURE  ", 21);
    rom[0x7fd5] = 0x20; rom[0x7fd6] = 0; rom[0x7fd7] = 11;
    rom[0x7fd9] = 2; rom[0x7fdc] = 0xff; rom[0x7fdd] = 0xff;
    rom[0x7ffc] = 0; rom[0x7ffd] = 0x80;
    const uint8_t call_and_wait[] = {0x22, 0x39, 0x82, 0x00, 0xcb};
    const uint8_t call_and_rti[] = {0x22, 0x39, 0x82, 0x00, 0x40};
    const uint8_t marker[] = {0xa9, 0x42, 0x85, 0x20, 0x6b};
    memcpy(rom, call_and_wait, sizeof(call_and_wait));
    memcpy(rom + 0x100, call_and_rti, sizeof(call_and_rti));
    memcpy(rom + 0x239, marker, sizeof(marker));
    RtlRegisterGame(&kGameInfo);
    TEST_ASSERT(SnesInit(rom, YI_ROM_SIZE) != NULL, "initialize synthetic cartridge");
    free(rom);
    GameConfigureRom(YI_ROM_EU_11);
    interp_bridge_set_scheduler_aot_policy(1);
    g_snes->nmiEnabled = true;
    TEST_ASSERT(interp_bridge_run_until_quiescent(&g_cpu, 0x008000), "synthetic mainline reaches WAI");
    TEST_ASSERT(g_ram[0x20] == 0x42 && g_snes->nmiEnabled,
                "PAL mainline uses fixture bytes instead of US disable_nmi AOT");
    g_ram[0x20] = 0;
    uint16_t stack = g_cpu.S;
    cpu_push_interrupt_frame_at(&g_cpu, 0x008005);
    TEST_ASSERT(interp_bridge_run_interrupt(&g_cpu, 0x008100), "synthetic handler reaches RTI");
    TEST_ASSERT(g_ram[0x20] == 0x42 && g_snes->nmiEnabled && g_cpu.S == stack,
                "PAL interrupt also excludes US AOT and balances the hardware frame");
    g_cpu.emulation = 0;
    g_ram[0x20] = 0;
    cpu_push_interrupt_frame_at(&g_cpu, 0x008005);
    TEST_ASSERT(interp_bridge_run_interrupt(&g_cpu, 0x008100), "native synthetic handler reaches RTI");
    TEST_ASSERT(g_ram[0x20] == 0x42 && g_cpu.S == stack,
                "native interrupt consumes the additional bank byte");
    interp_bridge_set_scheduler_aot_policy(-1);
    interp_bridge_set_aot_enabled(true);
    snes_free(g_snes); g_snes = NULL; g_ppu = NULL;
    return 1;
}

static int test_rom_identity(const char *path, YiRomProfile expected) {
    FILE *file = fopen(path, "rb");
    TEST_ASSERT(file != NULL, "open ROM identity fixture");
    uint8_t *data = calloc(1, YI_ROM_SIZE + 512);
    TEST_ASSERT(data != NULL, "allocate identity buffer");
    size_t size = fread(data, 1, YI_ROM_SIZE + 512, file);
    int extra = fgetc(file);
    fclose(file);
    TEST_ASSERT(extra == EOF && YiIdentifyRom(data, size) == expected, "exact revision identified");
    if (size == YI_ROM_SIZE) {
        memmove(data + 512, data, size);
        memset(data, 0, 512);
        TEST_ASSERT(YiIdentifyRom(data, size + 512) == expected, "copier header accepted");
        data[512] ^= 1;
    } else {
        data[512] ^= 1;
    }
    TEST_ASSERT(YiIdentifyRom(data, size == YI_ROM_SIZE ? size + 512 : size) == YI_ROM_UNSUPPORTED,
                "modified payload rejected");
    free(data);
    return 1;
}

static int test_invalid_identity(void) {
    uint8_t *data = calloc(1, YI_ROM_SIZE);
    TEST_ASSERT(data != NULL, "allocate invalid identity fixture");
    TEST_ASSERT(YiIdentifyRom(NULL, YI_ROM_SIZE) == YI_ROM_UNSUPPORTED, "null rejected");
    TEST_ASSERT(YiIdentifyRom(data, 1024) == YI_ROM_UNSUPPORTED, "short payload rejected");
    TEST_ASSERT(YiIdentifyRom(data, YI_ROM_SIZE) == YI_ROM_UNSUPPORTED, "invalid payload rejected");
    free(data);
    return 1;
}

static int test_frame_cadence(YiRomProfile profile) {
    uint8_t *rom = calloc(1, YI_ROM_SIZE);
    TEST_ASSERT(rom != NULL, "allocate timing cartridge");
    memcpy(rom + 0x7fc0, "PAL TIMING FIXTURE   ", 21);
    rom[0x7fd5] = 0x20; rom[0x7fd7] = 11;
    rom[0x7fd9] = YiRomIsPal(profile) ? 2 : 1;
    rom[0x7fdc] = rom[0x7fdd] = 0xff;
    rom[0x7ffc] = 0; rom[0x7ffd] = 0x80;
    const uint8_t wait[] = {0x58, 0xcb, 0x80, 0xfd}; /* CLI; WAI; BRA WAI */
    memcpy(rom, wait, sizeof(wait));
    TEST_ASSERT(SnesInit(rom, YI_ROM_SIZE) != NULL, "initialize timing cartridge");
    free(rom);
    GameConfigureRom(profile);
    interp_bridge_set_aot_enabled(false);
    uint8_t *pixels = calloc(256, 256 * 4);
    TEST_ASSERT(pixels != NULL, "allocate framebuffer");
    PpuBeginDrawing(g_ppu, pixels, 256 * 4, 0);
    uint64_t field = (YiRomIsPal(profile) ? 312u : 262u) * 1364u;
    for (unsigned frame = 1; frame <= 2; ++frame) {
        RtlRunFrame(0);
        TEST_ASSERT(g_cpu.master_cycles >= frame * field &&
                    g_cpu.master_cycles < frame * field + 100,
                    "frame scheduler follows the selected field length");
    }
    free(pixels);
    snes_free(g_snes); g_snes = NULL; g_ppu = NULL;
    interp_bridge_set_aot_enabled(true);
    return 1;
}

int main(int argc, char **argv) {
    if (!test_invalid_identity() || !test_pal_timing() || !test_revision_execution_floor())
        return 1;
    if (!test_frame_cadence(YI_ROM_US_10) || !test_frame_cadence(YI_ROM_EU_11))
        return 1;
    if (argc > 1 && !test_rom_identity(argv[1], YI_ROM_US_10)) return 1;
    if (argc > 2 && !test_rom_identity(argv[2], YI_ROM_EU_11)) return 1;
    printf("PAL regression tests: %u assertions passed\n", assertions);
    return 0;
}
