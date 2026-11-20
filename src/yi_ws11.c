/* Optional landscape presentation (USA / Europe Rev 0, 1 and 2).
 * Native camera/activation and framebuffer remain authoritative. Autoscroll
 * player-edge operands optionally follow the wider displayed viewport. */
#include "yi_ws11.h"
#include "common_rtl.h"
#include "common_cpu_infra.h"
#include "snes/cart.h"
#include "snes/superfx.h"
#include <stdio.h>
#include <string.h>

typedef struct OamCapture { uint8_t data[0x800]; bool valid; } OamCapture;
static OamCapture captures[3];
static unsigned capture_next;
static const OamCapture *selected;
static OamCapture frame_capture;
typedef struct OamImage { uint16_t oam[0x100]; uint8_t highOam[0x20]; } OamImage;
static OamImage wide_oam;
static Ppu detached;
static Ppu diagnostic_ppu;
static uint8_t diagnostic_wram[0x20000], diagnostic_gsu[0x10000];
static bool diagnostic_valid, presentation_wide;
/* Keep transient eligibility changes preceding F9, not only its final frame.
 * Host-only metadata: no account data, input changes or guest execution. */
typedef struct WsFrameDiagnostic {
  unsigned frame, mode, pause, stage, camera_x, camera_y;
  unsigned ppu_first, ppu_last, main_first, main_last, ui_first, ui_last;
  unsigned gate, tile_cause, failed_line, scenery, wide, cleared;
  unsigned brightness_first,brightness_last,oam_match,native_luma,center_changed_lines,emu_us,draw_us,moving_columns;
} WsFrameDiagnostic;
static WsFrameDiagnostic frame_diagnostic, diagnostic_history[180];
static unsigned history_next, history_count;
/* Low-resolution pre-F9 picture history: about 6 MiB, without overlays/accounts. */
typedef struct WsHistoryImage { uint8_t native[64*56*4],wide[86*56*4]; } WsHistoryImage;
static WsHistoryImage history_images[180];

/* CPU execution may reach the next scanout before the host presents. Keep
 * the completed field immutable while its successor clears/builds pixels. */
static uint8_t pixels[YI_WS_WIDTH * YI_WS_HEIGHT * 4];
static uint8_t presented_pixels[YI_WS_WIDTH * YI_WS_HEIGHT * 4];
static bool presented_wide;
static bool supported, enabled, active, requested;
typedef struct WsLayout { unsigned table, mode_ram, gameplay_mode, map_anchor, first_sprite, message_ram, autoscroll_ram, pause_ram; bool pal; const char *name; } WsLayout;
static const WsLayout usa_layout = {0xbf7c3,0x146,0x0f,0x4f9e8,0x168583,0xd0f,0xc1c,0xb0f,false,"USA"};
static const WsLayout eu0_layout = {0x7fa24,0x148,0x13,0x4fde8,0x158583,0xd11,0xc1e,0xb11,true,"EU Rev 0"};
static const WsLayout eu1_layout = {0x7fa24,0x148,0x13,0x4fde8,0x158583,0xd11,0xc1e,0xb11,true,"EU Rev 1"};
static const WsLayout eu2_layout = {0x7fa24,0x148,0x13,0x4fde8,0x158583,0xd11,0xc1e,0xb11,true,"EU Rev 2"};
static const WsLayout *layout;
static bool narrow_component, edge_bounds_verified;
static unsigned room_extra_left, room_extra_right;

static unsigned frames, captures_seen, frames_wide, tile_failures, sprite_misses;
static unsigned frame_reject, frame_tile_reason;
static int last_status = -1;
static unsigned last_reject = ~0u;
static unsigned last_frame_reject = ~0u, last_tile_reason = ~0u;
static uint32_t last_room_stream = ~0u;
static unsigned left_pixels, right_pixels, left_width, right_width;
static uint32_t sample_windows;
static unsigned sample_color, sample_main, sample_sub;
static uint16_t rd16(const uint8_t *p) { return p[0] | (uint16_t)p[1] << 8; }
static uint32_t rd24(const uint8_t *p) { return rd16(p) | (uint32_t)p[2] << 16; }

typedef struct RoomProfile {
  uint32_t sprites;
  uint8_t mode;
  bool valid;
  char name[32];
} RoomProfile;
enum { REGULAR_ROOMS = 0xde };
static RoomProfile rooms[REGULAR_ROOMS];
static int cached_room = -1;
/* The welcome scene uses a complete, periodic 32x32 background loaded by
 * $10DA33..$10DCAD, not the ordinary Map16 streaming renderer. */
static bool tiled_intro(void) {
  return layout && g_snes && g_snes->cart &&
      g_snes->cart->ram && g_snes->cart->ramSize >= 0x8000 &&
      ((g_ram[0x118] == (layout->pal ? 0x3d : 0x39) && rd16(g_ram + 0x21a) == 0x80) ||
       (g_ram[0x118] == layout->gameplay_mode - 4 && rd16(g_ram + 0x21a) == 11)) &&
      rd24(g_snes->cart->ram + 0x2600) == (layout->pal ? 0x15855e : 0x16855e);
}
static bool bonus_scene(const Ppu *p) {
  if (!layout || !p) return false;
  unsigned shift = layout->pal ? 4 : 0, mode = g_ram[0x118];
  unsigned kind = rd16(g_ram + 0x212);
  return (mode == 0x2b + shift || mode == 0x2c + shift || mode == 0x23 + shift) &&
      kind <= 10 && !(kind & 1) && p->bgmode == 0x41 &&
      p->bgXsc[0] == 0x6a && p->bgXsc[1] == 0x3a && p->bgXsc[2] == 0x34 &&
      !PPU_interlace(p) && !PPU_overscan(p);
}
static bool tutorial_outro(void) {
  if (!layout || !g_snes || !g_snes->cart || !g_snes->cart->ram) return false;
  unsigned mode = g_ram[0x118], shift = layout->pal ? 4 : 0;
  unsigned stage = rd16(g_ram + 0x21a);
  return (mode == layout->gameplay_mode || mode == 0x1e + shift || mode == 0x1f + shift) &&
      (stage == 11 || stage == 0) && rooms[0x39].valid &&
      rd24(g_snes->cart->ram + 0x2600) == rooms[0x39].sprites;
}
/* Bonus boards and objects retain their native layout. Only their complete
 * tiled backdrop layers continue into the additional picture columns. */
static void bonus_backdrop(Ppu *p) {
  p->screenEnabled[0] &= ~0x11u;
  p->screenEnabled[1] &= ~0x11u;
}
/* Burt's body is a dynamically rendered BG2 bitmap with per-line HDMA.
 * Room 3D uses ordinary Map16 terrain; preserve the complete native body,
 * and never wrap its bitmap into the additional side columns. */
static bool burt_arena(void) {
  return layout && g_snes && g_snes->cart && g_snes->cart->ram &&
      rd16(g_ram+0x218)==0 && rd16(g_ram+0x21a)==3 && rooms[0x3d].valid &&
      rd24(g_snes->cart->ram+0x2600)==rooms[0x3d].sprites;
}
/* Mode 12 uses the same native BG2 bitmap/Map16 terrain split as Salvo.
 * Keep that generated surface in the untouched center, extend terrain only. */
