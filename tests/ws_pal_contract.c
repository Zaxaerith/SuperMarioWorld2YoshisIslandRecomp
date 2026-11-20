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

/* Synthetic hardware/render fixtures. Never execute the user's game ROM. */
#include "yi_ws11.h"
#include "snes/superfx.h"
#include "snes/cart.h"
#include <stddef.h>

static void fixture_word(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void fixture_long(uint8_t *p, uint32_t v) { fixture_word(p, v); p[2] = v >> 16; }
static void count_gsu_observation(void *context, const SuperFx *fx) {
    (void)fx; ++*(unsigned *)context;
}

static uint16_t fixture_immediate(void *context,const SuperFx *fx,uint32_t pc,uint16_t value,unsigned bytes) {
    (void)fx; ++*(unsigned*)context;
    if(pc==0 && bytes==1 && value==8)return (uint16_t)-35;
    if(pc==2 && bytes==2 && value==232)return 275;
    return value;
}
static int test_gsu_immediate_operand(void) {
    /* Fictional GSU program: IBT r4,8; IWT r5,232; STOP. No game code runs. */
    uint8_t rom[]={0xa4,8,0xf5,232,0,0,1,1};
    uint8_t *a=calloc(1,0x10000),*b=calloc(1,0x10000);
    SuperFx *plain=superfx_create(rom,sizeof(rom),a,0x10000);
    SuperFx *wide=superfx_create(rom,sizeof(rom),b,0x10000);
    TEST_ASSERT(a && b && plain && wide,"allocate synthetic immediate cores");
    unsigned calls=0;wide->immediate_override=fixture_immediate;wide->immediate_context=&calls;
    superfx_cpu_write_io(plain,0x301e,0);superfx_cpu_write_io(plain,0x301f,0);
    superfx_cpu_write_io(wide,0x301e,0);superfx_cpu_write_io(wide,0x301f,0);
    superfx_sync(plain,10000);superfx_sync(wide,10000);
    TEST_ASSERT(calls==2 && wide->r[4].data==(uint16_t)-35 && wide->r[5].data==275,
      "actual IBT/IWT pipeline supplies signed and word-wide edge operands at correct PCs");
    TEST_ASSERT(plain->r[4].data==8 && plain->r[5].data==232,"unconfigured core retains native operands");
    plain->r[4]=wide->r[4];plain->r[5]=wide->r[5];
    TEST_ASSERT(!memcmp(&plain->r,&wide->r,offsetof(SuperFx,instruction_count)-offsetof(SuperFx,r)) &&
      plain->instruction_count==wide->instruction_count && !memcmp(a,b,0x10000),
      "operand substitution changes only selected registers, not timing flags camera or sprite memory");
    superfx_reset(wide);
    TEST_ASSERT(wide->immediate_override==fixture_immediate && wide->immediate_context==&calls,
      "host operand configuration survives hardware reset outside guest save state");
    superfx_destroy(plain);superfx_destroy(wide);free(a);free(b);return 1;
}
static int test_gsu_observer_boundary(void) {
    uint8_t rom[0x10000] = {1, 0}; /* NOP; STOP */
    uint8_t *ram1 = calloc(1, 0x10000), *ram2 = calloc(1, 0x10000);
    TEST_ASSERT(ram1 && ram2, "allocate isolated GSU RAM");
    SuperFx *plain = superfx_create(rom, sizeof(rom), ram1, 0x10000);
    SuperFx *observed = superfx_create(rom, sizeof(rom), ram2, 0x10000);
    TEST_ASSERT(plain && observed, "allocate isolated GSU cores");
    unsigned calls = 0;
    observed->observe = count_gsu_observation;
    observed->observe_context = &calls;
    observed->observe_pc = 0;
    superfx_cpu_write_io(plain, 0x301e, 0); superfx_cpu_write_io(plain, 0x301f, 0);
    superfx_cpu_write_io(observed, 0x301e, 0); superfx_cpu_write_io(observed, 0x301f, 0);
    superfx_sync(plain, 10000); superfx_sync(observed, 10000);
    TEST_ASSERT(calls == 1, "GSU observer sees actual opcode PC, not the pipeline lookahead");
    TEST_ASSERT(!memcmp(&plain->r, &observed->r, offsetof(SuperFx, instruction_count) - offsetof(SuperFx, r)),
                "observer preserves architectural GSU state and timing");
    TEST_ASSERT(!memcmp(ram1, ram2, 0x10000), "observer preserves GSU RAM");
    superfx_destroy(plain); superfx_destroy(observed); free(ram1); free(ram2);
    return 1;
}

static int test_ws_world_decoder(void) {
    uint8_t *ram = calloc(1, 0x20000), *gsu = calloc(1, 0x10000), *rom = calloc(1, YI_ROM_SIZE);
    TEST_ASSERT(ram && gsu && rom, "allocate Map16 fixtures");
    /* Distinct screen and Map16 page; all four quadrants must survive. */
    gsu[0xcaa + 2 * 16 + 3] = 5;
    fixture_word(ram + 0x18000 + 5 * 0x200 + 0x40 * 2 + (0x20 >> 3), 0x0102);
    fixture_word(rom + 0xc32a4 + 2, 0x100);
    for (unsigned i = 0; i < 4; ++i) fixture_word(rom + 0xc33f2 + 0x100 + 2 * 8 + i * 2, 0x2001 + i);
    uint16_t out = 0;
    TEST_ASSERT(YiWs11WorldTile(ram, gsu, rom, YI_ROM_SIZE, 0x320, 0x240, &out) && out == 0x2001, "Map16 top-left");
    TEST_ASSERT(YiWs11WorldTile(ram, gsu, rom, YI_ROM_SIZE, 0x328, 0x240, &out) && out == 0x2002, "Map16 top-right");
    TEST_ASSERT(YiWs11WorldTile(ram, gsu, rom, YI_ROM_SIZE, 0x320, 0x248, &out) && out == 0x2003, "Map16 bottom-left");
    TEST_ASSERT(YiWs11WorldTile(ram, gsu, rom, YI_ROM_SIZE, 0x328, 0x248, &out) && out == 0x2004, "Map16 bottom-right");
    TEST_ASSERT(!YiWs11WorldTile(ram, gsu, rom, YI_ROM_SIZE, -1, 0, &out) &&
                !YiWs11WorldTile(ram, gsu, rom, YI_ROM_SIZE, 4096, 0, &out), "no world-map wrap at room edges");
    fixture_word(ram + 0x18000 + 5 * 0x200 + 0x80 + 4, 0xff02);
    TEST_ASSERT(!YiWs11WorldTile(ram, gsu, rom, YI_ROM_SIZE, 0x320, 0x240, &out), "invalid Map16 page is rejected");
    free(ram); free(gsu); free(rom); return 1;
}

#ifdef YI_WIDESCREEN_PROTOTYPE
static int test_ws_detached_composition(void) {
    uint8_t *rom = calloc(1, YI_ROM_SIZE);
    TEST_ASSERT(rom != NULL, "allocate synthetic render cartridge");
    memcpy(rom + 0x7fc0, "WS11 RENDER FIXTURE   ", 21);
    rom[0x7fd5] = 0x20; rom[0x7fd6] = 0x15; rom[0x7fd7] = 11; rom[0x7fd8] = 6;
    rom[0x7fd9] = 2; rom[0x7fdc] = rom[0x7fdd] = 0xff;
    rom[0x7ffc] = 0; rom[0x7ffd] = 0x80;
    const uint8_t anchor[] = {0xac,0x10,0xf0,0x01,0x0a,0xa1,0x04,0xa2,0xf0,0x02};
    const uint8_t map[] = {0xf0,0x9e,0x40,0x11,0x51,0x12,0x52};
    /* Fictional room table, including every header-mode class. No real game
     * assets/code are embedded or executed by these presentation tests. */
    const unsigned fixture_modes[] = {0,1,2,3,5,6,7,8,9,10,11,12,13,14,15};
    for (unsigned i = 0; i < 0xde; ++i) {
      fixture_long(rom + 0x7fa24 + i*6, 0x500000 + i*16);
      fixture_long(rom + 0x7fa27 + i*6, 0x510000 + i*8);
      rom[0x100000 + i*16 + 6] = fixture_modes[i % 15] << 3;
    }
    fixture_long(rom + 0x7fa27, 0x158583); rom[0x100006] = 5 << 3;
    fixture_long(rom + 0x7fa27 + 6, 0x4ce976); rom[0x100016] = 0;
    fixture_long(rom + 0x7fa27 + 0x3a*6, 0x158642); rom[0x100000 + 0x3a*16 + 6] = 6 << 3;
    fixture_long(rom + 0x7fa27 + 0x3b*6, 0x4cea17); rom[0x100000 + 0x3b*16 + 6] = 5 << 3;
    fixture_long(rom + 0x7fa27 + 0x38*6, 0x15855e); rom[0x100000 + 0x38*16 + 6] = 14 << 3;
    fixture_long(rom + 0x7fa27 + 0x39*6, 0x158560); rom[0x100000 + 0x39*16 + 6] = 5 << 3;
    memcpy(rom + 0x431ef, anchor, sizeof(anchor)); memcpy(rom + 0x4fde8, map, sizeof(map));
    rom[0x7fa27] = 0x83; rom[0x7fa28] = 0x85; rom[0x7fa29] = 0x15;
    for (int i = 0; i < 4; ++i) fixture_word(rom + 0xc33f2 + i * 2, 1);
    /* The five cross-section room classes identified in every real ROM. */
    const unsigned cross_rooms[]={0x3b,0x42,0x58,0x72,0xbe};
    const unsigned cross_modes[]={5,1,5,1,1};
    for(unsigned i=0;i<5;++i)rom[0x100000+cross_rooms[i]*16+6]=cross_modes[i]<<3;
    rom[0x100000+0x3d*16+6]=5<<3;
    rom[0x100000+0x70*16+6]=12<<3;
    RtlRegisterGame(&kGameInfo);
    TEST_ASSERT(SnesInit(rom, YI_ROM_SIZE) != NULL, "initialize synthetic render hardware");
    const uint8_t edge_left[]={0xa2,0x60,0xa4,0x08,0x16,0x64,0x0b,0x20};
    const uint8_t edge_right[]={0xf2,0x80,0x01,0xf4,0xe8,0x00,0x16,0x64,0xe6,0x0b,0x72,0xd6};
    memcpy(g_snes->cart->rom+0x4893b,edge_left,sizeof(edge_left));
    memcpy(g_snes->cart->rom+0x48944,edge_right,sizeof(edge_right));
    free(rom); GameConfigureRom(YI_ROM_EU_12); YiWs11Init(YI_ROM_EU_12); YiWs11SetEnabled(true);
    TEST_ASSERT(g_snes->cart->superfx->observe != NULL, "PAL Rev2 anchors arm read-only capture");
    g_ram[0x118] = 0x13; g_ram[0x148] = 5; g_ram[0x146] = 0;
    fixture_word(g_ram + 0x39, 0x100); fixture_word(g_ram + 0x3b, 0x100);
    uint8_t *sram = g_snes->cart->ram;
    fixture_word(sram + 0x1e1a, 0x400);
    sram[0x2600] = 0x83; sram[0x2601] = 0x85; sram[0x2602] = 0x15;
    g_ppu->bgmode = 0x69; g_ppu->bgXsc[0] = 0x69;
    g_ppu->hScroll[0] = g_ppu->vScroll[0] = 0x100;
    g_ppu->screenEnabled[0] = 0x11; g_ppu->inidisp = 15;
    g_ppu->cgram[1] = 0x03e0; /* green BG */
    g_ppu->cgram[129] = 0x001f; /* red OBJ palette 0 */
    /* The native 256 columns occupy the second horizontal map page.
     * Both side regions are deliberately stale/blank in real VRAM. */
    for (unsigned i = 0x6c00; i < 0x7000; ++i) g_ppu->vram[i] = 1;
    for (unsigned i = 0; i < 8; ++i) { g_ppu->vram[16 + i] = 0xff; g_ppu->vram[32 + i] = 0xff; }
    for (unsigned i = 0; i < 128; ++i) g_ppu->oam[i * 2] = 0xf000;
    /* Source buffer has two active objects clipped by the original copy.
     * The native table has only the middle one. */
    for (unsigned i = 0; i < 256; ++i) fixture_word(sram + 0x200 + i * 8, 0x8000);
    const int xcoords[] = {-24, 110, 270};
    for (unsigned i = 0; i < 3; ++i) {
        fixture_word(sram + 0x200 + i * 8, (uint16_t)xcoords[i]);
        fixture_word(sram + 0x202 + i * 8, 40);
        fixture_word(sram + 0x204 + i * 8, 0x3002);
    }
    g_ppu->oam[32] = 110 | (39 << 8); g_ppu->oam[33] = 0x3002;
    g_snes->cart->superfx->observe(NULL, g_snes->cart->superfx);
    static uint8_t native[256 * 224 * 4], ram_before[0x20000], sram_before[0x10000];
    static Ppu before;
    PpuBeginDrawing(g_ppu, native, 256 * 4, 0);
    memcpy(ram_before, g_ram, sizeof(ram_before)); memcpy(sram_before, sram, sizeof(sram_before));
    CpuState cpu_before = g_cpu;
    for (unsigned line = 1; line <= 224; ++line) {
        ppu_runLine(g_ppu, line);
        before = *g_ppu;
        YiWs11RenderLine(g_ppu, line);
        if (memcmp(&before, g_ppu, sizeof(before))) {
            TEST_ASSERT(0, "detached composition must never mutate native PPU");
        }
    }
    TEST_ASSERT(YiWs11Active(), "eligible first room produces a wide frame");
    const uint8_t *wide = YiWs11Pixels();
    bool center_equal = true;
    for (unsigned y = 0; y < 224; ++y)
        if (memcmp(native + y * 256 * 4, wide + (y * YI_WS_WIDTH + YI_WS_EXTRA) * 4, 256 * 4)) center_equal = false;
    TEST_ASSERT(center_equal, "every native center pixel remains byte-identical");
    TEST_ASSERT(wide[1] == 255 && wide[(YI_WS_WIDTH - 1) * 4 + 1] == 255,
                "both margins contain actual decoded terrain on first visit");
    TEST_ASSERT(wide[(39 * YI_WS_WIDTH + YI_WS_EXTRA - 24) * 4 + 2] == 255 &&
                wide[(39 * YI_WS_WIDTH + YI_WS_EXTRA + 270) * 4 + 2] == 255,
                "captured signed sprites draw in both side margins");
    TEST_ASSERT(!memcmp(&cpu_before, &g_cpu, sizeof(g_cpu)) && !memcmp(ram_before, g_ram, sizeof(ram_before)) &&
                !memcmp(sram_before, sram, sizeof(sram_before)), "CPU, camera, objects and GSU RAM are unchanged");
    /* Global camera clamps can remain 4096 pixels wide for an isolated
     * one-screen room. Its allocated screen component is authoritative. */
    const unsigned component_cell=0xcaa+16+1;
    sram[component_cell]=1;
    memcpy(sram_before,sram,sizeof(sram_before));
    for(unsigned line=1;line<=224;++line){ppu_runLine(g_ppu,line);YiWs11RenderLine(g_ppu,line);}
    TEST_ASSERT(!YiWs11Active() && !YiWs11PresentationWide(),"isolated single-screen terrain stays native 4:3 despite broad camera clamps");
    TEST_ASSERT(!memcmp(sram_before,sram,sizeof(sram_before)) && !memcmp(&cpu_before,&g_cpu,sizeof(g_cpu)),
        "bounded-room classification never changes guest state or activation");
    sram[component_cell+1]=2;
    for(unsigned line=1;line<=224;++line){ppu_runLine(g_ppu,line);YiWs11RenderLine(g_ppu,line);}
    TEST_ASSERT(YiWs11Active() && YiWs11PresentationWide(),"a genuinely connected wider room retains its landscape extension");
    bool left_black=true;
    for(unsigned y=0;y<224;++y)for(unsigned x=0;x<YI_WS_EXTRA;++x) {
      const uint8_t *pixel=wide+(y*YI_WS_WIDTH+x)*4;if(pixel[0]||pixel[1]||pixel[2])left_black=false;
    }
    TEST_ASSERT(left_black && wide[(YI_WS_WIDTH-1)*4+1]==255,
        "confirmed left room boundary is black while the right extension stays visible");
    sram[component_cell-1]=2;sram[component_cell+1]=0;
    for(unsigned line=1;line<=224;++line){ppu_runLine(g_ppu,line);YiWs11RenderLine(g_ppu,line);}
    bool right_black=true;
    for(unsigned y=0;y<224;++y)for(unsigned x=YI_WS_EXTRA+256;x<YI_WS_WIDTH;++x) {
      const uint8_t *pixel=wide+(y*YI_WS_WIDTH+x)*4;if(pixel[0]||pixel[1]||pixel[2])right_black=false;
    }
    TEST_ASSERT(YiWs11PresentationWide() && right_black && wide[1]==255,
        "confirmed right room boundary is black without collapsing 16:9 or hiding the left extension");
    TEST_ASSERT(!memcmp(&cpu_before,&g_cpu,sizeof(g_cpu)) && !memcmp(ram_before,g_ram,sizeof(ram_before)),
        "room edge masking preserves player, camera and CPU state");
    sram[component_cell-1]=sram[component_cell]=sram[component_cell+1]=0;memcpy(sram_before,sram,sizeof(sram_before));
    /* Cross-section BG3 is generated from the top-left BG1 word of a
     * Map16 cell, not from whichever old cover occupies the scrolling slot. */
    static Ppu cover_source;
    cover_source=*g_ppu;
    uint8_t quad_source[8];memcpy(quad_source,g_snes->cart->rom+0xc33f2,8);
    uint16_t header_source=(g_ram[0x140] | g_ram[0x140 + 1]<<8);
    fixture_word(g_ram+0x140,0x0a);
    g_ppu->bgXsc[2]=0x34;g_ppu->bgTileAdr=0x200;
    g_ppu->screenEnabled[0]|=4;g_ppu->cgram[2]=0x001f;
    const unsigned poison_tiles[]={0x40,0x41,0x50,0x51};
    const unsigned cover_tiles[]={0x100,0x101,0x110,0x111,0x17f,0x180,0x18f,0x190};
    for(unsigned i=0;i<4;++i)for(unsigned y=0;y<8;++y)g_ppu->vram[0x2000+poison_tiles[i]*8+y]=0xff00;
    for(unsigned i=0;i<8;++i)for(unsigned y=0;y<8;++y)g_ppu->vram[0x2000+cover_tiles[i]*8+y]=0xff00;
    const unsigned inputs[]={0x017f,0x0180,0x01ff,0x0200,0xc180};
    for(unsigned kind=0;kind<5;++kind)for(unsigned phase=0;phase<16;++phase) {
      unsigned tile=inputs[kind];
      for(unsigned q=0;q<4;++q)fixture_word(g_snes->cart->rom+0xc33f2+q*2,tile);
      for(unsigned y=0;y<8;++y){g_ppu->vram[(tile&0x3ff)*16+y]=0x00ff;g_ppu->vram[(tile&0x3ff)*16+8+y]=0;}
      for(unsigned i=0x6800;i<0x7000;++i)g_ppu->vram[i]=tile;
      for(unsigned i=0x3400;i<0x3800;++i)g_ppu->vram[i]=0x2040;
      fixture_word(g_ram+0x39,256+phase);g_ppu->hScroll[0]=g_ppu->hScroll[2]=256+phase;
      g_ppu->vScroll[2]=g_ppu->vScroll[0];
      memcpy(ram_before,g_ram,sizeof(ram_before));
      bool preserved=true;
      for(unsigned line=1;line<=224;++line) {
        ppu_runLine(g_ppu,line);before=*g_ppu;YiWs11RenderLine(g_ppu,line);
        if(memcmp(&before,g_ppu,sizeof(before)))preserved=false;
        if(memcmp(native+(line-1)*256*4,wide+((line-1)*YI_WS_WIDTH+YI_WS_EXTRA)*4,256*4))preserved=false;
      }
      bool covered=kind==1 || kind==2 || kind==4;
      const uint8_t *left=wide+(100*YI_WS_WIDTH)*4;
      const uint8_t *right=wide+(100*YI_WS_WIDTH+YI_WS_WIDTH-1)*4;
      TEST_ASSERT(YiWs11Active() && (covered ? left[2]==255 && right[2]==255 : left[1]==255 && right[1]==255),
          "BG3 covers and transparent cells match Map16 at both sides across all 16 pixel phases");
      TEST_ASSERT(preserved && native[(100*256+128)*4+2]==255 &&
          !memcmp(ram_before,g_ram,sizeof(ram_before)) && !memcmp(sram_before,sram,sizeof(sram_before)) &&
          !memcmp(&cpu_before,&g_cpu,sizeof(g_cpu)),
          "cross-section reconstruction preserves native pixels, PPU, camera, activation and guest RAM");
    }
    const unsigned cross_streams[]={0x4cea17,0x510000+0x42*8,0x510000+0x58*8,0x510000+0x72*8,0x510000+0xbe*8};
    for(unsigned room=0;room<5;++room) {
      fixture_long(sram+0x2600,cross_streams[room]);fixture_word(g_ram+0x148,cross_modes[room]);
      for(unsigned line=1;line<=224;++line){ppu_runLine(g_ppu,line);YiWs11RenderLine(g_ppu,line);}
      TEST_ASSERT(YiWs11Active() && wide[(100*YI_WS_WIDTH)*4+2]==255 && wide[(100*YI_WS_WIDTH+YI_WS_WIDTH-1)*4+2]==255,
          "all five cross-section room profile classes use the same derived cover policy");
    }
    fixture_long(sram+0x2600,0x158583);fixture_word(g_ram+0x148,5);
    fixture_word(g_ram+0x140,0x0b);
    for(unsigned q=0;q<4;++q)fixture_word(g_snes->cart->rom+0xc33f2+q*2,0x017f);
    for(unsigned i=0x6800;i<0x7000;++i)g_ppu->vram[i]=0x017f;
    for(unsigned line=1;line<=224;++line){ppu_runLine(g_ppu,line);YiWs11RenderLine(g_ppu,line);}
    TEST_ASSERT(wide[(100*YI_WS_WIDTH)*4+2]==255 && wide[(100*YI_WS_WIDTH+YI_WS_WIDTH-1)*4+2]==255,
        "other BG3 tilesets retain their own original presentation policy");
    fixture_word(g_ram+0x140,header_source);fixture_word(g_ram+0x39,256);
    memcpy(g_snes->cart->rom+0xc33f2,quad_source,8);*g_ppu=cover_source;
    /* Pause does NOT change the gameplay mode. Exercise real pause-state
     * bytes plus a non-black backdrop, so disabling layers alone is insufficient. */
    static Ppu pause_source;pause_source=*g_ppu;
    g_ppu->bgmode=0x09;g_ppu->bgXsc[2]=0x34;g_ppu->bgTileAdr=0x200;
    g_ppu->screenEnabled[0]=4;g_ppu->screenEnabled[1]=0;g_ppu->cgram[0]=0x7fff;g_ppu->cgram[2]=0x001f;
    for(unsigned y=0;y<8;++y)g_ppu->vram[0x2008+y]=0xff00;
    for(unsigned i=0x3400;i<0x3800;++i)g_ppu->vram[i]=0x2001;
    const unsigned pause_states[]={1,0x13,0x14};
    for(unsigned state=0;state<3;++state) {
      g_ram[0xb11]=pause_states[state];g_ram[0x118]=0x13;
      bool native_ui=true;
      for(unsigned line=1;line<=224;++line) {
        ppu_runLine(g_ppu,line);before=*g_ppu;YiWs11RenderLine(g_ppu,line);
        if(memcmp(&before,g_ppu,sizeof(before)) || memcmp(native+(line-1)*256*4,wide+((line-1)*YI_WS_WIDTH+YI_WS_EXTRA)*4,256*4))native_ui=false;
      }
      bool margins_black=true;
      for(unsigned y=0;y<224;++y)for(unsigned x=0;x<YI_WS_WIDTH;++x)if(x<YI_WS_EXTRA || x>=YI_WS_EXTRA+256) {
        const uint8_t *pixel=wide+(y*YI_WS_WIDTH+x)*4;if(pixel[0]||pixel[1]||pixel[2])margins_black=false;
      }
      TEST_ASSERT(YiWs11Active() && margins_black && native_ui,"all pause phases retain original menu without side repetition or colored backdrop bleed");
    }
    g_ram[0xb11]=0;*g_ppu=pause_source;
    /* Pause state precedes the visible menu and can outlive it on close.
     * Until BG1 is hidden, the live landscape and side sprites must remain. */
    const unsigned landscape_pause_states[]={1,0x14};
    for(unsigned state=0;state<2;++state) {
      g_ram[0xb11]=landscape_pause_states[state];
      for(unsigned line=1;line<=224;++line) {ppu_runLine(g_ppu,line);YiWs11RenderLine(g_ppu,line);}
      TEST_ASSERT(YiWs11Active() && wide[(39*YI_WS_WIDTH+YI_WS_EXTRA-24)*4+2]==255,
          "opening/closing pause flag does not blank visible BG1 scenery or side sprites");
    }
    g_ram[0xb11]=0;

    /* The neighboring regional status address must not trigger dialogue clipping. */
    fixture_word(g_ram+0xd0f,9);
    for(unsigned line=1;line<=224;++line) { ppu_runLine(g_ppu,line); YiWs11RenderLine(g_ppu,line); }
    TEST_ASSERT(wide[(39*YI_WS_WIDTH+YI_WS_EXTRA-24)*4+2] == 255,
      "other region's status address does not hide legitimate side sprites");
    fixture_word(g_ram+0xd0f,0);
    /* Dialog text never changes the output width. Native center stays authoritative. */
    const unsigned messages[] = {1,3,9,13};
    for (unsigned m = 0; m < 4; ++m) {
      fixture_word(g_ram + 0xd11, messages[m]);
      for (unsigned line = 1; line <= 224; ++line) {
        ppu_runLine(g_ppu, line); before = *g_ppu; YiWs11RenderLine(g_ppu, line);
        TEST_ASSERT(!memcmp(&before, g_ppu, sizeof(before)), "dialog pass never mutates native PPU");
      }
      TEST_ASSERT(YiWs11Active(), "observed dialog phases remain wide");
      bool dialog_center = true;
      for (unsigned y = 0; y < 224; ++y)
        if (memcmp(native + y*256*4, wide + (y*YI_WS_WIDTH+YI_WS_EXTRA)*4, 256*4)) dialog_center = false;
      TEST_ASSERT(dialog_center, "original dialog picture is byte-identical");
    }
    fixture_word(g_ram + 0xd11, 0);
    for (unsigned mode = 0x0f; mode <= 0x12; ++mode) {
      g_ram[0x118] = mode;
      for (unsigned line = 1; line <= 224; ++line) { ppu_runLine(g_ppu, line); YiWs11RenderLine(g_ppu, line); }
      TEST_ASSERT(YiWs11Active(), "valid PAL Rev2 pipe/fade scene stays wide");
    }
    /* Invalid world data remains rejected while visible, even during transitions. */
    fixture_word(g_ram + 0x148, 3);
    for (unsigned line = 1; line <= 224; ++line) { ppu_runLine(g_ppu, line); YiWs11RenderLine(g_ppu, line); }
    TEST_ASSERT(!YiWs11Active(), "visible unsupported transition still rejects");
    TEST_ASSERT(YiWs11PresentationWide(),"unsupported transition preserves widescreen geometry without synthesizing terrain");
    g_ppu->inidisp = 0x8f;
    for (unsigned line = 1; line <= 224; ++line) { ppu_runLine(g_ppu, line); YiWs11RenderLine(g_ppu, line); }
    TEST_ASSERT(YiWs11Active(), "fully black pipe loading field keeps wide canvas");
    bool black = true;
    for (unsigned i = 0; i < YI_WS_WIDTH*224; ++i) if(wide[i*4] || wide[i*4+1] || wide[i*4+2]) black=false;
    TEST_ASSERT(black, "black transition never reuses stale landscape");
    g_ppu->inidisp = 15; g_ram[0x118] = 0x13; fixture_word(g_ram + 0x148, 5);
    fixture_long(sram + 0x2600, 0x15855e);
    fixture_word(g_ram + 0x21a, 0x80); fixture_word(g_ram + 0x148, 0);
    g_ram[0x118] = 0x3d; g_ppu->bgXsc[0] = 0x68;
    for (unsigned i = 0x6800; i < 0x6c00; ++i) g_ppu->vram[i] = 1;
    for (unsigned phase = 0; phase < 2; ++phase) {
      fixture_word(g_ram + 0xd11, phase ? 9 : 0);
      memcpy(ram_before, g_ram, sizeof(ram_before)); memcpy(sram_before, sram, sizeof(sram_before)); cpu_before = g_cpu;
      bool intro_ppu = true;
      for (unsigned line = 1; line <= 224; ++line) {
        ppu_runLine(g_ppu, line); before=*g_ppu; YiWs11RenderLine(g_ppu, line);
        if(memcmp(&before,g_ppu,sizeof(before))) intro_ppu=false;
      }
      TEST_ASSERT(YiWs11Active() && wide[1] == 255 && wide[(YI_WS_WIDTH-1)*4+1] == 255,
        "welcome scene renders both complete periodic background margins, including dialogue");
      bool intro_center = true;
      for(unsigned y=0;y<224;++y) if(memcmp(native+y*256*4,wide+(y*YI_WS_WIDTH+YI_WS_EXTRA)*4,256*4)) intro_center=false;
      TEST_ASSERT(intro_center && intro_ppu && !memcmp(ram_before,g_ram,sizeof(ram_before)) &&
        !memcmp(sram_before,sram,sizeof(sram_before)) && !memcmp(&cpu_before,&g_cpu,sizeof(g_cpu)),
        "tutorial center, camera, CPU, object activation and native PPU stay unchanged");
    }
    g_ram[0x118]=0x0f; fixture_word(g_ram+0x21a,11);
    for(unsigned line=1;line<=224;++line) { ppu_runLine(g_ppu,line); YiWs11RenderLine(g_ppu,line); }
    TEST_ASSERT(YiWs11Active(), "welcome-to-playable-tutorial fade retains genuine wide tiled background");
    g_ram[0x118] = 0x45; YiWs11RenderLine(g_ppu,1);
    TEST_ASSERT(!YiWs11Active(), "tutorial background exception never enables overworld");
    fixture_long(sram+0x2600,0x158560); fixture_word(g_ram+0x21a,11);
    fixture_word(g_ram+0x148,5); g_ram[0x118]=0x13; g_ppu->bgXsc[0]=0x69;
    for(unsigned i=0x6800;i<0x6c00;++i) g_ppu->vram[i]=0;
    YiWs11RenderLine(g_ppu,1);
    TEST_ASSERT(YiWs11Active(), "playable tutorial uses checked ordinary terrain, including its first dialog");
    fixture_word(g_ram+0xd11,0); fixture_word(g_ram+0x21a,0); fixture_long(sram+0x2600,0x158583);
    /* Tutorial exit overlay/fade retains current native center and live scenery. */
    fixture_long(sram+0x2600,0x158560); fixture_word(g_ram+0x21a,11);
    const unsigned endings[]={0x13,0x22,0x23};
    for(unsigned k=0;k<3;++k) {
      g_ram[0x118]=endings[k];g_ppu->screenEnabled[0]=0x14;g_ppu->screenEnabled[1]=0;
      for(unsigned line=1;line<=224;++line) { ppu_runLine(g_ppu,line);YiWs11RenderLine(g_ppu,line); }
      TEST_ASSERT(YiWs11Active(),"tutorial exit/hidden-landscape overlay stays in true wide presentation");
      bool original=true;
      for(unsigned y=0;y<224;++y) if(memcmp(native+y*256*4,wide+(y*YI_WS_WIDTH+YI_WS_EXTRA)*4,256*4)) original=false;
      TEST_ASSERT(original,"tutorial end overlay preserves all original pixels");
    }
    g_ram[0x118]=0x13;g_ppu->screenEnabled[0]=0x11;
    fixture_long(sram+0x2600,0x158583); fixture_word(g_ram+0x21a,0);
    /* All known forms share the ordinary terrain path; no guest state writes. */
    for(unsigned form=0;form<=0x12;form+=2) for(unsigned morph=0;morph<=6;++morph) {
      fixture_word(sram+0xae,form); fixture_word(sram+0x14e,morph);
      YiWs11RenderLine(g_ppu,1);
      TEST_ASSERT(YiWs11Active(), "known forms and morph phases keep ordinary landscape wide");
    }
    fixture_word(sram+0xae,0x13); YiWs11RenderLine(g_ppu,1);
    TEST_ASSERT(!YiWs11Active(), "unknown player form remains protected");
    fixture_word(sram+0xae,0); fixture_word(sram+0x14e,0);
    for(unsigned mode=0x14;mode<=0x16;++mode) {
      g_ram[0x118]=mode; YiWs11RenderLine(g_ppu,1);
      TEST_ASSERT(YiWs11Active(), "native level victory/death/fade scenes remain wide");
    }
    g_ram[0x118]=0x13; fixture_word(g_ram+0xd11,9);
    g_ppu->windowsel=0x220222; g_ppu->screenWindowed[0]=g_ppu->screenWindowed[1]=0x1f;
    g_ppu->window1left=0;g_ppu->window1right=255;g_ppu->cgwsel=0x40;
    for(unsigned line=1;line<=224;++line) { ppu_runLine(g_ppu,line); YiWs11RenderLine(g_ppu,line); }
    TEST_ASSERT(YiWs11Active() && wide[1]>0 && wide[(YI_WS_EXTRA+256)*4+1]>0,
      "dialog masks never create a black bar at either added viewport edge");
    bool dialog_native=true;
    for(unsigned y=0;y<224;++y) if(memcmp(native+y*256*4,wide+(y*YI_WS_WIDTH+YI_WS_EXTRA)*4,256*4)) dialog_native=false;
    TEST_ASSERT(dialog_native,"dialog mask correction preserves entire original center");
    g_ppu->windowsel=0;g_ppu->screenWindowed[0]=g_ppu->screenWindowed[1]=0;g_ppu->cgwsel=0;
    fixture_word(g_ram+0xd11,0);
    /* A host picture-memory view must be respected on visible lines.
     * Native blank rows must remain blank when that view changes mid-field. */
    static uint16_t view_vram[0x8000];
    memcpy(view_vram, g_ppu->vram, sizeof(view_vram));
    g_ppu->cgram[2] = 0x7c00;
    for (unsigned row = 0; row < 8; ++row) view_vram[16 + row] = 0xff00;
    g_ppu->renderVram = view_vram;
    bool view_native_preserved = true;
    for (unsigned line = 1; line <= 224; ++line) {
      g_ppu->inidisp = line <= 8 || line >= 217 ? 0x8f : 15;
      ppu_runLine(g_ppu, line); before = *g_ppu; YiWs11RenderLine(g_ppu, line);
      if (memcmp(&before, g_ppu, sizeof(before))) view_native_preserved = false;
      if (line == 9) for (unsigned row = 0; row < 8; ++row) view_vram[16 + row] = 0x00ff;
    }
    TEST_ASSERT(YiWs11Active() && view_native_preserved,
                "external picture memory is respected without altering native PPU bindings");
    TEST_ASSERT(!YiWs11VerticalActive() && wide[(YI_WS_EXTRA+50)*4] == 0 &&
        wide[(223*YI_WS_WIDTH+YI_WS_EXTRA+50)*4+1] == 0,
        "native blank rows remain untouched across picture-memory updates");
    g_ppu->renderVram = NULL; g_ppu->inidisp = 15;
    /* Cross a tile/page boundary in both directions. The native PPU supplies
     * the oracle pixels; margin VRAM is blank again before each frame. */
    const unsigned scrolls[] = {0x1ff, 0x101};
    for (unsigned k = 0; k < 2; ++k) {
        unsigned scroll = scrolls[k];
        fixture_word(g_ram + 0x39, scroll); g_ppu->hScroll[0] = scroll;
        memset(g_ppu->vram + 0x6800, 0, 0x800 * 2);
        for (unsigned ty = 0; ty < 32; ++ty)
            for (unsigned tx = scroll / 8; tx <= (scroll + 255) / 8; ++tx) {
                unsigned addr = 0x6800 + ty * 32 + (tx & 31) + ((tx & 32) ? 0x400 : 0);
                g_ppu->vram[addr] = 1;
            }
        for (unsigned line = 1; line <= 224; ++line) {
            ppu_runLine(g_ppu, line); YiWs11RenderLine(g_ppu, line);
        }
        TEST_ASSERT(YiWs11Active() && wide[1] == 255 && wide[(YI_WS_WIDTH - 1) * 4 + 1] == 255,
                    "first-visit terrain survives page boundary and direction reversal");
    }
    /* A full native color window clips added columns to black without
     * opt-in legacy window expansion. Reproduce that real rendering class. */
    g_ppu->windowsel = 2u << 20; g_ppu->window1left = 0; g_ppu->window1right = 255;
    g_ppu->cgwsel = 0x40;
    bool masks_preserved = true;
    for (unsigned line = 1; line <= 224; ++line) {
        ppu_runLine(g_ppu, line); before = *g_ppu;
        YiWs11RenderLine(g_ppu, line);
        if (memcmp(&before, g_ppu, sizeof(before))) masks_preserved = false;
    }
    TEST_ASSERT(YiWs11Active() && wide[1] == 255 && wide[(YI_WS_WIDTH - 1) * 4 + 1] == 255,
                "full-screen color masks extend over both private side margins");
    TEST_ASSERT(masks_preserved, "native color/window state is unchanged");
    g_ppu->windowsel = 0; g_ppu->cgwsel = 0;
    /* Actual IRQ pattern: 8 black top rows, 208 original visible rows,
     * then 8 blank bottom rows. Offscreen world cells have different colors;
     * neither reconstruction nor an edge copy may replace native blanking. */
    for (unsigned q = 0; q < 4; ++q) {
        fixture_word(g_snes->cart->rom + 0xc33f2 + 8 + q * 2, q < 2 ? 3 : 1);
        fixture_word(g_snes->cart->rom + 0xc33f2 + 16 + q * 2, q < 2 ? 1 : 3);
        fixture_word(g_snes->cart->rom + 0xc33f2 + 24 + q * 2, 4);
    }
    for (unsigned x = 0; x < 16; ++x) {
        fixture_word(g_ram + 0x18000 + x * 2, 1);
        fixture_word(g_ram + 0x18000 + 13 * 32 + x * 2, 2);
        fixture_word(g_ram + 0x18000 + 14 * 32 + x * 2, 3);
    }
    /* Before-HDMA scanout includes world row 472 in the native picture. */
    for (unsigned tx = 0; tx < 32; ++tx) {
      g_ppu->vram[0x6800 + 27*32 + tx] = 3;
      g_ppu->vram[0x6c00 + 27*32 + tx] = 3;
    }
    g_ppu->cgram[2] = 0x7c00; g_ppu->cgram[3] = 0x03ff;
    for (unsigned r = 0; r < 8; ++r) { g_ppu->vram[48 + r] = 0xff00; g_ppu->vram[64 + r] = 0xffff; }
    memcpy(ram_before, g_ram, sizeof(ram_before)); memcpy(sram_before, sram, sizeof(sram_before));
    cpu_before = g_cpu;
    bool band_ppu_equal = true;
    for (unsigned line = 1; line <= 224; ++line) {
        g_ppu->inidisp = line <= 8 || line >= 217 ? 0x8f : 15;
        ppu_runLine(g_ppu, line); before = *g_ppu;
        YiWs11RenderLine(g_ppu, line);
        if (memcmp(&before, g_ppu, sizeof(before))) band_ppu_equal = false;
    }
    TEST_ASSERT(YiWs11Active() && !YiWs11VerticalActive(), "retain original 8 top and 8 bottom rows");
    bool visible_equal = true, top_blue = true, bottom_colors = true;
    for (unsigned y = 0; y < 224; ++y)
        if (memcmp(native + y * 256 * 4, wide + (y * YI_WS_WIDTH + YI_WS_EXTRA) * 4, 256 * 4)) visible_equal = false;
    for (unsigned y = 0; y < 7; ++y) {
        const uint8_t *px = wide + (y * YI_WS_WIDTH + YI_WS_EXTRA + 50) * 4;
        if (px[0] || px[1] || px[2]) top_blue = false;
    }
    for (unsigned y = 216; y < 223; ++y) {
        const uint8_t *px = wide + (y * YI_WS_WIDTH + YI_WS_EXTRA + 50) * 4;
        if (px[0] || px[1] || px[2]) bottom_colors = false;
    }
    const uint8_t *last = wide + (223 * YI_WS_WIDTH + YI_WS_EXTRA + 50) * 4;
    TEST_ASSERT(wide[(7*YI_WS_WIDTH+YI_WS_EXTRA+50)*4+1] == 0, "eighth top row retains original blanking");
    TEST_ASSERT(top_blue && bottom_colors && last[0] == 0 && last[1] == 0 && last[2] == 0,
                "native top and bottom blanking stay black");
    TEST_ASSERT(visible_equal, "all 224 original center rows remain byte-identical");
    bool lower_black = true;
    for (unsigned y=216;y<224;++y) for(unsigned x=0;x<YI_WS_WIDTH;++x) {
      const uint8_t *px=wide+(y*YI_WS_WIDTH+x)*4;
      if(px[0]||px[1]||px[2]) lower_black=false;
    }
    TEST_ASSERT(lower_black,"native lower blanking covers the entire 16:9 width");
    bool upper_black = true;
    for (unsigned y=0;y<8;++y) for(unsigned x=0;x<YI_WS_WIDTH;++x) {
      const uint8_t *px=wide+(y*YI_WS_WIDTH+x)*4;
      if(px[0]||px[1]||px[2]) upper_black=false;
    }
    TEST_ASSERT(upper_black,"native upper blanking covers the entire 16:9 width");
    TEST_ASSERT(band_ppu_equal && !memcmp(&cpu_before, &g_cpu, sizeof(g_cpu)) &&
                !memcmp(ram_before, g_ram, sizeof(ram_before)) && !memcmp(sram_before, sram, sizeof(sram_before)),
                "side-only pass preserves native PPU, CPU, camera, activation and GSU RAM");
    /* Next frame fades: cached full-brightness data must never leak through. */
    for (unsigned line = 1; line <= 224; ++line) {
        g_ppu->inidisp = line <= 8 || line >= 217 ? 0x8f : 8;
        ppu_runLine(g_ppu, line); YiWs11RenderLine(g_ppu, line);
    }
    TEST_ASSERT(!YiWs11VerticalActive(), "fades retain native borders");
    TEST_ASSERT(wide[(YI_WS_EXTRA + 50) * 4] == 0 &&
                wide[(223 * YI_WS_WIDTH + YI_WS_EXTRA + 50) * 4 + 1] == 0,
                "blank borders never reuse prior bright pixels");
    /* Native borders must survive fades, iris windows, messages and
     * camera events at every brightness. */
    for (unsigned kind = 0; kind < 4; ++kind) {
      g_ram[0x118] = kind == 0 ? 0x11 : kind == 1 ? 0x0f : 0x13;
      fixture_word(g_ram+0xd11,kind == 2 ? 9 : 0);
      if (kind == 3) {
        g_ram[0x1e2c] = 4; fixture_word(sram+0x1364,0xba);
        fixture_word(sram+0xf04,2); fixture_word(sram+0x1e2a,1);
      }
      g_ppu->windowsel = kind == 0 ? 0x333333 : 0;
      g_ppu->window1left = g_ppu->window2left = 0;
      g_ppu->window1right = g_ppu->window2right = 255;
      for (unsigned brightness = 1; brightness <= 15; ++brightness) {
        for (unsigned line=1;line<=224;++line) {
          g_ppu->inidisp = line<=8 || line>=217 ? 0x8f : brightness;
          ppu_runLine(g_ppu,line); YiWs11RenderLine(g_ppu,line);
        }
        TEST_ASSERT(YiWs11Active() && !YiWs11VerticalActive(), "native borders survive iris, fade, dialogue and stairs");
      }
    }
    fixture_word(g_ram+0xd11,0); fixture_word(sram+0x1e2a,0);
    g_ram[0x118]=0x13; g_ppu->windowsel=0;
    /* Shift both IRQ edges by a line. Native content must not be overwritten. */
    for(unsigned line=1;line<=224;++line) {
      g_ppu->inidisp = line<=9 || line>=216 ? 0x8f : 8;
      ppu_runLine(g_ppu,line); YiWs11RenderLine(g_ppu,line);
    }
    TEST_ASSERT(YiWs11Active() && !YiWs11VerticalActive(), "actual 9/9 native blank borders are preserved");
    bool shifted_center=true;
    for(unsigned y=9;y<215;++y) if(memcmp(native+y*256*4,wide+(y*YI_WS_WIDTH+YI_WS_EXTRA)*4,256*4)) shifted_center=false;
    TEST_ASSERT(shifted_center,"all visible center rows survive shifted IRQ edges");
    for (unsigned i = 0x6800; i < 0x7000; ++i) g_ppu->vram[i] = 1;
    /* Restore ordinary fixture terrain for following contract cases. */
    memset(g_ram + 0x18000, 0, 0x8000); g_ppu->inidisp = 15;
    /* Camera-event owners are byte-indexed even when the next byte is stale. */
    g_ram[0x1e2c] = 4; g_ram[0x1e2d] = 0xee;
    sram[0x1e2c] = 0xff; /* deliberately incorrect mirror from the old implementation */
    fixture_word(sram + 0xf04, 2);
    for (unsigned kind = 0; kind < 3; ++kind) {
      fixture_word(sram + 0x1364, kind == 0 ? 0xba : kind == 1 ? 0xc7 : 0xc8);
      fixture_word(sram + 0x1e2a, 1);
      for (unsigned line = 1; line <= 224; ++line) {
        g_ppu->inidisp = line <= 8 || line >= 217 ? 0x8f : 15;
        ppu_runLine(g_ppu, line); YiWs11RenderLine(g_ppu, line);
      }
      TEST_ASSERT(YiWs11Active(), "stairs / sunflower camera owner admits complete wide field");
      TEST_ASSERT(!YiWs11VerticalActive(), "plant event retains native top and bottom borders");
    }
    g_ppu->inidisp = 15; ppu_runLine(g_ppu, 1);
    /* Both owner kinds observed during the user's shot-triggered fallback. */
    const unsigned shot_owners[]={0xb8,0xc1};
    for(unsigned k=0;k<2;++k) {
      fixture_word(sram+0x1364,shot_owners[k]);
      fixture_word(sram+0x1e2a,1);
      YiWs11RenderLine(g_ppu,1);
      TEST_ASSERT(YiWs11Active(),"shot-triggered active camera owner keeps widescreen");
    }
    fixture_word(sram+0x1364,0xc8);
    fixture_word(sram + 0x1e2a, 0xfff8); YiWs11RenderLine(g_ppu, 1);
    TEST_ASSERT(YiWs11Active(), "plant return-to-player phase stays wide");
    fixture_word(sram + 0x1364, 0xbb); YiWs11RenderLine(g_ppu, 1);
    TEST_ASSERT(YiWs11Active(), "validated active camera owners share landscape presentation");
    fixture_word(sram + 0x1364, 0xba); fixture_word(sram + 0xf04, 0);
    fixture_word(sram + 0x1e2a, 1); YiWs11RenderLine(g_ppu, 1);
    TEST_ASSERT(!YiWs11Active(), "inactive positive event owner stays excluded");
    fixture_word(sram + 0xf04, 2); g_ram[0x1e2c] = 5; YiWs11RenderLine(g_ppu, 1);
    TEST_ASSERT(!YiWs11Active(), "unaligned camera owner slot is rejected");
    g_ram[0x1e2c] = 0xff; YiWs11RenderLine(g_ppu, 1);
    TEST_ASSERT(!YiWs11Active(), "out-of-range camera owner slot is rejected");
    fixture_word(sram + 0x1e2a, 0);
    /* Complete fields in every newly audited room, with PAL Rev2 mode at $0148.
     * Nonzero sprite palettes at $0146 must no longer close the gate. */
    const struct { unsigned stage, pointer, mode; } room_cases[] = {
      {0, 0x158642, 6}, {1, 0x4ce976, 0}, {1, 0x4cea17, 5}, {0, 0x158583, 5}
    };
    for (unsigned k = 0; k < 4; ++k) {
      unsigned pointer = room_cases[k].pointer;
      fixture_word(g_ram + 0x21a, room_cases[k].stage);
      fixture_word(g_ram + 0x148, room_cases[k].mode);
      fixture_word(g_ram + 0x146, 10);
      sram[0x2600] = pointer; sram[0x2601] = pointer >> 8; sram[0x2602] = pointer >> 16;
      memcpy(ram_before, g_ram, sizeof(ram_before)); memcpy(sram_before, sram, sizeof(sram_before));
      cpu_before = g_cpu;
      bool preserved = true;
      for (unsigned line = 1; line <= 224; ++line) {
        g_ppu->inidisp = line <= 8 || line >= 217 ? 0x8f : 15;
        ppu_runLine(g_ppu, line); before = *g_ppu;
        YiWs11RenderLine(g_ppu, line);
        if (memcmp(&before, g_ppu, sizeof(before))) preserved = false;
      }
      TEST_ASSERT(YiWs11Active(), "audited room accepts its exact PAL Rev2 header despite nonzero sprite palette");
      TEST_ASSERT(!YiWs11VerticalActive(), "all audited room profiles retain native raster borders");
      for (unsigned y = 0; y < 224; ++y)
        if (memcmp(native + y * 256 * 4, wide + (y * YI_WS_WIDTH + YI_WS_EXTRA) * 4, 256 * 4)) preserved = false;
      TEST_ASSERT(preserved && !memcmp(&cpu_before, &g_cpu, sizeof(g_cpu)) &&
          !memcmp(ram_before, g_ram, sizeof(ram_before)) && !memcmp(sram_before, sram, sizeof(sram_before)),
          "room transition renderer preserves visible center, native PPU, camera and activation state");
      g_ram[0x148] ^= 1; g_ppu->inidisp = 15; ppu_runLine(g_ppu, 1); YiWs11RenderLine(g_ppu, 1);
      TEST_ASSERT(!YiWs11Active(), "room header mismatch fails closed");
    }
    fixture_word(g_ram + 0x148, 5); fixture_word(g_ram + 0x146, 0);
    /* Tile-at-a-time decoding must retain alternating world tiles at all
     * eight sub-tile scroll phases, including both partial edge tiles. */
    g_ppu->cgram[2] = 0x7c00;
    for (unsigned row = 0; row < 8; ++row) g_ppu->vram[3*16 + row] = 0xff00;
    for (unsigned q = 0; q < 4; ++q) fixture_word(g_snes->cart->rom + 0xc33f2 + q*2, q & 1 ? 3 : 1);
    for (unsigned ty = 0; ty < 32; ++ty)
      for (unsigned tx = 0; tx < 64; ++tx)
        g_ppu->vram[0x6800 + ty*32 + (tx & 31) + (tx >= 32 ? 0x400 : 0)] = tx & 1 ? 3 : 1;
    for (unsigned phase = 0; phase < 8; ++phase) {
      unsigned scroll = 0x100 + phase;
      fixture_word(g_ram + 0x39, scroll); g_ppu->hScroll[0] = scroll;
      for (unsigned line = 1; line <= 224; ++line) {
        g_ppu->inidisp = 15; ppu_runLine(g_ppu, line); YiWs11RenderLine(g_ppu, line);
      }
      bool correct = YiWs11Active();
      for (int x = -YI_WS_EXTRA; x < 256 + YI_WS_EXTRA; ++x) {
        const uint8_t *pixel = wide + (100*YI_WS_WIDTH + YI_WS_EXTRA + x)*4;
        bool blue = (scroll + x) & 8;
        if (pixel[0] != (blue ? 255 : 0) || pixel[1] != (blue ? 0 : 255)) correct = false;
      }
      TEST_ASSERT(correct, "tile-span optimization preserves alternating terrain at every pixel phase");
    }
    for (unsigned q = 0; q < 4; ++q) fixture_word(g_snes->cart->rom + 0xc33f2 + q*2, 1);
    for (unsigned i = 0x6800; i < 0x7000; ++i) g_ppu->vram[i] = 1;
    fixture_word(g_ram + 0x39, 0x101); g_ppu->hScroll[0] = 0x101;
    g_ppu->inidisp = 15; ppu_runLine(g_ppu, 1);
    /* Unresolvable source cells must close the experiment instead of
     * showing stale wrapped VRAM as if it were real landscape. */
    fixture_word(g_ram + 0x18000 + ((0x101 - YI_WS_EXTRA) & 0xf0) / 8, 0xff00);
    ppu_runLine(g_ppu, 1);
    YiWs11RenderLine(g_ppu, 1);
    TEST_ASSERT(!YiWs11Active(), "invalid side tile forces native fallback");
    fixture_word(g_ram + 0x18000 + ((0x101 - YI_WS_EXTRA) & 0xf0) / 8, 0);
    /* Regression: the first real PAL Rev2 trace used 13/0, while the initial
     * prototype incorrectly required the US mode ID and a guessed header. */
    g_ram[0x118] = 0x49; YiWs11RenderLine(g_ppu, 1);
    TEST_ASSERT(!YiWs11Active(), "non-level PAL scene remains excluded");
    g_ram[0x118] = 0x13; g_ram[0x148] = 2; YiWs11RenderLine(g_ppu, 1);
    TEST_ASSERT(!YiWs11Active(), "wrong level mode remains excluded");
    g_ram[0x148] = 5;
    g_ppu->bgmode = 0x09; g_ppu->screenEnabled[0] = 0x14; g_ppu->screenEnabled[1] = 0;
    for (unsigned line = 1; line <= 224; ++line) {
      ppu_runLine(g_ppu, line); YiWs11RenderLine(g_ppu, line);
    }
    TEST_ASSERT(YiWs11Active() && !YiWs11VerticalActive(),
                "ordinary pause/score overlay keeps its original center in a wide landscape");
    g_ppu->bgmode = 0x69; g_ppu->screenEnabled[0] = 0x15; g_ppu->screenEnabled[1] = 2;
    for (unsigned line = 1; line <= 224; ++line) {
      ppu_runLine(g_ppu, line); YiWs11RenderLine(g_ppu, line);
    }
    TEST_ASSERT(YiWs11Active(), "return from landscape-hidden menu restores wide next field");
    /* Hidden BG1 in main screen alone is legal if sub screen still uses it. */
    g_ppu->screenEnabled[0] = 0x14; g_ppu->screenEnabled[1] = 1;
    ppu_runLine(g_ppu, 1); YiWs11RenderLine(g_ppu, 1);
    TEST_ASSERT(YiWs11Active(), "BG1 used by color-math sub screen is not mistaken for hidden terrain");
    g_ppu->screenEnabled[0] = 0x11; g_ppu->screenEnabled[1] = 0;
    for (unsigned i = 0; i < 0xde; ++i) {
      const uint8_t *entry = g_snes->cart->rom + 0x7fa24 + i*6;
      unsigned obj = entry[0] | (unsigned)entry[1] << 8 | (unsigned)entry[2] << 16;
      unsigned mode = g_snes->cart->rom[obj - 0x400000 + 6] >> 3;
      bool normal = mode == 0 || mode == 1 || mode == 2 || mode == 5 || mode == 6 ||
          mode == 7 || mode == 8 || mode == 11 || mode == 12 || mode == 15;
      memcpy(sram + 0x2600, entry + 3, 3);
      fixture_word(g_ram + 0x218, i % 6); fixture_word(g_ram + 0x21a, i % 72);
      fixture_word(g_ram + 0x148, mode);
      YiWs11RenderLine(g_ppu, 1);
      TEST_ASSERT(YiWs11Active() == normal, "all ROM-derived room slots apply normal/special layout policy across six worlds");
      g_ram[0x148] ^= 1; YiWs11RenderLine(g_ppu, 1);
      TEST_ASSERT(!YiWs11Active(), "ROM-derived room rejects a stale/mismatched live header");
    }
    fixture_word(g_ram + 0x148, 5); fixture_long(sram + 0x2600, 0x158583);
    fixture_word(g_ram + 0x218, 5); fixture_word(g_ram + 0x21a, 71);
    bool general_preserved = true;
    for (unsigned line = 1; line <= 224; ++line) {
      ppu_runLine(g_ppu, line); before = *g_ppu;
      YiWs11RenderLine(g_ppu, line);
      if (memcmp(&before, g_ppu, sizeof(before))) general_preserved = false;
    }
    TEST_ASSERT(general_preserved, "general world policy does not mutate native PPU");
    TEST_ASSERT(YiWs11Active(), "normal complete field supports final world and global stage range");
    fixture_word(g_ram + 0x218, 6); YiWs11RenderLine(g_ppu, 1);
    TEST_ASSERT(!YiWs11Active(), "invalid world is excluded");
    fixture_word(g_ram + 0x218, 0); fixture_word(g_ram + 0x21a, 72); YiWs11RenderLine(g_ppu, 1);
    TEST_ASSERT(!YiWs11Active(), "invalid stage is excluded");
    g_ram[0x21a] = 0; sram[0x2600] ^= 1; YiWs11RenderLine(g_ppu, 1);
    TEST_ASSERT(!YiWs11Active(), "unregistered sprite stream remains excluded");
    sram[0x2600] ^= 1; g_ram[0xd11] = 1; YiWs11RenderLine(g_ppu, 1);
    TEST_ASSERT(YiWs11Active(), "message overlays keep checked landscape wide");
    g_ram[0xd11] = 0; YiWs11Toggle(); YiWs11RenderLine(g_ppu, 1);
    TEST_ASSERT(!YiWs11Active(), "F8 native comparison switch");
    YiWs11SetEnabled(true);
    g_ram[0x118] = 0x49;
    YiWs11RenderLine(g_ppu, 1);
    TEST_ASSERT(!YiWs11Active(), "PAL retry-setup mode remains excluded");
    g_ram[0x118] = 0x13; g_ram[0x148] = 0; g_ram[0x146] = 5;
    YiWs11RenderLine(g_ppu, 1);
    TEST_ASSERT(!YiWs11Active(), "PAL Rev2 reads its own header mode address");
    g_ram[0x148] = 5;
    YiWs11SetEnabled(false);
    YiWs11RenderLine(g_ppu, 1);
    TEST_ASSERT(!YiWs11Active(), "checkbox off restores native view");
    /* Pipe exits and neighboring camera positions share native raster borders;
     * no per-room or camera-coordinate exceptions. */
    YiWs11SetEnabled(true); g_ram[0x118]=0x13;
    fixture_word(g_ram+0x148,5); fixture_word(g_ram+0x218,0); fixture_word(g_ram+0x21a,0);
    fixture_long(sram+0x2600,0x158583); fixture_word(sram+0x1e1a,0x2000);
    fixture_word(g_ram+0xd11,0); fixture_word(g_ram+0xc1e,0);
    fixture_word(sram+0x1e2a,0); fixture_word(sram+0xae,0); fixture_word(sram+0x14e,0);
    memset(g_ram+0x18000,0,0x8000); memset(sram+0xcaa,0,0x80);
    for(unsigned q=0;q<4;++q) fixture_word(g_snes->cart->rom+0xc33f2+q*2,1);
    for(unsigned i=0x6800;i<0x7000;++i) g_ppu->vram[i]=1;
    for(unsigned i=0;i<8;++i) g_ppu->vram[16+i]=0xff;
    g_ppu->cgram[1]=0x03e0;g_ppu->bgmode=0x69;g_ppu->bgXsc[0]=0x69;
    g_ppu->windowsel=0;g_ppu->screenEnabled[0]=0x11;g_ppu->screenEnabled[1]=0;
    const unsigned exits[]={1250,1264,1280};
    for(unsigned k=0;k<3;++k) {
      fixture_word(g_ram+0x39,exits[k]);fixture_word(g_ram+0x3b,1776);
      g_ppu->hScroll[0]=exits[k]&511;g_ppu->vScroll[0]=1776&255;
      for(unsigned line=1;line<=224;++line) {
        g_ppu->inidisp=line<=8||line>=217?0x8f:15;
        ppu_runLine(g_ppu,line);YiWs11RenderLine(g_ppu,line);
      }
      TEST_ASSERT(YiWs11Active() && !YiWs11VerticalActive(),"pipes retain native borders and the wide viewport");
      TEST_ASSERT(wide[(223*YI_WS_WIDTH+YI_WS_EXTRA+50)*4+1] == 0,
        "all camera positions retain black lower rows without coordinate exceptions");
    }
    const unsigned autoscrollers[]={1,18,0xffff};
    for(unsigned k=0;k<3;++k) {
      fixture_word(g_ram+0xc1e,autoscrollers[k]);
      memcpy(ram_before,g_ram,sizeof(ram_before));memcpy(sram_before,sram,sizeof(sram_before));cpu_before=g_cpu;
      bool autoscroll_ppu_equal=true;
      for(unsigned line=1;line<=224;++line) {
        g_ppu->inidisp=line<=8||line>=217?0x8f:15;
        ppu_runLine(g_ppu,line);before=*g_ppu;YiWs11RenderLine(g_ppu,line);
        if(memcmp(&before,g_ppu,sizeof(before)))autoscroll_ppu_equal=false;
      }
      TEST_ASSERT(YiWs11Active() && YiWs11PresentationWide(),"verified autoscrollers retain wide playable geometry");
      SuperFx *fx=g_snes->cart->superfx;
      TEST_ASSERT(fx->immediate_override && fx->immediate_override(NULL,fx,0x09893d,8,1)==(uint16_t)-35 &&
        fx->immediate_override(NULL,fx,0x098947,0xe8,2)==275,"native player bounds shift 43 pixels on each side");
      YiWs11SetEnabled(false);
      TEST_ASSERT(fx->immediate_override(NULL,fx,0x09893d,8,1)==8,"F8 native view restores native movement bounds");
      YiWs11SetEnabled(true);
      /* Re-establish the current visible field after changing the renderer switch. */
      for(unsigned line=1;line<=224;++line) {g_ppu->inidisp=line<=8||line>=217?0x8f:15;ppu_runLine(g_ppu,line);YiWs11RenderLine(g_ppu,line);}
      TEST_ASSERT(fx->immediate_override(NULL,fx,0x098955,0xf8,2)==0xf8 &&
        fx->immediate_override(NULL,fx,0x098947,0xe8,1)==0xe8,"warp and unrelated operands remain native");
      bool native_equal=true, margins_black=true;
      for(unsigned y=0;y<224;++y) {
        if(memcmp(native+y*256*4,wide+(y*YI_WS_WIDTH+YI_WS_EXTRA)*4,256*4))native_equal=false;
        for(unsigned x=0;x<YI_WS_EXTRA;++x) {
          const uint8_t *left=wide+(y*YI_WS_WIDTH+x)*4,*right=left+(YI_WS_EXTRA+256)*4;
          if(left[0]||left[1]||left[2]||right[0]||right[1]||right[2])margins_black=false;
        }
      }
      TEST_ASSERT(native_equal,"autoscroll preserves all native center pixels");
      TEST_ASSERT(autoscroll_ppu_equal && !memcmp(ram_before,g_ram,sizeof(ram_before))&&!memcmp(sram_before,sram,sizeof(sram_before))&&
        !memcmp(&cpu_before,&g_cpu,sizeof(g_cpu)),"autoscroll camera, movement bounds and activation state remain untouched");
    }
    /* Fully blank transition fields must not reactivate the wide fallback. */
    for(unsigned line=1;line<=224;++line) {g_ppu->inidisp=0x8f;ppu_runLine(g_ppu,line);YiWs11RenderLine(g_ppu,line);}
    TEST_ASSERT(YiWs11PresentationWide(),"verified autoscroll keeps wide geometry through blank transitions");
    fixture_word(g_ram+0xc1e,0);
    for(unsigned line=1;line<=224;++line) {g_ppu->inidisp=line<=8||line>=217?0x8f:15;ppu_runLine(g_ppu,line);YiWs11RenderLine(g_ppu,line);}
    TEST_ASSERT(YiWs11Active() && YiWs11PresentationWide(),"ordinary landscape resumes widescreen when autoscrolling ends");
    SuperFx *edge_fx=g_snes->cart->superfx;
    TEST_ASSERT(edge_fx->immediate_override(NULL,edge_fx,0x09893d,8,1)==8 &&
      edge_fx->immediate_override(NULL,edge_fx,0x098947,0xe8,2)==0xe8,
      "non-autoscroll gameplay retains original movement bounds");
    /* Mode-2 offset tables use the shared native sampler, including side tiles. */
    fixture_word(g_ram+0x218,0); fixture_word(g_ram+0x21a,6);
    fixture_word(g_ram+0x148,3); g_ram[0x118]=19;
    fixture_long(sram+0x2600,0x510000+3*8);
    fixture_word(g_ram+0x39,257); fixture_word(g_ram+0x3b,256);
    g_ppu->bgmode=2;g_ppu->screenEnabled[0]=1;g_ppu->screenEnabled[1]=0;
    g_ppu->bgXsc[0]=0x69;g_ppu->bgXsc[2]=0x74;
    g_ppu->hScroll[0]=257;g_ppu->vScroll[0]=256;
    g_ppu->hScroll[2]=g_ppu->vScroll[2]=0;
    g_ppu->windowsel=0;g_ppu->cgwsel=0;g_ppu->cgadsub=0;
    fixture_word(g_ram+3345,0);fixture_word(sram+0x1e2a,0);
    for(unsigned i=0x6800;i<0x7000;++i)g_ppu->vram[i]=1;
    for(unsigned col=0;col<32;++col) {
      g_ppu->vram[0x7400+col]=0x2100;
      g_ppu->vram[0x7420+col]=0x2100+((col&1)*8);
    }
    int lx,ly;PpuLegacyBgCoordinates(g_ppu,0,65,42,&lx,&ly);
    TEST_ASSERT(lx==322 && ly==306,"Mode-2 shared coordinates include per-column vertical shift");
    memcpy(ram_before,g_ram,sizeof(ram_before));memcpy(sram_before,sram,sizeof(sram_before));cpu_before=g_cpu;
    bool mode2_unchanged=true;
    for(unsigned line=1;line<=224;++line) {
      g_ppu->inidisp=15;ppu_runLine(g_ppu,line);before=*g_ppu;
      YiWs11RenderLine(g_ppu,line);
      if(memcmp(&before,g_ppu,sizeof(before)))mode2_unchanged=false;
    }
    TEST_ASSERT(YiWs11Active(),"Mode-2 terrain remains widescreen");
    bool mode2_center=true;
    for(unsigned y=0;y<224;++y)if(memcmp(native+y*256*4,wide+(y*YI_WS_WIDTH+YI_WS_EXTRA)*4,256*4))mode2_center=false;
    TEST_ASSERT(mode2_center&&mode2_unchanged&&!memcmp(ram_before,g_ram,sizeof(ram_before))&&
      !memcmp(sram_before,sram,sizeof(sram_before))&&!memcmp(&cpu_before,&g_cpu,sizeof(g_cpu)),
      "Mode-2 rendering preserves original picture and guest state");
    /* A moving column can cross the final Map16 row before the native
     * blank band. Hardware tilemap addressing wraps; scenery must not blink. */
    fixture_word(g_ram+0x3b,1804);g_ppu->vScroll[0]=780;
    for(unsigned phase=780;phase<1024;phase+=17) {
      for(unsigned col=0;col<32;++col)g_ppu->vram[0x7420+col]=0x2000|phase;
      for(unsigned line=1;line<=224;++line){g_ppu->inidisp=line<=8||line>=217?0x8f:15;ppu_runLine(g_ppu,line);YiWs11RenderLine(g_ppu,line);}
      TEST_ASSERT(YiWs11Active() && YiWs11PresentationWide() && wide[(100*YI_WS_WIDTH+YI_WS_WIDTH-1)*4+1]==255,
          "Mode-2 moving columns retain visible margins when their world address wraps");
      bool exact=true;
      for(unsigned y=0;y<224;++y)if(memcmp(native+y*256*4,wide+(y*YI_WS_WIDTH+YI_WS_EXTRA)*4,256*4))exact=false;
      TEST_ASSERT(exact,"wrapping offset columns preserve every native center pixel");
    }
    fixture_word(g_ram+0x3b,256);g_ppu->vScroll[0]=256;
    /* The ordinary terrain path also covers Burt; native BG2 body is never
     * repeated in side columns. No boss simulation or camera patch runs. */
    for(unsigned arena=0;arena<2;++arena) {
    fixture_word(g_ram+0x218,0);fixture_word(g_ram+0x21a,arena?7:3);
    fixture_word(g_ram+0x148,arena?12:5);
    fixture_long(sram+0x2600,0x510000+(arena?0x70:0x3d)*8);
    g_ppu->bgmode=1;g_ppu->bgXsc[1]=0x60;g_ppu->screenEnabled[0]=3;
    g_ppu->screenEnabled[1]=0;g_ppu->cgwsel=0;g_ppu->cgadsub=0;
    for(unsigned i=0x6000;i<0x6400;++i)g_ppu->vram[i]=0x2003;
    for(unsigned row=0;row<8;++row)g_ppu->vram[3*16+row]=0xff00;
    g_ppu->cgram[2]=0x7c00;
    for(unsigned phase=0;phase<3;++phase) {
      g_ram[0x118]=phase?51+phase:19;
      g_ppu->inidisp=15;fixture_word(g_ram+3345,phase?1:0);
      memcpy(ram_before,g_ram,sizeof(ram_before));memcpy(sram_before,sram,sizeof(sram_before));cpu_before=g_cpu;
      bool original=true,unchanged=true;
      for(unsigned line=1;line<=224;++line) {
        ppu_runLine(g_ppu,line);before=*g_ppu;YiWs11RenderLine(g_ppu,line);
        if(memcmp(&before,g_ppu,sizeof(before)))unchanged=false;
      }
      for(unsigned y=0;y<224;++y)if(memcmp(native+y*256*4,wide+(y*YI_WS_WIDTH+YI_WS_EXTRA)*4,256*4))original=false;
      TEST_ASSERT(YiWs11Active()&&wide[1]==255&&native[0]==255,
        "bitmap boss gameplay/dialog/fade retain terrain without duplicating BG2");
      TEST_ASSERT(original&&unchanged&&!memcmp(ram_before,g_ram,sizeof(ram_before))&&
        !memcmp(sram_before,sram,sizeof(sram_before))&&!memcmp(&cpu_before,&g_cpu,sizeof(g_cpu)),
        "bitmap boss path preserves original picture and all guest state");
    }
    }
    fixture_word(g_ram+0x148,5);
    g_ram[0x118]=19;fixture_long(sram+0x2600,1410435);fixture_word(g_ram+0x21a,0);
    fixture_word(g_ram+3345,0);
    /* Bonus board remains native; complete backdrop layers alone expand. */
    g_ppu->bgmode=0x41;g_ppu->bgXsc[0]=0x6a;g_ppu->bgXsc[1]=0x3a;g_ppu->bgXsc[2]=0x34;
    g_ppu->screenEnabled[0]=0x05;g_ppu->screenEnabled[1]=0x02;
    g_ppu->windowsel=0;g_ppu->cgwsel=0;g_ppu->cgadsub=0;
    for(unsigned i=0x6800;i<0x7000;++i)g_ppu->vram[i]=0x2003;
    for(unsigned i=0x3400;i<0x3c00;++i)g_ppu->vram[i]=1;
    const unsigned tiles[]={1,2,17,18};
    for(unsigned k=0;k<4;++k)for(unsigned row=0;row<8;++row)g_ppu->vram[tiles[k]*16+row]=0xff;
    for(unsigned k=0;k<4;++k)for(unsigned row=0;row<8;++row)g_ppu->vram[tiles[k]*8+row]=0xff; /* BG3 is 2bpp */
    for(unsigned row=0;row<8;++row)g_ppu->vram[3*16+row]=0xff00;
    g_ppu->cgram[1]=0x03e0;g_ppu->cgram[2]=0x7c00;g_ppu->inidisp=15;
    g_ram[0x118]=0x30;
    for(unsigned kind=0;kind<=10;kind+=2) {
      fixture_word(g_ram+0x212,kind);
      memcpy(ram_before,g_ram,sizeof(ram_before));memcpy(sram_before,sram,sizeof(sram_before));cpu_before=g_cpu;
      bool unchanged=true;
      for(unsigned line=1;line<=224;++line) { ppu_runLine(g_ppu,line);before=*g_ppu;YiWs11RenderLine(g_ppu,line);if(memcmp(&before,g_ppu,sizeof(before)))unchanged=false; }
      TEST_ASSERT(!YiWs11Active(),
        "all six bonus layouts remain native 4:3");
      bool original=true;
      for(unsigned y=0;y<224;++y) if(memcmp(native+y*256*4,wide+(y*YI_WS_WIDTH+YI_WS_EXTRA)*4,256*4))original=false;
      TEST_ASSERT(original && unchanged && !memcmp(ram_before,g_ram,sizeof(ram_before)) &&
        !memcmp(sram_before,sram,sizeof(sram_before)) && !memcmp(&cpu_before,&g_cpu,sizeof(g_cpu)),
        "bonus center, board, objects, RNG and input state stay unchanged");
    }
    /* Fixed UI family uses the same background policy across retry/results,
     * not one hand-authored patch for each individual screen. */
    const unsigned ui_modes[]={0x13,0x14,0x15,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x3b,0x3c,0x3d,0x40};
    for(unsigned k=0;k<sizeof(ui_modes)/sizeof(ui_modes[0]);++k) {
      g_ram[0x118]=ui_modes[k]+4;
      g_ppu->bgmode=1;g_ppu->bgXsc[0]=0x1c;g_ppu->bgXsc[1]=0x1c;g_ppu->bgXsc[2]=0x34;
      g_ppu->screenEnabled[0]=4;g_ppu->screenEnabled[1]=0;
      bool preserved=true;
      for(unsigned line=1;line<=224;++line) {ppu_runLine(g_ppu,line);before=*g_ppu;YiWs11RenderLine(g_ppu,line);if(memcmp(&before,g_ppu,sizeof(before)))preserved=false;}
      bool original=true;
      for(unsigned y=0;y<224;++y)if(memcmp(native+y*256*4,wide+(y*YI_WS_WIDTH+YI_WS_EXTRA)*4,256*4))original=false;
      TEST_ASSERT(YiWs11Active()&&original&&preserved,"centered UI family preserves native picture and PPU");
      if(ui_modes[k]>=0x20 && ui_modes[k]<=0x29) {
        bool black=true;
        for(unsigned y=0;y<224;++y)for(unsigned x=0;x<YI_WS_WIDTH;++x)if(x<YI_WS_EXTRA || x>=YI_WS_EXTRA+256) {
          const uint8_t *pixel=wide+(y*YI_WS_WIDTH+x)*4;
          if(pixel[0]||pixel[1]||pixel[2])black=false;
        }
        TEST_ASSERT(black,"all result/score phases keep text and backdrop out of both margins");
      }

    }
    g_ram[0x118]=14;g_ppu->bgmode=7;g_ppu->screenEnabled[0]=1;
    memset(g_ppu->m7matrix,0,sizeof(g_ppu->m7matrix));g_ppu->m7matrix[0]=g_ppu->m7matrix[3]=256;
    static uint16_t saved_vram[0x8000];memcpy(saved_vram,g_ppu->vram,sizeof(saved_vram));
    for(unsigned i=0;i<0x8000;++i)g_ppu->vram[i]=0x0100;
    ppu_runLine(g_ppu,1);before=*g_ppu;YiWs11RenderLine(g_ppu,1);
    TEST_ASSERT(YiWs11Active()&&wide[1]==255&&!memcmp(&before,g_ppu,sizeof(before))&&
      !memcmp(native,wide+YI_WS_EXTRA*4,256*4),"title Mode7 projection extends with unchanged native center and transform");
    memcpy(g_ppu->vram,saved_vram,sizeof(saved_vram));
    g_ram[0x118]=48;g_ppu->bgmode=0x41;g_ppu->bgXsc[0]=0x6a;g_ppu->bgXsc[1]=0x3a;
    /* Same unloaded-row symptom in a completely different scene and region:
     * BG3 wraps to a conflicting row inside the synthetic lower extension. */
    fixture_word(g_ram+0x212,0);g_ppu->screenEnabled[0]=4;g_ppu->screenEnabled[1]=0;
    g_ppu->vScroll[2]=4;
    for(unsigned i=0;i<32;++i)g_ppu->vram[0x3400+14*32+i]=64;
    for(unsigned tile=64;tile<=81;++tile)for(unsigned i=0;i<8;++i)g_ppu->vram[tile*8+i]=0xff00;
    for(unsigned line=1;line<=224;++line) {g_ppu->inidisp=line<=8||line>=217?0x8f:15;ppu_runLine(g_ppu,line);YiWs11RenderLine(g_ppu,line);}
    TEST_ASSERT(!YiWs11Active()&&wide[(223*YI_WS_WIDTH+YI_WS_EXTRA+50)*4+1]==0,
      "bonus fallback preserves native blank band regardless of extra background data");
    for(unsigned i=0;i<32;++i)g_ppu->vram[0x3400+14*32+i]=1;
    for(unsigned line=1;line<=224;++line) {g_ppu->inidisp=line<=8||line>=217?0x8f:15;ppu_runLine(g_ppu,line);YiWs11RenderLine(g_ppu,line);}
    TEST_ASSERT(!YiWs11Active()&&wide[(223*YI_WS_WIDTH+YI_WS_EXTRA+50)*4+1]==0,
      "lower blanking remains black even with valid continuation");
    g_ppu->inidisp=15;
    fixture_word(g_ram+0x212,12);YiWs11RenderLine(g_ppu,1);
    TEST_ASSERT(!YiWs11Active(),"unknown bonus type remains excluded");
    fixture_word(g_ram+0x212,0);g_ram[0x118]=0x49;YiWs11RenderLine(g_ppu,1);
    TEST_ASSERT(!YiWs11Active(),"bonus layout exception never admits unrelated scenes");
    g_snes->cart->rom[0x4893e]^=1;
    YiWs11Init(YI_ROM_EU_12);YiWs11SetEnabled(true);
    g_ram[0x118]=0x13;fixture_word(g_ram+0xc1e,18);
    for(unsigned line=1;line<=224;++line) {ppu_runLine(g_ppu,line);YiWs11RenderLine(g_ppu,line);}
    TEST_ASSERT(!YiWs11Active() && !YiWs11PresentationWide(),"unknown edge code retains safe native autoscroll geometry");
    TEST_ASSERT(g_snes->cart->superfx->immediate_override(NULL,g_snes->cart->superfx,0x09893d,8,1)==8,
      "unverified code never changes movement operands");
    g_snes->cart->rom[0x4893e]^=1;fixture_word(g_ram+0xc1e,0);
    g_snes->cart->rom[0x4fde8] ^= 1;
    YiWs11Init(YI_ROM_EU_12); YiWs11SetEnabled(true);
    YiWs11RenderLine(g_ppu, 1);
    TEST_ASSERT(!YiWs11Active(), "modified ROM anchor disables experimental renderer");
    return 1;
}
#endif

int main(int argc, char **argv) {
 if(argc==3 && !strcmp(argv[1],"--layout-only")) {
   FILE *f=fopen(argv[2],"rb"); if(!f) return 1;
   uint8_t *rom=malloc(YI_ROM_SIZE); size_t n=fread(rom,1,YI_ROM_SIZE,f); fclose(f);
   YiRomProfile profile=YiIdentifyRom(rom,n);
   if(profile==YI_ROM_UNSUPPORTED) return 1;
   RtlRegisterGame(&kGameInfo); if(!SnesInit(rom,(int)n)) return 1;
   GameConfigureRom(profile); YiWs11Init(profile);
   free(rom); if(!YiWs11Supported()) return 1;
   printf("ROM layout verified without guest execution: profile=%d\n",profile);return 0;
 }

 if (!test_gsu_immediate_operand() || !test_gsu_observer_boundary() || !test_ws_world_decoder() || !test_ws_detached_composition()) return 1;
 printf("PAL Rev2 widescreen: %u assertions passed\n", assertions); return 0;
}