static const RoomProfile *room_profile(void);
static bool bitmap_arena(void) {
  const RoomProfile *room = room_profile();
  return burt_arena() || (room && room->mode == 12);
}
static bool level_overlay(const Ppu *p) {
  return layout && PPU_mode(p)==1 && PPU_bgTilemapWider(p,0) &&
      (g_ram[0x118]==layout->gameplay_mode || g_ram[0x118]==layout->gameplay_mode+1);
}
/* Fixed UI remains centered. Additional scenery is only taken from known
 * background layers, never duplicated boards, text or interactive objects. */
static unsigned ui_scene(const Ppu *p) {
  if (!layout || !p || bonus_scene(p) || PPU_interlace(p) || PPU_overscan(p)) return 0;
  unsigned mode=g_ram[0x118], shift=layout->pal?4:0;
  /* Pause keeps the gameplay mode; its region-specific state byte is the
   * state discriminator. Opening/closing can still show live BG1 scenery;
   * only switch the side policy once the menu actually hides that landscape. */
  if (mode>=layout->gameplay_mode-4 && mode<=layout->gameplay_mode+3 &&
      g_ram[layout->pause_ram] && PPU_mode(p)==1 &&
      !((p->screenEnabled[0] | p->screenEnabled[1]) & 1)) return 4;
  if (PPU_mode(p)==7 && (mode==9+shift || mode==10+shift)) return 3;
  if (PPU_mode(p)>1) return 0;
  if (mode>=0x20+shift && mode<=0x29+shift) return 2;
  if (mode==9+shift || mode==10+shift ||
      (mode>=0x13+shift && mode<=0x15+shift) ||
      (mode>=0x33+shift && mode<=0x37+shift) ||
      (mode>=0x3b+shift && mode<=0x40+shift)) return 1;
  return 0;
}
static void configure_ui_background(Ppu *p,unsigned kind) {
  if(kind==3) return; /* Native Mode7 projection over the complete map. */
  unsigned layers = (kind==2 || kind==4) ? 0u : (PPU_bgTilemapWider(p,1)?2u:0u);
  p->screenEnabled[0] &= layers;
  p->screenEnabled[1] &= layers;
  p->screenWindowed[0]=p->screenWindowed[1]=0;
  p->windowsel=0;p->cgwsel &= 0x3f;
}
static bool ordinary_mode(unsigned mode) {
  /* Mode 3 uses the shared offset-per-tile sampler; 9/10 are special
   * boss/bitmap modes; 13 uses
   * large BG1 tiles; 14 includes intro/title layouts. Keep these native. */
  return mode == 0 || mode == 1 || mode == 2 || mode == 3 || mode == 5 || mode == 6 ||
      mode == 7 || mode == 8 || mode == 11 || mode == 12 || mode == 15;
}
static bool rom_offset(uint32_t pointer, size_t size, size_t *offset) {
  if (pointer >= 0x400000 && pointer < 0x600000) *offset = pointer - 0x400000;
  else if (pointer < 0x400000 && (pointer & 0x8000))
    *offset = (pointer >> 16) * 0x8000 + (pointer & 0x7fff);
  else return false;
  return *offset < size;
}
static void load_room_profiles(const Cart *cart) {
  memset(rooms, 0, sizeof(rooms)); cached_room = -1;
  unsigned candidates = 0;
  for (unsigned i = 0; i < REGULAR_ROOMS; ++i) {
    const uint8_t *entry = cart->rom + layout->table + i * 6;
    size_t obj, spr;
    RoomProfile *room = &rooms[i];
    if (!rom_offset(rd24(entry), cart->romSize, &obj) || obj + 10 > cart->romSize ||
        !rom_offset(rd24(entry + 3), cart->romSize, &spr)) continue;
    room->sprites = rd24(entry + 3);
    /* The first nine packed header fields total 48 bits. Mode is next 5. */
    room->mode = cart->rom[obj + 6] >> 3;
    room->valid = true;
    snprintf(room->name, sizeof(room->name), "%s room %02X (mode %u)", layout->name, i, room->mode);
    if (i == 0x38)
      snprintf(room->name, sizeof(room->name), "Tutorial welcome");
    if (i == 0x39)
      snprintf(room->name, sizeof(room->name), "Playable tutorial");
    candidates += ordinary_mode(room->mode);
  }
  printf("[WS11] ROM room profiles: %u ordinary candidates / %u slots; live PPU/tile guards required.\n",
      candidates, REGULAR_ROOMS);
}
static const RoomProfile *room_profile(void) {
  if (!g_snes || !g_snes->cart || !g_snes->cart->ram ||
      g_snes->cart->ramSize < 0x8000 || rd16(g_ram + 0x218) >= 6 ||
      (rd16(g_ram + 0x21a) >= 72 && !tiled_intro())) return NULL;
  uint32_t sprites = rd24(g_snes->cart->ram + 0x2600);
  if (cached_room >= 0 && rooms[cached_room].valid && rooms[cached_room].sprites == sprites)
    return &rooms[cached_room];
  for (unsigned i = 0; i < sizeof(rooms) / sizeof(rooms[0]); ++i)
    if (rooms[i].valid && rooms[i].sprites == sprites) { cached_room = i; return &rooms[i]; }
  return NULL;
}
const char *YiWs11SceneName(void) {
  if (g_snes && ui_scene(g_snes->ppu)) return "Centered UI / wide background";
  if (g_snes && bonus_scene(g_snes->ppu)) return "Bonus Challenge backdrop";
  const RoomProfile *room = room_profile();
  return room ? room->name : "unsupported room";
}

bool YiWs11WorldTile(const uint8_t *ram, const uint8_t *gsu_ram, const uint8_t *rom, size_t size,
                     int x, int y, uint16_t *tile) {
  if (!ram || !gsu_ram || !rom || !tile || size < 0xc4000 || x < 0 || x >= 4096 || y < 0 || y >= 2048)
    return false;
  /* EU code at $109120 and $09FDE8 retains these RAM layouts. Screen IDs
   * are bytes; the guest reads word $6CA9+index and selects its high byte. */
  unsigned screen = gsu_ram[0xcaa + (y >> 8) * 16 + (x >> 8)] & 0x3f;
  unsigned cell = 0x18000 + screen * 0x200 + (y & 0xf0) * 2 + ((x & 0xf0) >> 3);
  uint16_t map16 = rd16(ram + cell);
  unsigned page = map16 >> 8;
  if (page >= (0x33f2 - 0x32a4) / 2) return false;
  /* GSU ROM bank $4C is the linear mirror at payload offset $0C0000.
   * Each Map16 entry is TL, TR, BL, BR (four 8x8 SNES tilemap words). */
  unsigned base = rd16(rom + 0xc32a4 + page * 2);
  unsigned quad = ((y & 8) ? 2 : 0) + ((x & 8) ? 1 : 0);
  unsigned address = (0x33f2 + base + (map16 & 255) * 8 + quad * 2) & 0xffff;
  if (0xc0000u + address + 1 >= size) return false;
  *tile = rd16(rom + 0xc0000 + address);
  return true;
}

static void capture_oam(void *context, const SuperFx *fx) {
  (void)context;
  if (!supported || !enabled || fx->ram_size < 0x2800) return;
  OamCapture *out = &captures[capture_next++ % 3];
  memcpy(out->data, fx->ram + 0x200, sizeof(out->data));
  out->valid = true;
  ++captures_seen;
}

static bool autoscroll_active(void) {
  unsigned mode=g_ram[0x118];
  return layout && !tiled_intro() && mode>=layout->gameplay_mode-4 &&
      mode<=layout->gameplay_mode+3 && rd16(g_ram+layout->autoscroll_ram)!=0;
}
static uint16_t player_edge_operand(void *context,const SuperFx *fx,
                                    uint32_t pc,uint16_t value,unsigned bytes) {
  (void)context;(void)fx;
  if(pc!=0x09893d && pc!=0x098947)return value;
  if (!supported || !enabled || !edge_bounds_verified || !active ||
      !autoscroll_active() || narrow_component || ui_scene(g_snes->ppu)) return value;
  /* Same edge displacement feeds native push, velocity/subpixel matching,
   * squish death and Baby Mario correction. Sprite handling starts later;
   * camera registers, enemy activation and warp thresholds are untouched. */
  if(pc==0x09893d && bytes==1 && value==8)return (uint16_t)(8-YI_WS_EXTRA);
  if(pc==0x098947 && bytes==2 && value==0xe8)return 0xe8+YI_WS_EXTRA;
  return value;
}
void YiWs11Init(YiRomProfile profile) {
  supported = enabled = active = false;
  presentation_wide = diagnostic_valid = presented_wide = false;
  memset(presented_pixels,0,sizeof(presented_pixels));
  history_next=history_count=0;
  memset(rooms, 0, sizeof(rooms)); cached_room = -1;
  narrow_component = edge_bounds_verified = false;
  memset(captures, 0, sizeof(captures));
  selected = NULL;
  frame_reject = frame_tile_reason = 0;
  capture_next = captures_seen = frames = frames_wide = tile_failures = sprite_misses = 0;
  last_status = -1; last_reject = ~0u;
  last_frame_reject = last_tile_reason = ~0u; last_room_stream = ~0u;
#ifdef YI_WIDESCREEN_PROTOTYPE
  layout = profile == YI_ROM_US_10 ? &usa_layout : profile == YI_ROM_EU_10 ? &eu0_layout :
      profile == YI_ROM_EU_11 ? &eu1_layout : profile == YI_ROM_EU_12 ? &eu2_layout : NULL;
  if (!layout || !g_snes || !g_snes->cart || !g_snes->cart->superfx) return;
  Cart *cart = g_snes->cart;
  cart->superfx->immediate_override=NULL;cart->superfx->immediate_context=NULL;
  /* Exact ROM identity is checked by the host first. These local instruction
   * anchors additionally pin the read-only observer and decoder assumptions. */
  const uint8_t copy_prefix[] = {0xac,0x10,0xf0,0x01,0x0a,0xa1,0x04,0xa2,0xf0,0x02};
  const uint8_t map_prefix[] = {0xf0,0x9e,0x40,0x11,0x51,0x12,0x52};
  if (cart->romSize != YI_ROM_SIZE ||
      memcmp(cart->rom + 0x431ef, copy_prefix, sizeof(copy_prefix)) ||
      memcmp(cart->rom + layout->map_anchor, map_prefix, sizeof(map_prefix)) ||
      rd24(cart->rom + layout->table + 3) != layout->first_sprite) {
    fprintf(stderr, "[WS11] ROM layout anchors differ; widescreen disabled.\n"); return;
  }
  const uint8_t edge_left[]={0xa2,0x60,0xa4,0x08,0x16,0x64,0x0b,0x20};
  const uint8_t edge_right[]={0xf2,0x80,0x01,0xf4,0xe8,0x00,0x16,0x64,0xe6,0x0b,0x72,0xd6};
  edge_bounds_verified=!memcmp(cart->rom+0x4893b,edge_left,sizeof(edge_left)) &&
      !memcmp(cart->rom+0x48944,edge_right,sizeof(edge_right));
  cart->superfx->immediate_override=player_edge_operand;
  supported = true; enabled = requested;
  load_room_profiles(cart);
  cart->superfx->observe_pc = 0x08b1ef;
  cart->superfx->observe = capture_oam;
  cart->superfx->observe_context = NULL;
  printf("[WS11] %s layout verified; requested=%d; original activation.\n", layout->name, requested);
  printf("[WS11] autoscroll player bounds: %s; camera/activation native.\n",edge_bounds_verified?"16:9 edges verified":"native 4:3 fallback");
  printf("[WS11] status RAM: message=%04x autoscroll=%04x\n", layout->message_ram, layout->autoscroll_ram);
#else
  (void)profile;
#endif
}

void YiWs11Toggle(void) {
  YiWs11SetEnabled(!requested);
}
void YiWs11SetEnabled(bool value) {
  requested = value; enabled = supported && requested; active = presentation_wide = presented_wide = false;
  memset(captures, 0, sizeof(captures)); capture_next = 0; selected = NULL;
  printf("[WS11] User switch: %s\n", enabled ? "enabled" : "native 4:3");
}
bool YiWs11Active(void) { return active; }
bool YiWs11PresentationWide(void) { return enabled && presented_wide; }
bool YiWs11Supported(void) { return supported; }
bool YiWs11VerticalActive(void) { return false; }
const uint8_t *YiWs11Pixels(void) { return presented_pixels; }

/* Reproduce the guest buffer -> OAM ordering, either with native clipping
 * (for generation matching) or only in the private wider presentation copy. */
static void build_oam(OamImage *out, const OamCapture *capture, int extra) {
  for (unsigned slot = 16; slot < 128; ++slot) out->oam[slot * 2] = 0xf000;
  int forward = 16, reverse = 127;
  for (unsigned i = 0; i < 256 && forward <= reverse; ++i) {
    const uint8_t *p = capture->data + i * 8;
    int x = (int16_t)rd16(p), y = (int16_t)rd16(p + 2);
    if (x < -15 - extra || x >= 256 + extra || y < -15 || y >= 240) continue;
    unsigned flags = rd16(p + 6);
    int slot = flags & 0x4000 ? reverse-- : forward++;
    out->oam[slot * 2] = (x & 255) | (((y - 1) & 255) << 8);
    out->oam[slot * 2 + 1] = rd16(p + 4);
    unsigned shift = (slot & 3) * 2;
    unsigned pair = ((x >> 8) & 1) | (flags & 2);
    out->highOam[slot >> 2] = (out->highOam[slot >> 2] & ~(3u << shift)) | (pair << shift);
  }
}

static bool matches_oam(const Ppu *native, const OamCapture *capture) {
  if (!capture->valid) return false;
  /* Only temporary host memory is written. Native HUD slots 0..15 remain
   * authoritative. Inactive slots' stale tile/high bytes are irrelevant. */
  OamImage expected = {0};
  build_oam(&expected, capture, 0);
  for (unsigned slot = 16; slot < 128; ++slot) {
    uint16_t xy = native->oam[slot * 2], ex = expected.oam[slot * 2];
    if ((xy >> 8) == 0xf0 && (ex >> 8) == 0xf0) continue;
    unsigned shift = (slot & 3) * 2;
    if (xy != ex || native->oam[slot * 2 + 1] != expected.oam[slot * 2 + 1] ||
        ((native->highOam[slot >> 2] ^ expected.highOam[slot >> 2]) & (3u << shift)))
      return false;
  }
  return true;
}

static void apply_wide_oam(Ppu *out) {
  /* The selected GSU generation is frozen for this field. Convert once,
   * then copy only game-object slots. HUD slots 0..15 remain scanline-live. */
  memcpy(out->oam + 32, wide_oam.oam + 32, sizeof(wide_oam.oam) - 32 * sizeof(uint16_t));
  memcpy(out->highOam + 4, wide_oam.highOam + 4, sizeof(wide_oam.highOam) - 4);
}

static int world_origin(int camera, unsigned scroll, unsigned mask) {
  /* Recover world high bits from camera; pixel phase always comes from the
   * actual PPU scanline, including its vertical +1 convention. */
  int delta = ((int)scroll - camera) & mask;
  if (delta > (int)(mask / 2)) delta -= mask + 1;
  return camera + delta;
}

static unsigned map_address(const Ppu *p, int x, int y) {
  unsigned address = PPU_bgTilemapAdr(p, 0) + (((y >> 3) & 31) << 5) + ((x >> 3) & 31);
  if ((x & 256) && PPU_bgTilemapWider(p, 0)) address += 0x400;
  if ((y & 256) && PPU_bgTilemapHigher(p, 0)) address += PPU_bgTilemapWider(p, 0) ? 0x800 : 0x400;
  return address & 0x7fff;
}

/* The native OPT table only describes the 256-column screen. Extending its
 * ring entries repeats unrelated moving columns at the sides; negative X
 * columns do not use OPT at all. Reconstruct only the detached BG1 coordinates
 * from the same moving rectangles and sine phase, after matching all native
 * columns. No guest table, movement, camera or activation is changed. */
static bool moving_columns_valid, moving_scanout_ready;
static uint8_t moving_rectangles[20*6];
static int moving_sine, moving_camera_x, moving_camera_y;
static int16_t moving_offsets[YI_WS_WIDTH];
static int moving_displacement(int world_x,int sine) {
  int offset=0, tile_x=world_x>>4, tile_y=moving_camera_y>>4;
  for(unsigned i=0;i<20;++i) {
    const uint8_t *r=moving_rectangles+i*6;
    if(tile_x>=r[0] && tile_x<=r[0]+r[2] &&
        tile_y>=r[1]-11 && tile_y<=r[1]+r[3])
      offset=((int8_t)r[4]*sine)>>8;
  }
  return offset;
}
static void prepare_moving_columns(const Ppu *p) {
  moving_columns_valid=false;
  const Cart *cart=g_snes->cart;
  if(PPU_mode(p)!=2 || cart->ram[0x1ca]!=1 || cart->romSize<0x42e18)return;
  /* Wavy-lava rectangles use a different formula; retain their native path. */
  for(unsigned i=0;i<20;++i) {
    const uint8_t *r=cart->ram+0x1f72+i*4;
    if(r[0] || r[1] || r[2])return;
  }
  memcpy(moving_rectangles,cart->ram+0x449e,sizeof(moving_rectangles));
  /* CPU camera state can already be ahead of the displayed scroll. Use
   * the same live PPU world origin as the landscape decoder. */
  moving_camera_x=world_origin(rd16(g_ram+0x39),p->hScroll[0],PPU_bgTilemapWider(p,0)?511:255);
  moving_camera_y=rd16(g_ram+0x3b);
  unsigned timer=rd16(g_ram+0xcfd);
  /* CPU timer, GSU geometry and displayed VRAM are buffered separately.
   * Accept a recent phase only if every native column agrees. */
  for(unsigned phase=0;phase<8;++phase) {
    int sine=(int16_t)rd16(cart->rom+0x42c18+((timer+phase*2)&0x1fe));
    bool matches=true;
    /* The first native fetch column deliberately bypasses OPT. Its global
     * scroll can differ from the per-column table without invalidating it. */
    for(int x=8;x<256;x+=8) {
      int lx,ly;PpuLegacyBgCoordinates(p,0,x,32,&lx,&ly);
      int world_x=moving_camera_x+x;
      if(((lx-p->hScroll[0]-x)&1023) || ((ly-(32+moving_camera_y+moving_displacement(world_x,sine)))&1023)) {
        matches=false;break;
      }
    }
    if(matches){
      moving_sine=sine;moving_columns_valid=true;
      for(int x=-YI_WS_EXTRA;x<256+YI_WS_EXTRA;++x)
        moving_offsets[x+YI_WS_EXTRA]=(int16_t)moving_displacement(moving_camera_x+x,sine);
      break;
    }
  }
}
static void landscape_coordinates(const Ppu *p,int layer,int x,int line,
    int *lx,int *ly,const void *context) {
  (void)context;
  if(moving_columns_valid && PPU_mode(p)==2 && layer==0 && (x<0 || x>=256)) {
    *lx=p->hScroll[0]+x;
    *ly=line+(moving_camera_y&1023)+moving_offsets[x+YI_WS_EXTRA];
  }
}

static int render_world_y(const Ppu *p,int y) {
  /* Mode 2 offset columns wrap through the 11-bit world-grid address.
   * Do not reject the complete frame when a moving column crosses row 2048. */
  return PPU_mode(p)==2 ? y&2047 : y;
}

static bool fill_world_span(Ppu *out, const uint8_t *ram, const uint8_t *gsu,
                            const Cart *cart, int ox, int wy, int line,
                            int begin, int end) {
  /* A tilemap entry serves an entire 8-pixel span. Decode once per entry,
   * including unaligned edge fragments, instead of once per output pixel. */
  for (int x = begin; x < end; x += 8 - ((out->hScroll[0] + x) & 7)) {
    uint16_t tile;
    int lx, ly;
    PpuLegacyBgCoordinates(out, 0, x, line, &lx, &ly);
    landscape_coordinates(out,0,x,line,&lx,&ly,NULL);
    int wx = ox + lx - out->hScroll[0];
    int world_y = render_world_y(out,wy + ly - out->vScroll[0] - line);
    if (!YiWs11WorldTile(ram, gsu, cart->rom, cart->romSize, wx, world_y, &tile)) return false;
    out->vram[map_address(out, lx, ly)] = tile;
  }
  return true;
}

/* Tileset 0A uses 16x16 BG3 cross-section covers. The guest derives each
 * cover from the TOP-LEFT 8x8 BG1 word (GSU $08BC36..$08BC97). Its native
 * DMA only refreshes the current row/column, so offscreen BG3 slots can hold
 * unrelated old covers. Generate the same words in our private PPU copy.
 * That GSU routine is byte-identical in all four supported ROM revisions. */
static void fill_cross_section_span(Ppu *out, const uint8_t *ram, const uint8_t *gsu,
                                   const Cart *cart, int line, int begin, int end) {
  /* BG3 tileset is eight bytes before the selected region's level mode. */
  if (rd16(ram+layout->mode_ram-8)!=0x0a || PPU_mode(out)!=1 ||
      !PPU_bigTiles(out,2) || tiled_intro() || bonus_scene(out) || ui_scene(out)) return;
  int ox=world_origin(rd16(ram+0x39),out->hScroll[2],PPU_bgTilemapWider(out,2)?1023:511);
  int oy=world_origin(rd16(ram+0x3b),out->vScroll[2],PPU_bgTilemapHigher(out,2)?1023:511);
  for(int x=begin;x<end;x+=16-((out->hScroll[2]+x)&15)) {
    uint16_t top_left;
    if(!YiWs11WorldTile(ram,gsu,cart->rom,cart->romSize,(ox+x)&~15,(oy+line)&~15,&top_left))continue;
    unsigned tile=top_left&0x3ff;
    uint16_t cover=tile>=0x180 && tile<0x200 ? (top_left&0xc07f)|0x2100 : 0x01ce;
    int lx=x+out->hScroll[2],ly=line+out->vScroll[2];
    unsigned address=PPU_bgTilemapAdr(out,2)+(((ly>>4)&31)<<5)+((lx>>4)&31);
    if((lx&512)&&PPU_bgTilemapWider(out,2))address+=0x400;
    if((ly&512)&&PPU_bgTilemapHigher(out,2))address+=PPU_bgTilemapWider(out,2)?0x800:0x400;
    out->vram[address&0x7fff]=cover;
  }
}

/* One-screen-wide connected terrain islands are bounded 4:3 rooms even
 * when the game's global camera clamp still spans the entire level grid.
 * Determine this from the native allocated-screen map, once per field;
 * no room IDs, camera writes or wider object activation are involved. */
/* Shared room-map analysis: the component width chooses native 4:3 for
 * narrow rooms; independently, visible-row boundaries limit added margins. */
static bool analyze_room_bounds(void) {
  room_extra_left=room_extra_right=YI_WS_EXTRA;
  if(tiled_intro())return false;
  unsigned x=rd16(g_ram+0x39),y=rd16(g_ram+0x3b),cx=(x+128)/256,cy=(y+112)/256;
  const uint8_t *map=g_snes->cart->ram+0xcaa;
  if(cx>=16 || cy>=8 || !(map[cy*16+cx]&0x3f))return false;
  uint8_t queue[128],seen[128]={0};unsigned head=0,tail=0,min_x=cx,max_x=cx;
  queue[tail++]=(uint8_t)(cy*16+cx);seen[cy*16+cx]=1;
  while(head<tail) {
    unsigned cell=queue[head++],px=cell&15,py=cell>>4;
    if(px<min_x)min_x=px;if(px>max_x)max_x=px;
    const int dx[]={-1,1,0,0},dy[]={0,0,-1,1};
    for(unsigned i=0;i<4;++i) {
      int nx=(int)px+dx[i],ny=(int)py+dy[i];
      if(nx<0 || nx>=16 || ny<0 || ny>=8)continue;
      unsigned next=(unsigned)ny*16+(unsigned)nx;
      if(!seen[next] && (map[next]&0x3f)){seen[next]=1;queue[tail++]=(uint8_t)next;}
    }
  }
  bool narrow=min_x==max_x;
  if(autoscroll_active() || y+223>=2048 || x+256>4096)return narrow;
  /* Union of row runs: only mask where every visible row confirms a void. */
  unsigned left=16,right=0;
  for(unsigned row=y/256;row<=(y+223)/256;++row) {
    if(!(map[row*16+cx]&0x3f))return narrow;
    unsigned lo=cx,hi=cx+1;
    while(lo && (map[row*16+lo-1]&0x3f))--lo;
    while(hi<16 && (map[row*16+hi]&0x3f))++hi;
    if(lo<left)left=lo;if(hi>right)right=hi;
  }
  if(x>=left*256 && x+256<=right*256) {
    unsigned l=x-left*256,r=right*256-(x+256);
    if(l<room_extra_left)room_extra_left=l;
    if(r<room_extra_right)room_extra_right=r;
  }
  return narrow;
}

/* Bit positions are printed with named causes below. Scene IDs and header
 * offsets come from the selected ROM layout. */
static bool supported_plant_event(const uint8_t *s) {
  unsigned event = rd16(s + 0x1e2a), slot = g_ram[0x1e2c];
  /* $03CCBA writes the byte-sized owner slot. The native camera follows
   * active scene objects, then uses -8..-1 to return to Yoshi. Validate
   * the owner slot/state; room, transformations and live tile checks still apply.
   * Absolute $1E2C is mirrored WRAM, unlike cartridge-mapped $7E2A. */
  if ((event != 1 && event < 0xfff8) || slot > 0x5c || (slot & 3)) return false;
  unsigned id = rd16(s + 0x1360 + slot);
  return (id != 0 && id < 0x200) &&
      (event != 1 || rd16(s + 0xf00 + slot) != 0);
}
/* Unknown player-edge code must stay in the original viewport rather than
 * advertise terrain that the native boundary would prevent entering. */
static bool native_autoscroll_view(void) {
  return autoscroll_active() && !edge_bounds_verified;
}
static unsigned reject_mask(const Ppu *p) {
  const uint8_t *s = g_snes->cart->ram;
  unsigned r = 0;
  if (!supported || !enabled) r |= 1u << 0;
  if (g_snes->pal != layout->pal || g_snes->cart->ramSize < 0x8000) r |= 1u << 1;
  if (bonus_scene(p)) return r | (1u << 2); /* Bonus gameplay remains native 4:3. */
  if (native_autoscroll_view()) return r | (1u << 6);
  if (ui_scene(p)) return r;
  if (narrow_component) r |= 1u << 11;
  unsigned mode = g_ram[0x118];
  bool level_transition = mode >= layout->gameplay_mode - 4 && mode <= layout->gameplay_mode + 3;
  if (mode != layout->gameplay_mode && !level_transition && !tiled_intro() &&
      mode != 0x1e + (layout->pal ? 4 : 0) && mode != 0x1f + (layout->pal ? 4 : 0) &&
      !(bitmap_arena() && (mode==0x30+(layout->pal?4:0) || mode==0x31+(layout->pal?4:0)))) r |= 1u << 2;
  const RoomProfile *room = room_profile();
  if (!room) r |= 1u << 3;
  /* Unpacked level mode: USA $0146, Europe Rev 1 $0148. */
  if (room && (tiled_intro() ? room != &rooms[0x38] || room->mode != 14 ||
      rd16(g_ram + layout->mode_ram) != 0 :
      !ordinary_mode(room->mode) || rd16(g_ram + layout->mode_ram) != room->mode ||
      ((room->mode == 3) != (PPU_mode(p) == 2)))) r |= 1u << 4;
  /* Dialog overlays keep the native center; landscape data is still checked. */
  /* Autoscroll viewport constraints are handled before UI and terrain guards. */
  if (rd16(s + 0x1e2a) && !supported_plant_event(s) && !bitmap_arena()) r |= 1u << 7;
  /* Ordinary player forms (car, mole, helicopter, train, submarine,
   * skis and Super Baby Mario) use the same native terrain renderer.
   * Unknown forms/states remain excluded; no guest activation is widened. */
  unsigned form = rd16(s + 0xae), morph = rd16(s + 0x14e);
  if (form > 0x12 || (form & 1) || morph > 6) r |= 1u << 8;
  if ((tiled_intro() && p->bgXsc[0] != 0x68) || (PPU_mode(p) != 1 && PPU_mode(p) != 2) || PPU_bigTiles(p, 0) || (!PPU_bgTilemapWider(p, 0) && !tiled_intro()) ||
      PPU_interlace(p) || PPU_overscan(p)) r |= 1u << 9;
  /* Menus can retain game mode $13 and the room's scroll/map registers,
   * while suppressing BG1 entirely (r4 trace: TM/TS=$14/$00, BGMODE=$09).
   * Never synthesize landscape bands into that non-landscape presentation. */
  if (!((p->screenEnabled[0] | p->screenEnabled[1]) & 1) && !tutorial_outro() && !level_overlay(p) && !bitmap_arena()) r |= 1u << 10;
  return r;
}
static bool eligible(const Ppu *p) { return reject_mask(p) == 0; }
static void print_rejects(unsigned mask, const Ppu *p) {
  unsigned owner = g_ram[0x1e2c];
  static const char *names[] = {"switch", "region", "game-mode", "room", "level-mode",
      "message", "autoscroll", "camera-event", "transformation", "ppu-layout", "landscape-hidden", "bounded-room"};
  printf("[WS11] gate=%03x reasons=", mask);
  for (unsigned i = 0; i < sizeof(names)/sizeof(names[0]); ++i) if (mask & (1u << i)) printf("%s,", names[i]);
  if (!mask) printf("none (gate open; tile checks can still reject)");
  printf(" message=%u auto=%u cameraEvent=%u form=%u transform=%u scroll=%u,%u owner=%02x id=%03x state=%u\n",
      rd16(g_ram + layout->message_ram), rd16(g_ram + layout->autoscroll_ram), rd16(g_snes->cart->ram + 0x1e2a),
      rd16(g_snes->cart->ram + 0xae), rd16(g_snes->cart->ram + 0x14e), p->hScroll[0], p->vScroll[0], g_ram[0x1e2c],
      owner <= 0x5c ? rd16(g_snes->cart->ram + 0x1360 + owner) : 0,
      owner <= 0x5c ? rd16(g_snes->cart->ram + 0xf00 + owner) : 0);
}

static void copy_legacy_ppu(Ppu *out, const Ppu *source) {
  /* Legacy composition reads registers, host policy, OAM/CGRAM/VRAM and
   * overlay selectors. It never reads Mode-2 capture arrays or fast-renderer
   * bgBuffers/overlayBuffers, and its OBJ helper clears objBuffer before
   * evaluation. Skip unused capture/scratch storage per scanline.
   * Keep all actual picture memory live;
   * caching VRAM/palette across lines would break DMA/raster effects. */
  memcpy(out, source, offsetof(Ppu, wsMode2Capture));
  memcpy((uint8_t *)out + offsetof(Ppu, wsLayerClamp),
         (const uint8_t *)source + offsetof(Ppu, wsLayerClamp),
         offsetof(Ppu, bgBuffers) - offsetof(Ppu, wsLayerClamp));
  memcpy(out->overlayCaptures, source->overlayCaptures, sizeof(out->overlayCaptures));
  memcpy((uint8_t *)out + offsetof(Ppu, renderPitch),
         (const uint8_t *)source + offsetof(Ppu, renderPitch),
         sizeof(Ppu) - offsetof(Ppu, renderPitch));
  if (source->renderVram) memcpy(out->vram, source->renderVram, sizeof(out->vram));
  out->renderVram = NULL; /* Snapshot owns its picture memory, never a live view. */
}

static bool original_band_is_black(unsigned begin, unsigned end) {
  for (unsigned y = begin; y < end; ++y) {
    const uint8_t *p = pixels + (y * YI_WS_WIDTH + YI_WS_EXTRA) * 4;
    for (unsigned x = 0; x < 256; ++x)
      if (p[x*4] || p[x*4+1] || p[x*4+2]) return false;
  }
  return true;
}
/* Dialog UI stays native. Apply the same detached backdrop policy to sides
 * and upper corners; native PPU/window state is never changed. */
static void configure_dialog_backdrop(Ppu *p) {
  p->screenWindowed[0] = p->screenWindowed[1] = 0;
  p->windowsel = 0;
  p->cgwsel &= 0x3f;
  p->screenEnabled[0] &= ~(4u | 16u);
  p->screenEnabled[1] &= ~(4u | 16u);
}


void YiWs11RenderLine(const Ppu *native, unsigned line) {
  if (!supported || !line || line > YI_WS_HEIGHT) return;
  if (line == 1) {
    ++frames;
    narrow_component=analyze_room_bounds();
    moving_columns_valid=moving_scanout_ready=false;
    presentation_wide = false;
    left_pixels = right_pixels = left_width = right_width = 0;
    frame_reject = reject_mask(native); frame_tile_reason = 0;
    active = frame_reject == 0;
    memset(&frame_diagnostic,0,sizeof(frame_diagnostic));
    frame_diagnostic.frame=frames;frame_diagnostic.mode=g_ram[0x118];
    frame_diagnostic.pause=g_ram[layout->pause_ram];frame_diagnostic.stage=rd16(g_ram+0x21a);
    frame_diagnostic.camera_x=rd16(g_ram+0x39);frame_diagnostic.camera_y=rd16(g_ram+0x3b);
    frame_diagnostic.ppu_first=native->bgmode;frame_diagnostic.main_first=native->screenEnabled[0];
    frame_diagnostic.ui_first=ui_scene(native);frame_diagnostic.brightness_first=native->inidisp;
    if(frame_reject)frame_diagnostic.failed_line=1;

    selected = NULL;
    for (unsigned i = 0; active && i < 3; ++i) {
      const OamCapture *c = &captures[(capture_next + 2 - i) % 3];
      if (matches_oam(native, c)) { frame_capture = *c; selected = &frame_capture; break; }
    }
    if (active && !selected) ++sprite_misses;
    if (selected) build_oam(&wide_oam, selected, YI_WS_EXTRA);
    memset(pixels, 0, sizeof(pixels));
  }
  if (active) {
    unsigned reason = reject_mask(native);
    if (reason) { frame_reject |= reason; active = false; if(!frame_diagnostic.failed_line)frame_diagnostic.failed_line=line; }
  }
  /* Retain the native raster blanking; extend only visible scanlines sideways.
   * No vertical reconstruction, fixed crop or extra overlay is applied. */
  bool blank_band_line = PPU_forcedBlank(native) || PPU_brightness(native) == 0;
  if (supported && line == 112 && g_snes->cart->ramSize >= sizeof(diagnostic_gsu)) {
    diagnostic_ppu = *native;
    memcpy(diagnostic_ppu.vram, PpuRenderVram(native), sizeof(diagnostic_ppu.vram));
    diagnostic_ppu.renderVram = NULL;
    memcpy(diagnostic_wram, g_ram, sizeof(diagnostic_wram));
    memcpy(diagnostic_gsu, g_snes->cart->ram, sizeof(diagnostic_gsu));
    diagnostic_valid = true;
  }
  if (active && !blank_band_line) {
    /* Initial blank scanlines may precede the displayed scroll/HDMA state.
     * Freeze column motion at the first visible line, not the blank border. */
    if(!moving_scanout_ready){prepare_moving_columns(native);moving_scanout_ready=true;}
    const Cart *cart = g_snes->cart;
    int ox = world_origin(rd16(g_ram + 0x39), native->hScroll[0], PPU_bgTilemapWider(native, 0) ? 511 : 255);
    int oy = world_origin(rd16(g_ram + 0x3b), native->vScroll[0], PPU_bgTilemapHigher(native, 0) ? 511 : 255);
    int wy = oy + (int)line;
    /* A live native-row cross-check rejects wrong room layout/scroll phase.
     * Dynamic cells can differ; at least 3/4 samples must agree. */
    unsigned matches = 0;
    for (int x = 0; x < 256; x += 64) {
      uint16_t tile;
      int lx, ly;
      PpuLegacyBgCoordinates(native, 0, x, line, &lx, &ly);
      if (YiWs11WorldTile(g_ram, cart->ram, cart->rom, cart->romSize,
          ox + lx - native->hScroll[0], render_world_y(native,wy + ly - native->vScroll[0] - line), &tile) &&
          tile == PpuRenderVram(native)[map_address(native, lx, ly)])
        ++matches;
    }
    if (!tiled_intro() && !bonus_scene(native) && !ui_scene(native) && matches < 3) { active = false; ++tile_failures; frame_tile_reason = 1; if(!frame_diagnostic.failed_line)frame_diagnostic.failed_line=line; }
    if (active) {
      copy_legacy_ppu(&detached, native);
      detached.renderVram = NULL;
      /* Non-simulation host pointers must not alias live extraction surfaces. */
      memset(&detached.mode7Hd, 0, sizeof(detached.mode7Hd));
      memset(detached.overlayRenderBuffer, 0, sizeof(detached.overlayRenderBuffer));
      detached.widescreenLineEnhancer = NULL;
      PpuSetExtraSpace(&detached, YI_WS_EXTRA);
      int left = ox - rd16(cart->ram + 0x1e18);
      int right = rd16(cart->ram + 0x1e1a) + 256 - (ox + 256);
      if(left>(int)room_extra_left)left=room_extra_left;
      if(right>(int)room_extra_right)right=room_extra_right;
      PpuSetExtraSideSpace(&detached, (tiled_intro() || bonus_scene(native) || ui_scene(native)) ? YI_WS_EXTRA : left, (tiled_intro() || bonus_scene(native) || ui_scene(native)) ? YI_WS_EXTRA : right, 0);
      PpuSetWidescreenWindowExpansion(&detached, 0x3f, 3);
      /* Text/HUD overlays stay in the authoritative 256-column center. */
      if (rd16(g_ram + layout->message_ram)) {
        configure_dialog_backdrop(&detached);
      }
      if (bonus_scene(native)) bonus_backdrop(&detached);
      if (tutorial_outro() || (level_overlay(native) && !((native->screenEnabled[0]|native->screenEnabled[1])&1))) detached.screenEnabled[0] |= 1;
    if (bitmap_arena()) { detached.screenEnabled[0]&=~2u; detached.screenEnabled[1]&=~2u; }
    if (ui_scene(native)) configure_ui_background(&detached,ui_scene(native));
      left_width = detached.extraLeftCur; right_width = detached.extraRightCur;
      if (line == 112) {
        sample_windows = native->windowsel; sample_color = native->cgwsel;
        sample_main = native->screenEnabled[0]; sample_sub = native->screenEnabled[1];
      }
      detached.renderBuffer = pixels;
      detached.renderPitch = YI_WS_WIDTH * 4;
      detached.renderFlags = kPpuRenderFlags_NoSpriteLimits;
      detached.wsOamLeftHintStrict = detached.wsOamRightHintStrict = 1;
      memset(detached.wsOamLeftHint, 0xff, sizeof(detached.wsOamLeftHint));
      memset(detached.wsOamRightHint, 0, sizeof(detached.wsOamRightHint));
      if (selected) {
        apply_wide_oam(&detached);
        memset(detached.wsOamRightHint, 0xff, sizeof(detached.wsOamRightHint));
      }
      if (!tiled_intro() && !bonus_scene(native) && !ui_scene(native) && (!fill_world_span(&detached, g_ram, cart->ram, cart, ox, wy, line, -(int)detached.extraLeftCur, 0) ||
          !fill_world_span(&detached, g_ram, cart->ram, cart, ox, wy, line, 256, 256 + detached.extraRightCur))) {
        active = false; ++tile_failures; frame_tile_reason = 2; if(!frame_diagnostic.failed_line)frame_diagnostic.failed_line=line;
      }
      if (active) {
        fill_cross_section_span(&detached,g_ram,cart->ram,cart,line,-(int)detached.extraLeftCur,0);
        fill_cross_section_span(&detached,g_ram,cart->ram,cart,line,256,256+detached.extraRightCur);
        /* Results use BG3 for text, not a periodic backdrop. Like pause,
         * keep their margins black even when CGRAM backdrop is non-black. */
        unsigned ui_kind=ui_scene(native);
        if(ui_kind!=2 && ui_kind!=4) PpuRenderDetachedLegacyLineWithCoordinates(&detached, line, moving_columns_valid ? landscape_coordinates : NULL, NULL);
        const uint8_t *row = pixels + (line - 1) * YI_WS_WIDTH * 4;
        for (unsigned x = 0; x < YI_WS_EXTRA; ++x) {
          if (row[x*4] || row[x*4+1] || row[x*4+2]) ++left_pixels;
          unsigned r = x + YI_WS_EXTRA + 256;
          if (row[r*4] || row[r*4+1] || row[r*4+2]) ++right_pixels;
        }
      }
    }
  }
  /* This is the entire native picture, not a re-render or a scaled crop. */
  memcpy(pixels + ((line - 1) * YI_WS_WIDTH + YI_WS_EXTRA) * 4,
         native->renderBuffer + (line - 1) * native->renderPitch, 256 * 4);
  if (line == YI_WS_HEIGHT) {
    frame_diagnostic.scenery=active;
    if (!active && enabled && !narrow_component && !native_autoscroll_view() && g_ram[0x118] >= layout->gameplay_mode - 4 &&
        g_ram[0x118] <= layout->gameplay_mode + 3 && original_band_is_black(0, 224)) {
      memset(pixels, 0, sizeof(pixels)); active = true;
    }
    /* Keep presentation geometry stable through game/goal transitions even
     * when this field cannot safely supply additional scenery. Native center
     * is already copied; never repeat previous-frame landscape. Bonus stays 4:3. */
    bool game_transition = g_ram[0x118] >= layout->gameplay_mode - 4 &&
        g_ram[0x118] <= layout->gameplay_mode + 3;
    bool goal_transition = g_ram[0x118] == 0x1e + (layout->pal ? 4 : 0) ||
        g_ram[0x118] == 0x1f + (layout->pal ? 4 : 0);
    if (!active && enabled && !narrow_component && !native_autoscroll_view() && !bonus_scene(native) &&
        ((frame_tile_reason && !frame_reject && room_profile()) || game_transition || goal_transition)) {
      frame_diagnostic.cleared=1;
      /* No stale side pixels during room loading; native center is current. */
      for (unsigned y=0;y<224;++y) {
        memset(pixels+y*YI_WS_WIDTH*4,0,YI_WS_EXTRA*4);
        memset(pixels+(y*YI_WS_WIDTH+YI_WS_EXTRA+256)*4,0,YI_WS_EXTRA*4);
      }
      presentation_wide = game_transition || goal_transition;
      if (frame_tile_reason && !frame_reject && room_profile()) active = true;
    }
    /* Publish pixels and geometry together, only after a complete scanout.
     * This adds no frame delay: the host still receives this field at line224. */
    memcpy(presented_pixels,pixels,sizeof(pixels));
    presented_wide=active || presentation_wide;
    frame_diagnostic.ppu_last=native->bgmode;frame_diagnostic.main_last=native->screenEnabled[0];
    frame_diagnostic.ui_last=ui_scene(native);frame_diagnostic.gate=frame_reject;
    frame_diagnostic.tile_cause=frame_tile_reason;frame_diagnostic.wide=YiWs11PresentationWide();
    frame_diagnostic.brightness_last=native->inidisp;frame_diagnostic.oam_match=selected!=NULL;
    frame_diagnostic.moving_columns=moving_columns_valid;
    WsHistoryImage *image=&history_images[history_next%180];unsigned luma=0;
    for(unsigned y=0;y<224;++y)
      if(memcmp(native->renderBuffer+y*native->renderPitch,pixels+(y*YI_WS_WIDTH+YI_WS_EXTRA)*4,256*4))++frame_diagnostic.center_changed_lines;
    for(unsigned y=0;y<56;++y) {
      for(unsigned x=0;x<64;++x) {
        const uint8_t *src=native->renderBuffer+(y*4)*native->renderPitch+x*16;
        uint8_t *dst=image->native+(y*64+x)*4;memcpy(dst,src,4);dst[3]=255;
        luma+=src[0]+src[1]+src[2];
      }
      for(unsigned x=0;x<86;++x) {
        uint8_t *dst=image->wide+(y*86+x)*4;memcpy(dst,pixels+((y*4)*YI_WS_WIDTH+x*4)*4,4);dst[3]=255;
      }
    }
    frame_diagnostic.native_luma=luma/(64*56*3);
    diagnostic_history[history_next++%180]=frame_diagnostic;if(history_count<180)++history_count;
    if (active) ++frames_wide;
    unsigned end_reject = reject_mask(native);
    uint32_t room_stream = rd24(g_snes->cart->ram + 0x2600);
    if ((int)active != last_status || end_reject != last_reject ||
        frame_reject != last_frame_reject || frame_tile_reason != last_tile_reason ||
        room_stream != last_room_stream || frames % 300 == 0) {
      printf("[WS11] field=%u world=%u\n", frames, rd16(g_ram + 0x218));
      printf("[WS11] %s mode=%02x stage=%u levelMode=%u sprites=%s captures=%u wide=%u tileReject=%u spriteMiss=%u camera=%u,%u PPU=%02x BG1SC=%02x room=%06x\n",
        active ? "16:9" : "native fallback", g_ram[0x118], rd16(g_ram + 0x21a), rd16(g_ram + layout->mode_ram),
        selected ? "matched" : "native-only", captures_seen, frames_wide, tile_failures, sprite_misses,
        rd16(g_ram + 0x39), rd16(g_ram + 0x3b), native->bgmode, native->bgXsc[0],
        rd24(g_snes->cart->ram + 0x2600));
      print_rejects(end_reject, native);
      printf("[WS11] frameGate=%03x tileCause=%s roomProfile=%s\n", frame_reject,
          frame_tile_reason == 1 ? "native-row-mismatch" : frame_tile_reason == 2 ? "invalid-world-cell" : "none",
          YiWs11SceneName());
      printf("[WS11] sides=%u/%u nonBlack=%u/%u color=%02x windows=%06x main/sub=%02x/%02x\n",
          left_width, right_width, left_pixels, right_pixels, sample_color,
          sample_windows, sample_main, sample_sub);
      last_status = active; last_reject = end_reject;
      last_frame_reject = frame_reject; last_tile_reason = frame_tile_reason; last_room_stream = room_stream;
    }
  }
}

void YiWs11FrameTiming(unsigned emu_us,unsigned draw_us) {
  if(history_count){WsFrameDiagnostic *d=&diagnostic_history[(history_next-1)%180];d->emu_us=emu_us;d->draw_us=draw_us;}
}
static void bmp_word(uint8_t *p,unsigned value) {for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(value>>(i*8));}
static void write_history_sheet(const char *path,bool wide) {
  unsigned tile_w=wide?86:64,w=tile_w*12,h=56*((history_count+11)/12),pitch=w*3;
  if(!h)return;FILE *f=fopen(path,"wb");if(!f)return;
  uint8_t header[54]={0};header[0]='B';header[1]='M';bmp_word(header+2,54+pitch*h);
  bmp_word(header+10,54);bmp_word(header+14,40);bmp_word(header+18,w);bmp_word(header+22,h);header[26]=1;header[28]=24;
  fwrite(header,1,54,f);uint8_t row[1032*3];
  for(unsigned y=h;y>0;--y) {
    memset(row,0,pitch);unsigned sy=y-1;
    for(unsigned column=0;column<12;++column) {
      unsigned frame=(sy/56)*12+column;if(frame>=history_count)continue;
      const WsHistoryImage *image=&history_images[(history_next-history_count+frame)%180];
      const uint8_t *src=(wide?image->wide:image->native)+(sy%56)*tile_w*4;
      for(unsigned x=0;x<tile_w;++x)memcpy(row+(column*tile_w+x)*3,src+x*4,3);
    }
    if(fwrite(row,1,pitch,f)!=pitch)break;
  }
  fclose(f);
}

/* Explicit F9 local evidence only: no ROM, account data or guest execution. */
void YiWs11CaptureTerrain(const char *path) {
  if (!diagnostic_valid) return;
  FILE *f = fopen(path, "wb");
  if (!f) return;
  const char magic[8] = "YIWSR11";
  bool ok = fwrite(magic,1,sizeof(magic),f)==sizeof(magic) &&
      fwrite(&diagnostic_ppu,1,sizeof(diagnostic_ppu),f)==sizeof(diagnostic_ppu) &&
      fwrite(diagnostic_wram,1,sizeof(diagnostic_wram),f)==sizeof(diagnostic_wram) &&
      fwrite(diagnostic_gsu,1,sizeof(diagnostic_gsu),f)==sizeof(diagnostic_gsu);
  if (fclose(f)) ok=false;
  printf("[WS11] F9 local terrain evidence: %s\n",ok?"saved":"write failed");
  char history_path[2048];size_t length=strlen(path),suffix=strlen("-terrain.bin");
  if(length>=suffix && !strcmp(path+length-suffix,"-terrain.bin"))
    snprintf(history_path,sizeof(history_path),"%.*s-history.csv",(int)(length-suffix),path);
  else snprintf(history_path,sizeof(history_path),"%s-history.csv",path);
  FILE *history=fopen(history_path,"wb");
  if(history) {
    fprintf(history,"frame,mode,pause,stage,camera_x,camera_y,ppu_first,ppu_last,main_first,main_last,ui_first,ui_last,gate,tile_cause,failed_line,scenery,wide,cleared,brightness_first,brightness_last,oam_match,native_luma,center_changed_lines,emu_us,draw_us,moving_columns\n");
    for(unsigned i=0;i<history_count;++i) {
      const WsFrameDiagnostic *d=&diagnostic_history[(history_next-history_count+i)%180];
      fprintf(history,"%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
          d->frame,d->mode,d->pause,d->stage,d->camera_x,d->camera_y,d->ppu_first,d->ppu_last,
          d->main_first,d->main_last,d->ui_first,d->ui_last,d->gate,d->tile_cause,d->failed_line,d->scenery,d->wide,d->cleared,d->brightness_first,d->brightness_last,d->oam_match,d->native_luma,d->center_changed_lines,d->emu_us,d->draw_us,d->moving_columns);
    }
    fclose(history);
  }
  size_t base=length>=suffix && !strcmp(path+length-suffix,"-terrain.bin")?length-suffix:length;
  snprintf(history_path,sizeof(history_path),"%.*s-history-native.bmp",(int)base,path);write_history_sheet(history_path,false);
  snprintf(history_path,sizeof(history_path),"%.*s-history-wide.bmp",(int)base,path);write_history_sheet(history_path,true);


}
