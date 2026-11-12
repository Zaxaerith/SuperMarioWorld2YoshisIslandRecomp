/*
 * Super Mario World 2: Yoshi's Island (SNESRecomp Native AOT Desktop Player)
 * Real-time SDL2 interactive host: PAL/NTSC gameplay, authentic PPU/APU, SuperFX GSU,
 * native recomp-ui launcher GUI, single-player controller mapping and SRAM saves.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "game_rtl.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "snes/snes.h"
#include "snes/ppu.h"
#include "snes/cart.h"
#include "snes/superfx.h"
#include "snes/apu.h"
#include "snes/dsp.h"
#include "snes/interp_bridge.h"

#ifdef RECOMP_LAUNCHER
#include "recomp_launcher.h"
#include "launcher_profile.h"
#include "launcher_binds.h"
#include "launcher_cache.h"
#include "host_paths.h"
#include "keybinds.h"
#include "desktop/display_aspect.h"
#include "desktop/launcher_video.h"
#endif

#ifdef _WIN32
#include <windows.h>
typedef MMRESULT (WINAPI *timeBeginPeriod_t)(UINT uPeriod);
typedef MMRESULT (WINAPI *timeEndPeriod_t)(UINT uPeriod);
static timeBeginPeriod_t pfn_timeBeginPeriod = NULL;
static timeEndPeriod_t pfn_timeEndPeriod = NULL;
static void init_timer_precision(void) {
  HMODULE h = LoadLibraryA("winmm.dll");
  if (h) {
    pfn_timeBeginPeriod = (timeBeginPeriod_t)GetProcAddress(h, "timeBeginPeriod");
    pfn_timeEndPeriod = (timeEndPeriod_t)GetProcAddress(h, "timeEndPeriod");
    if (pfn_timeBeginPeriod) pfn_timeBeginPeriod(1);
  }
}
static void cleanup_timer_precision(void) {
  if (pfn_timeEndPeriod) pfn_timeEndPeriod(1);
}
#endif

struct SpcPlayer *g_spc_player = NULL;
bool g_new_ppu = true;

#define ROM_PAYLOAD_SIZE YI_ROM_SIZE
static YiRomProfile s_rom_profile = YI_ROM_UNSUPPORTED;

#ifdef RECOMP_LAUNCHER
static RecompLauncherCSettings g_settings;
static char g_config_path[1024] = "config.ini";
static char g_keybinds_path[1024] = "keybinds.ini";
static char g_active_sram_path[1024] = "saves/save.srm";
#endif

static SDL_mutex *s_apu_mutex = NULL;
void RtlApuLock(void) {
  if (s_apu_mutex) SDL_LockMutex(s_apu_mutex);
}
void RtlApuUnlock(void) {
  if (s_apu_mutex) SDL_UnlockMutex(s_apu_mutex);
}

#define HOST_AUDIO_PREFILL 2136u
static bool s_audio_primed = false;
static int g_frames_per_block = 534;
static int g_audio_channels = 2;
static uint8_t *g_audiobuffer = NULL;
static uint8_t *g_audiobuffer_cur = NULL;
static uint8_t *g_audiobuffer_end = NULL;

static void SDLCALL AudioCallback(void *userdata, Uint8 *stream, int len) {
  (void)userdata;
  if (!s_apu_mutex || !g_audiobuffer || SDL_LockMutex(s_apu_mutex) != 0) {
    memset(stream, 0, len);
    return;
  }

  Uint8 *orig_stream = stream;
  int orig_len = len;

  while (len != 0) {
    if (g_audiobuffer_end - g_audiobuffer_cur == 0) {
      uint32_t available = (g_snes && g_snes->apu && g_snes->apu->dsp)
          ? dsp_available(g_snes->apu->dsp) : 0;
      if (!s_audio_primed && available < HOST_AUDIO_PREFILL) {
        memset(g_audiobuffer, 0, g_frames_per_block * g_audio_channels * sizeof(int16_t));
      } else {
        s_audio_primed = true;
        RtlRenderAudio((int16_t *)g_audiobuffer, g_frames_per_block, g_audio_channels);
        if (g_snes && g_snes->apu && g_snes->apu->dsp && dsp_available(g_snes->apu->dsp) < 4)
          s_audio_primed = false;
      }
      g_audiobuffer_cur = g_audiobuffer;
      g_audiobuffer_end = g_audiobuffer + g_frames_per_block * g_audio_channels * sizeof(int16_t);
    }
    int n = (len < (int)(g_audiobuffer_end - g_audiobuffer_cur))
        ? len : (int)(g_audiobuffer_end - g_audiobuffer_cur);
    memcpy(stream, g_audiobuffer_cur, n);
    g_audiobuffer_cur += n;
    stream += n;
    len -= n;
  }

#ifdef RECOMP_LAUNCHER
  if (!g_settings.enable_audio || g_settings.volume <= 0) {
    memset(orig_stream, 0, orig_len);
  } else if (g_settings.volume < 100) {
    int16_t *samples = (int16_t *)orig_stream;
    int count = orig_len / (int)sizeof(int16_t);
    int vol = g_settings.volume;
    for (int i = 0; i < count; i++) {
      samples[i] = (int16_t)(((int32_t)samples[i] * vol) / 100);
    }
  }
#endif

  SDL_UnlockMutex(s_apu_mutex);
}

void debug_on_block_enter(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { (void)a; (void)b; (void)c; (void)d; }
void debug_on_wram_write_byte(uint32_t a, uint8_t b, uint8_t c) { (void)a; (void)b; (void)c; }
void debug_on_wram_write_word(uint32_t a, uint16_t b, uint16_t c) { (void)a; (void)b; (void)c; }
void NORETURN Die(const char *s) {
  fprintf(stderr, "FATAL: %s\n", s ? s : "Unknown fatal error");
#if defined(_WIN32)
  SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Super Mario World 2: Yoshi's Island", s ? s : "Fatal Error", NULL);
#endif
  exit(1);
}

/* SNES Joypad Buttons */
enum {
  BTN_B      = 1 << 0,
  BTN_Y      = 1 << 1,
  BTN_SELECT = 1 << 2,
  BTN_START  = 1 << 3,
  BTN_UP     = 1 << 4,
  BTN_DOWN   = 1 << 5,
  BTN_LEFT   = 1 << 6,
  BTN_RIGHT  = 1 << 7,
  BTN_A      = 1 << 8,
  BTN_X      = 1 << 9,
  BTN_L      = 1 << 10,
  BTN_R      = 1 << 11,
};

static uint32_t s_pad_state = 0;
static SDL_GameController *s_controller = NULL;

static uint32_t poll_controller_with_deadzone(SDL_GameController *ctrl, int deadzone_pct) {
  if (!ctrl) return 0;
  uint32_t p = 0;
  if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_DPAD_UP))    p |= BTN_UP;
  if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  p |= BTN_DOWN;
  if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  p |= BTN_LEFT;
  if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) p |= BTN_RIGHT;

  int threshold = 3276; /* default 10% */
  if (deadzone_pct > 0 && deadzone_pct <= 100)
    threshold = (32767 * deadzone_pct) / 100;

  int16_t ax = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_LEFTX);
  int16_t ay = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_LEFTY);
  if (ax < -threshold) p |= BTN_LEFT;
  if (ax > threshold)  p |= BTN_RIGHT;
  if (ay < -threshold) p |= BTN_UP;
  if (ay > threshold)  p |= BTN_DOWN;

  /* SNES B is bottom button: Xbox A (Jump / Flutter) */
  if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_A)) p |= BTN_B;
  /* SNES A is right button: Xbox B (Throw Egg / Aim) */
  if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_B)) p |= BTN_A;
  /* SNES Y is left button: Xbox X (Tongue / Eat / Spit) */
  if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_X)) p |= BTN_Y;
  /* SNES X is top button: Xbox Y (Lock Aim) */
  if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_Y)) p |= BTN_X;

  if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_LEFTSHOULDER))  p |= BTN_L;
  if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) p |= BTN_R;
  if (SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000)  p |= BTN_L;
  if (SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000) p |= BTN_R;

  if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_START)) p |= BTN_START;
  if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_BACK))  p |= BTN_SELECT;

  return p;
}

static void refresh_controller_bindings(void) {
  int num_joysticks = SDL_NumJoysticks();
#ifdef RECOMP_LAUNCHER
  for (int j = 0; j < num_joysticks; j++) {
    if (!SDL_IsGameController(j)) continue;
    char guid_str[64] = {0};
    SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(j), guid_str, sizeof(guid_str));
    if (g_settings.player_src[0] == 2 && g_settings.player_gamepad_guid[0][0] &&
        strcmp(guid_str, g_settings.player_gamepad_guid[0]) == 0 && !s_controller) {
      s_controller = SDL_GameControllerOpen(j);
      if (s_controller) {
        printf("[Input] Gamepad bound by GUID: %s\n", SDL_GameControllerName(s_controller));
        return;
      }
    }
  }
#endif

  for (int j = 0; j < num_joysticks; j++) {
    if (!SDL_IsGameController(j)) continue;
    if (s_controller) {
      SDL_Joystick *joy = SDL_GameControllerGetJoystick(s_controller);
      if (joy && SDL_JoystickInstanceID(joy) == SDL_JoystickGetDeviceInstanceID(j))
        return;
    }
    if (!s_controller) {
      s_controller = SDL_GameControllerOpen(j);
      if (s_controller) {
        printf("[Input] Gamepad connected: %s\n", SDL_GameControllerName(s_controller));
        return;
      }
    }
  }
}

static void update_keyboard(const uint8_t *keys) {
  uint32_t p1 = 0;

#ifdef RECOMP_LAUNCHER
  int src = g_settings.player_src[0];
  int dz = g_settings.deadzone[0];

  /* 0: Auto/Both, 1: Keyboard, 2: Gamepad */
  if (src == 1) {
    p1 = keybinds_read_player_runner(keys, 1);
    /* Alternative keys for default binds */
    if (keybinds_get_button(1, 1) == SDL_SCANCODE_J && keys[SDL_SCANCODE_SPACE]) p1 |= BTN_B;
    if (keybinds_get_button(1, 3) == SDL_SCANCODE_K && keys[SDL_SCANCODE_Z])     p1 |= BTN_Y;
    if (keybinds_get_button(1, 0) == SDL_SCANCODE_L && keys[SDL_SCANCODE_X])     p1 |= BTN_A;
    if (keybinds_get_button(1, 2) == SDL_SCANCODE_I && keys[SDL_SCANCODE_C])     p1 |= BTN_X;
    if (keybinds_get_button(1, 8) == SDL_SCANCODE_UP && keys[SDL_SCANCODE_W])    p1 |= BTN_UP;
    if (keybinds_get_button(1, 9) == SDL_SCANCODE_DOWN && keys[SDL_SCANCODE_S])  p1 |= BTN_DOWN;
    if (keybinds_get_button(1, 10) == SDL_SCANCODE_LEFT && keys[SDL_SCANCODE_A]) p1 |= BTN_LEFT;
    if (keybinds_get_button(1, 11) == SDL_SCANCODE_RIGHT && keys[SDL_SCANCODE_D])p1 |= BTN_RIGHT;
    if (keybinds_get_button(1, 7) == SDL_SCANCODE_TAB && keys[SDL_SCANCODE_RSHIFT]) p1 |= BTN_SELECT;
  } else if (src == 2) {
    if (s_controller) {
      p1 = poll_controller_with_deadzone(s_controller, dz);
    } else {
      /* Safe keyboard fallback */
      p1 = keybinds_read_player_runner(keys, 1);
      if (keybinds_get_button(1, 1) == SDL_SCANCODE_J && keys[SDL_SCANCODE_SPACE]) p1 |= BTN_B;
      if (keybinds_get_button(1, 3) == SDL_SCANCODE_K && keys[SDL_SCANCODE_Z])     p1 |= BTN_Y;
      if (keybinds_get_button(1, 0) == SDL_SCANCODE_L && keys[SDL_SCANCODE_X])     p1 |= BTN_A;
      if (keybinds_get_button(1, 2) == SDL_SCANCODE_I && keys[SDL_SCANCODE_C])     p1 |= BTN_X;
      if (keybinds_get_button(1, 8) == SDL_SCANCODE_UP && keys[SDL_SCANCODE_W])    p1 |= BTN_UP;
      if (keybinds_get_button(1, 9) == SDL_SCANCODE_DOWN && keys[SDL_SCANCODE_S])  p1 |= BTN_DOWN;
      if (keybinds_get_button(1, 10) == SDL_SCANCODE_LEFT && keys[SDL_SCANCODE_A]) p1 |= BTN_LEFT;
      if (keybinds_get_button(1, 11) == SDL_SCANCODE_RIGHT && keys[SDL_SCANCODE_D])p1 |= BTN_RIGHT;
      if (keybinds_get_button(1, 7) == SDL_SCANCODE_TAB && keys[SDL_SCANCODE_RSHIFT]) p1 |= BTN_SELECT;
    }
  } else {
    /* Auto: read keyboard and gamepad combined */
    p1 = keybinds_read_player_runner(keys, 1);
    if (keybinds_get_button(1, 1) == SDL_SCANCODE_J && keys[SDL_SCANCODE_SPACE]) p1 |= BTN_B;
    if (keybinds_get_button(1, 3) == SDL_SCANCODE_K && keys[SDL_SCANCODE_Z])     p1 |= BTN_Y;
    if (keybinds_get_button(1, 0) == SDL_SCANCODE_L && keys[SDL_SCANCODE_X])     p1 |= BTN_A;
    if (keybinds_get_button(1, 2) == SDL_SCANCODE_I && keys[SDL_SCANCODE_C])     p1 |= BTN_X;
    if (keybinds_get_button(1, 8) == SDL_SCANCODE_UP && keys[SDL_SCANCODE_W])    p1 |= BTN_UP;
    if (keybinds_get_button(1, 9) == SDL_SCANCODE_DOWN && keys[SDL_SCANCODE_S])  p1 |= BTN_DOWN;
    if (keybinds_get_button(1, 10) == SDL_SCANCODE_LEFT && keys[SDL_SCANCODE_A]) p1 |= BTN_LEFT;
    if (keybinds_get_button(1, 11) == SDL_SCANCODE_RIGHT && keys[SDL_SCANCODE_D])p1 |= BTN_RIGHT;
    if (keybinds_get_button(1, 7) == SDL_SCANCODE_TAB && keys[SDL_SCANCODE_RSHIFT]) p1 |= BTN_SELECT;
    p1 |= poll_controller_with_deadzone(s_controller, dz);
  }
#else
  if (keys[SDL_SCANCODE_UP]    || keys[SDL_SCANCODE_W]) p1 |= BTN_UP;
  if (keys[SDL_SCANCODE_DOWN]  || keys[SDL_SCANCODE_S]) p1 |= BTN_DOWN;
  if (keys[SDL_SCANCODE_LEFT]  || keys[SDL_SCANCODE_A]) p1 |= BTN_LEFT;
  if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) p1 |= BTN_RIGHT;
  if (keys[SDL_SCANCODE_J]     || keys[SDL_SCANCODE_SPACE]) p1 |= BTN_B;
  if (keys[SDL_SCANCODE_K]     || keys[SDL_SCANCODE_Z])     p1 |= BTN_Y;
  if (keys[SDL_SCANCODE_L]     || keys[SDL_SCANCODE_X])     p1 |= BTN_A;
  if (keys[SDL_SCANCODE_I]     || keys[SDL_SCANCODE_C])     p1 |= BTN_X;
  if (keys[SDL_SCANCODE_Q])                                 p1 |= BTN_L;
  if (keys[SDL_SCANCODE_E])                                 p1 |= BTN_R;
  if (keys[SDL_SCANCODE_RETURN])                            p1 |= BTN_START;
  if (keys[SDL_SCANCODE_TAB]   || keys[SDL_SCANCODE_RSHIFT]) p1 |= BTN_SELECT;
  p1 |= poll_controller_with_deadzone(s_controller, 10);
#endif

  s_pad_state = p1;
}

/* Strict ROM verification and loading */
static uint8_t* load_and_verify_rom(const char *path, size_t *out_size, char *err_msg, size_t err_cap) {
  if (!path || !path[0]) {
    if (err_msg && err_cap) snprintf(err_msg, err_cap, "No ROM path provided.");
    return NULL;
  }
  FILE *f = fopen(path, "rb");
  if (!f) {
    if (err_msg && err_cap) snprintf(err_msg, err_cap, "Failed to open file: %s", path);
    return NULL;
  }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  rewind(f);

  if (sz < (long)ROM_PAYLOAD_SIZE) {
    if (err_msg && err_cap)
      snprintf(err_msg, err_cap, "File size (%ld bytes) is smaller than expected (%u bytes).", sz, ROM_PAYLOAD_SIZE);
    fclose(f);
    return NULL;
  }

  size_t header_skip = 0;
  if ((size_t)sz == ROM_PAYLOAD_SIZE + 512) {
    header_skip = 512;
  } else if ((size_t)sz != ROM_PAYLOAD_SIZE) {
    if (err_msg && err_cap)
      snprintf(err_msg, err_cap, "Invalid file size %ld (expected %u headerless or %u headered).",
               sz, ROM_PAYLOAD_SIZE, ROM_PAYLOAD_SIZE + 512);
    fclose(f);
    return NULL;
  }

  uint8_t *payload = malloc(ROM_PAYLOAD_SIZE);
  if (!payload || fseek(f, (long)header_skip, SEEK_SET) != 0 ||
      fread(payload, 1, ROM_PAYLOAD_SIZE, f) != ROM_PAYLOAD_SIZE) {
    free(payload);
    fclose(f);
    if (err_msg && err_cap) snprintf(err_msg, err_cap, "Failed to read ROM payload.");
    return NULL;
  }
  fclose(f);

  s_rom_profile = YiIdentifyRom(payload, ROM_PAYLOAD_SIZE);
  if (s_rom_profile == YI_ROM_UNSUPPORTED) {
    if (err_msg && err_cap)
      snprintf(err_msg, err_cap, "Expected USA v1.0 or Europe (En,Fr,De) Rev 1.");
    free(payload);
    return NULL;
  }

  *out_size = ROM_PAYLOAD_SIZE;
  return payload;
}

extern uint8_t *g_sram;
extern int g_sram_size;

static void ensure_dir_for_file(const char *filepath) {
  char buf[1024];
  snprintf(buf, sizeof(buf), "%s", filepath);
  char *p = strrchr(buf, '/');
  if (!p) p = strrchr(buf, '\\');
  if (p) {
    *p = '\0';
#ifdef _WIN32
    CreateDirectoryA(buf, NULL);
#else
    mkdir(buf, 0755);
#endif
  }
}

static void resolve_sram_paths(char *active_path, size_t cap) {
  /* Candidate 1: saves/save.srm in exe dir */
  FILE *f1 = fopen("saves/save.srm", "rb");
  if (f1) {
    fclose(f1);
    snprintf(active_path, cap, "saves/save.srm");
    return;
  }
  /* Candidate 2: save.srm in exe dir */
  FILE *f2 = fopen("save.srm", "rb");
  if (f2) {
    fclose(f2);
    snprintf(active_path, cap, "save.srm");
    return;
  }
  /* Candidate 3: ../saves/save.srm (e.g. running from build/ with root saves) */
  FILE *f3 = fopen("../saves/save.srm", "rb");
  if (f3) {
    fclose(f3);
    ensure_dir_for_file("saves/save.srm");
    FILE *src = fopen("../saves/save.srm", "rb");
    FILE *dst = fopen("saves/save.srm", "wb");
    if (src && dst) {
      char buf[4096];
      size_t n;
      while ((n = fread(buf, 1, sizeof(buf), src)) > 0) fwrite(buf, 1, n, dst);
      printf("[SRAM] Migrated existing save from ../saves/save.srm to saves/save.srm\n");
    }
    if (src) fclose(src);
    if (dst) fclose(dst);
    snprintf(active_path, cap, "saves/save.srm");
    return;
  }
  /* Default to portable saves/save.srm and create folder */
  ensure_dir_for_file("saves/save.srm");
  snprintf(active_path, cap, "saves/save.srm");
}

static void load_sram(const char *path) {
  if (!g_sram || g_sram_size <= 0) return;
  FILE *f = fopen(path, "rb");
  if (f) {
    size_t n = fread(g_sram, 1, g_sram_size, f);
    fclose(f);
    printf("[SRAM] Loaded %zu bytes from %s\n", n, path);
  }
}

static void save_sram(const char *path) {
  if (!g_sram || g_sram_size <= 0) return;
  ensure_dir_for_file(path);
  FILE *f = fopen(path, "wb");
  if (f) {
    size_t written = fwrite(g_sram, 1, g_sram_size, f);
    fclose(f);
    if (written != (size_t)g_sram_size) {
      fprintf(stderr, "[SRAM] Warning: Short write to %s (%zu/%d bytes)\n", path, written, g_sram_size);
    }
  } else {
    fprintf(stderr, "[SRAM] Error: Failed to open %s for writing!\n", path);
  }
}

#ifdef RECOMP_LAUNCHER
static int ini_read_int(const char *path, const char *section, const char *key, int *out_val) {
  FILE *f = fopen(path, "r");
  if (!f) return 0;
  char line[256];
  char cur_sect[128] = {0};
  int found = 0;
  while (fgets(line, sizeof(line), f)) {
    char *s = line;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == ';' || *s == '#' || *s == '\r' || *s == '\n' || *s == '\0') continue;
    if (*s == '[') {
      char *end = strchr(s, ']');
      if (end) {
        size_t slen = end - (s + 1);
        if (slen >= sizeof(cur_sect)) slen = sizeof(cur_sect) - 1;
        memcpy(cur_sect, s + 1, slen);
        cur_sect[slen] = '\0';
      }
      continue;
    }
    if (strcmp(cur_sect, section) != 0) continue;
    char *eq = strchr(s, '=');
    if (!eq) continue;
    *eq = '\0';
    char *k = s;
    char *v = eq + 1;
    while (*k && (k[strlen(k) - 1] == ' ' || k[strlen(k) - 1] == '\t')) k[strlen(k) - 1] = '\0';
    while (*v == ' ' || *v == '\t') v++;
    if (strcmp(k, key) == 0) {
      *out_val = (int)strtol(v, NULL, 10);
      found = 1;
      break;
    }
  }
  fclose(f);
  return found;
}

static int ini_read_string(const char *path, const char *section, const char *key, char *out_str, size_t max_len) {
  FILE *f = fopen(path, "r");
  if (!f) return 0;
  char line[256];
  char cur_sect[128] = {0};
  int found = 0;
  while (fgets(line, sizeof(line), f)) {
    char *s = line;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == ';' || *s == '#' || *s == '\r' || *s == '\n' || *s == '\0') continue;
    if (*s == '[') {
      char *end = strchr(s, ']');
      if (end) {
        size_t slen = end - (s + 1);
        if (slen >= sizeof(cur_sect)) slen = sizeof(cur_sect) - 1;
        memcpy(cur_sect, s + 1, slen);
        cur_sect[slen] = '\0';
      }
      continue;
    }
    if (strcmp(cur_sect, section) != 0) continue;
    char *eq = strchr(s, '=');
    if (!eq) continue;
    *eq = '\0';
    char *k = s;
    char *v = eq + 1;
    while (*k && (k[strlen(k) - 1] == ' ' || k[strlen(k) - 1] == '\t')) k[strlen(k) - 1] = '\0';
    while (*v == ' ' || *v == '\t') v++;
    char *vend = v + strlen(v);
    while (vend > v && (*(vend - 1) == ' ' || *(vend - 1) == '\t' || *(vend - 1) == '\r' || *(vend - 1) == '\n')) {
      *(vend - 1) = '\0';
      vend--;
    }
    if (strcmp(k, key) == 0) {
      snprintf(out_str, max_len, "%s", v);
      found = 1;
      break;
    }
  }
  fclose(f);
  return found;
}

static void load_launcher_settings(RecompLauncherCSettings *settings, const char *cfg_path) {
  int val = 0;
  char str[128];
  if (ini_read_int(cfg_path, "General", "SkipLauncher", &val))
    settings->skip_launcher = val != 0;

  if (ini_read_int(cfg_path, "Graphics", "WindowScale", &val) && val >= 1 && val <= 6)
    settings->window_scale = val;
  if (ini_read_int(cfg_path, "Graphics", "Fullscreen", &val) && val >= 0 && val <= 2)
    settings->fullscreen = val;
  if (ini_read_int(cfg_path, "Graphics", "LinearFiltering", &val))
    settings->linear_filter = val != 0;
  if (ini_read_string(cfg_path, "Graphics", "DisplayAspect", str, sizeof(str))) {
    uint8_t asp = 0;
    if (SnesDisplayAspect_Parse(str, &asp)) settings->aspect_index = asp;
  }

  if (ini_read_int(cfg_path, "Sound", "Volume", &val) && val >= 0 && val <= 100)
    settings->volume = val;
  if (ini_read_int(cfg_path, "Sound", "AudioFreq", &val) && val >= 8000 && val <= 96000)
    settings->audio_freq = val;

  if (ini_read_int(cfg_path, "Controller", "SourceP1", &val) && val >= 0 && val <= 2)
    settings->player_src[0] = val;
  if (ini_read_string(cfg_path, "Controller", "GuidP1", str, sizeof(str)))
    snprintf(settings->player_gamepad_guid[0], sizeof(settings->player_gamepad_guid[0]), "%s", str);
  if (ini_read_int(cfg_path, "Controller", "DeadzoneP1", &val) && val >= 0 && val <= 100)
    settings->deadzone[0] = val;
}

static void save_launcher_settings(const RecompLauncherCSettings *settings, const char *cfg_path) {
  char buf[64];
  snprintf(buf, sizeof(buf), "%d", settings->skip_launcher ? 1 : 0);
  launcher_ini_kv_write(cfg_path, "General", "SkipLauncher", buf);

  snprintf(buf, sizeof(buf), "%d", settings->window_scale);
  launcher_ini_kv_write(cfg_path, "Graphics", "WindowScale", buf);
  snprintf(buf, sizeof(buf), "%d", settings->fullscreen);
  launcher_ini_kv_write(cfg_path, "Graphics", "Fullscreen", buf);
  snprintf(buf, sizeof(buf), "%d", settings->linear_filter ? 1 : 0);
  launcher_ini_kv_write(cfg_path, "Graphics", "LinearFiltering", buf);
  snprintf(buf, sizeof(buf), "%s", SnesDisplayAspect_Name(settings->aspect_index));
  launcher_ini_kv_write(cfg_path, "Graphics", "DisplayAspect", buf);

  snprintf(buf, sizeof(buf), "%d", settings->volume);
  launcher_ini_kv_write(cfg_path, "Sound", "Volume", buf);
  snprintf(buf, sizeof(buf), "%d", settings->audio_freq);
  launcher_ini_kv_write(cfg_path, "Sound", "AudioFreq", buf);

  snprintf(buf, sizeof(buf), "%d", settings->player_src[0]);
  launcher_ini_kv_write(cfg_path, "Controller", "SourceP1", buf);
  launcher_ini_kv_write(cfg_path, "Controller", "GuidP1", settings->player_gamepad_guid[0]);
  snprintf(buf, sizeof(buf), "%d", settings->deadzone[0]);
  launcher_ini_kv_write(cfg_path, "Controller", "DeadzoneP1", buf);
}

static int run_launcher_flow(int argc, char **argv, const char *cli_rom_override, char *out_rom_path, size_t out_rom_cap, RecompLauncherCSettings *settings) {
  memset(settings, 0, sizeof(*settings));
  settings->window_scale = 3;
  settings->fullscreen = 0;
  settings->linear_filter = 1;
  settings->aspect_index = 0; /* 4:3 CRT default */
  settings->enable_audio = 1;
  settings->audio_freq = 44100;
  settings->volume = 100;
  settings->player_src[0] = 1; /* Keyboard */
  settings->deadzone[0] = 10;

  snesrecomp_exe_dir_path("config.ini", g_config_path, sizeof(g_config_path));
  snesrecomp_exe_dir_path("keybinds.ini", g_keybinds_path, sizeof(g_keybinds_path));

  load_launcher_settings(settings, g_config_path);

  bool force_launcher = false;
  const char *explicit_rom = (cli_rom_override && cli_rom_override[0]) ? cli_rom_override : NULL;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--launcher") == 0) {
      force_launcher = true;
    } else if (strcmp(argv[i], "--rom") == 0 && i + 1 < argc) {
      explicit_rom = argv[++i];
    } else if (argv[i][0] != '-' && !explicit_rom) {
      explicit_rom = argv[i];
    }
  }

  /* Command line ROM specified and not forcing launcher */
  if (explicit_rom && !force_launcher) {
    size_t sz = 0;
    char err[256];
    uint8_t *rom = load_and_verify_rom(explicit_rom, &sz, err, sizeof(err));
    if (rom) {
      free(rom);
      snprintf(out_rom_path, out_rom_cap, "%s", explicit_rom);
      printf("[ROM] Verified from command line: %s\n", explicit_rom);
      return 0;
    }
    fprintf(stderr, "[ROM] Explicit ROM rejected (%s), opening launcher...\n", err);
  }

  /* Remembered ROM from rom.cfg */
  char remembered_rom[1024] = {0};
  snesrecomp_rom_cache_read(remembered_rom, sizeof(remembered_rom));

  if (settings->skip_launcher && !force_launcher && remembered_rom[0]) {
    size_t sz = 0;
    char err[256];
    uint8_t *rom = load_and_verify_rom(remembered_rom, &sz, err, sizeof(err));
    if (rom) {
      free(rom);
      snprintf(out_rom_path, out_rom_cap, "%s", remembered_rom);
      printf("[ROM] Verified from remembered cache (Skip Launcher active): %s\n", remembered_rom);
      return 0;
    }
    fprintf(stderr, "[ROM] Remembered ROM invalid (%s), opening launcher...\n", err);
  }

  /* Build RecompLauncherCGameInfo */
  RecompLauncherCGameInfo game;
  memset(&game, 0, sizeof(game));
  launcher_profile_apply("snes", &game);
  game.name = "Super Mario World 2: Yoshi's Island";
  game.region = "USA / Europe Rev 1";
  game.known_sha256 = yi_known_sha256;
  game.num_known_sha256 = 2;
  game.has_expected_crc = 0;
  game.expected_crc = 0;
  game.num_players = 1;
  game.widescreen_supported = 0;

  static const char *const kAspectLabels[] = {
      "4:3 (CRT)",
      "8:7 (Square pixels)",
      "1:1 (Square frame)",
  };
  game.aspect_labels = kAspectLabels;
  game.num_aspect_labels = 3;
  game.aspect_setting_label = "Aspect ratio";
  game.aspect_setting_help = "4:3 recreates standard CRT TV. 8:7 uses square pixels. 1:1 presents a square frame.";
  game.has_shader = 0;
  game.sram_path = g_active_sram_path;
  game.rom_cache_path = "rom.cfg";
  game.config_path = g_config_path;
  game.keybinds_path = g_keybinds_path;

  char initial_rom[1024] = {0};
  if (explicit_rom) {
    snprintf(initial_rom, sizeof(initial_rom), "%s", explicit_rom);
  } else if (remembered_rom[0]) {
    snprintf(initial_rom, sizeof(initial_rom), "%s", remembered_rom);
  } else {
    const char *candidates[] = {
        "Super Mario World 2 - Yoshi's Island (USA).sfc",
        "Super Mario World 2 - Yoshi's Island (USA).smc",
        "../Super Mario World 2 - Yoshi's Island (USA).sfc",
        "roms/Super Mario World 2 - Yoshi's Island (USA).sfc",
        "D:/MYAPPLICA/1GAMAMA/SFCGAMES/Super Mario World 2 - Yoshi's Island (USA).sfc"
    };
    for (size_t i = 0; i < sizeof(candidates)/sizeof(candidates[0]); i++) {
      FILE *probe = fopen(candidates[i], "rb");
      if (probe) {
        fclose(probe);
        snprintf(initial_rom, sizeof(initial_rom), "%s", candidates[i]);
        break;
      }
    }
  }

  char assets_dir[1024] = ".";
  snesrecomp_exe_dir_path("", assets_dir, sizeof(assets_dir));

  printf("[Launcher] Opening native Yoshi's Island launcher%s...\n", force_launcher ? " (--launcher)" : "");
  int action = recomp_launcher_run_window(
      "Super Mario World 2: Yoshi's Island \xE2\x80\x94 Launcher",
      settings,
      &game,
      assets_dir,
      initial_rom,
      out_rom_path,
      out_rom_cap
  );

  save_launcher_settings(settings, g_config_path);

  if (action == 1) {
    printf("[Launcher] User exited launcher.\n");
    return -1;
  }
  if (action == 0 && out_rom_path[0]) {
    snesrecomp_rom_cache_write(out_rom_path);
    return 0;
  }
  if (initial_rom[0]) {
    snprintf(out_rom_path, out_rom_cap, "%s", initial_rom);
    snesrecomp_rom_cache_write(out_rom_path);
    return 0;
  }
  return -1;
}
#endif

static void print_usage(const char *prog_name) {
  printf("Super Mario World 2: Yoshi's Island Recomp\n\n");
  printf("Usage: %s [options] [rom_path]\n\n", prog_name ? prog_name : "YoshisIslandRecomp.exe");
  printf("Options:\n");
  printf("  --launcher       Force opening the native recomp-ui launcher\n");
  printf("  --rom <path>     Specify path to Yoshi's Island ROM (.sfc / .smc)\n");
  printf("  -h, --help       Display this help message and exit\n\n");
  printf("Supported ROM:\n");
  printf("  Super Mario World 2: Yoshi's Island (USA), Rev 1.0\n");
  printf("  Headerless (.sfc): 2,097,152 bytes\n");
  printf("  Headered   (.smc): 2,097,664 bytes (512-byte Copier Header)\n");
  printf("  SHA-256: 9b4957466798bbdb5b43a450bbb60b2591ae81d95b891430f62d53ca62e8bc7b\n");
  printf("  CRC32:   D138F224\n");
}

int main(int argc, char **argv) {
#ifdef _WIN32
  HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
  DWORD ft = (hOut && hOut != INVALID_HANDLE_VALUE) ? GetFileType(hOut) : FILE_TYPE_UNKNOWN;
  if (ft == FILE_TYPE_UNKNOWN || ft == 0) {
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
      freopen("CONOUT$", "w", stdout);
      freopen("CONOUT$", "w", stderr);
      freopen("CONIN$", "r", stdin);
    }
  }
  init_timer_precision();
#endif

  /* Check for help */
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      print_usage(argv[0]);
      return 0;
    }
  }

  char cli_rom_abs[1024] = {0};
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--rom") == 0 && i + 1 < argc) {
#ifdef RECOMP_LAUNCHER
      snesrecomp_abspath(argv[i + 1], cli_rom_abs, sizeof(cli_rom_abs));
#else
      snprintf(cli_rom_abs, sizeof(cli_rom_abs), "%s", argv[i + 1]);
#endif
      break;
    } else if (argv[i][0] != '-') {
#ifdef RECOMP_LAUNCHER
      snesrecomp_abspath(argv[i], cli_rom_abs, sizeof(cli_rom_abs));
#else
      snprintf(cli_rom_abs, sizeof(cli_rom_abs), "%s", argv[i]);
#endif
      break;
    }
  }

#ifdef RECOMP_LAUNCHER
  snesrecomp_anchor_to_exe_dir();
  resolve_sram_paths(g_active_sram_path, sizeof(g_active_sram_path));
#endif

  printf("=================================================================\n");
  printf("  Super Mario World 2: Yoshi's Island (SNESRecomp Native AOT)   \n");
  printf("=================================================================\n");

  char chosen_rom_path[1024] = {0};
#ifdef RECOMP_LAUNCHER
  int flow = run_launcher_flow(argc, argv, cli_rom_abs, chosen_rom_path, sizeof(chosen_rom_path), &g_settings);
  if (flow != 0) {
    return 0; /* Clean quit from launcher */
  }
  keybinds_init(g_keybinds_path);
#else
  if (cli_rom_abs[0]) snprintf(chosen_rom_path, sizeof(chosen_rom_path), "%s", cli_rom_abs);
  else if (argc >= 2) snprintf(chosen_rom_path, sizeof(chosen_rom_path), "%s", argv[1]);
  else snprintf(chosen_rom_path, sizeof(chosen_rom_path), "%s", "Super Mario World 2 - Yoshi's Island (USA).sfc");
#endif

  size_t rom_size = 0;
  char err_msg[512] = {0};
  uint8_t *rom_data = load_and_verify_rom(chosen_rom_path, &rom_size, err_msg, sizeof(err_msg));
  if (!rom_data) {
    char full_err[1024];
    snprintf(full_err, sizeof(full_err),
             "ROM Error: %s\n\nPath: %s\n\nExpected: USA v1.0 or Europe (En,Fr,De) Rev 1",
             err_msg[0] ? err_msg : "Verification failed", chosen_rom_path);
    fprintf(stderr, "%s\n", full_err);
    Die(full_err);
    return 1;
  }
  printf("[ROM] Verified successfully (%zu bytes payload): %s\n", rom_size, chosen_rom_path);

  RtlRegisterGame(&kGameInfo);
  if (!SnesInit(rom_data, (int)rom_size)) {
    free(rom_data);
    Die("Failed to initialize SNES core!");
    return 2;
  }
  GameConfigureRom(s_rom_profile);
  free(rom_data);

  if (!g_snes || !g_snes->cart || !g_snes->cart->superfx) {
    fprintf(stderr, "Warning: SuperFX coprocessor was not initialized!\n");
  } else {
    printf("[Hardware] SuperFX (GSU-2) coprocessor active.\n");
  }

#ifdef RECOMP_LAUNCHER
  load_sram(g_active_sram_path);
#else
  load_sram("save.srm");
#endif

  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
    char err[256];
    snprintf(err, sizeof(err), "SDL_Init Error: %s", SDL_GetError());
    Die(err);
    return 3;
  }

  s_apu_mutex = SDL_CreateMutex();
  if (!s_apu_mutex) Die("SDL_CreateMutex failed");

  int scale = 3;
  int aspect = 0; /* 4:3 CRT default */
  bool fullscreen = false;
  bool linear_filter = true;
#ifdef RECOMP_LAUNCHER
  if (g_settings.window_scale >= 1 && g_settings.window_scale <= 6) scale = g_settings.window_scale;
  if (g_settings.aspect_index >= 0 && g_settings.aspect_index <= 2) aspect = g_settings.aspect_index;
  fullscreen = g_settings.fullscreen != 0;
  linear_filter = g_settings.linear_filter != 0;
#endif

  int pres_w = 298, pres_h = 224;
  if (aspect == 1) pres_w = 256;      /* 8:7 square pixels */
  else if (aspect == 2) pres_w = 224; /* 1:1 square frame */

  int win_w = (aspect == 0) ? ((256 * scale * 7 + 3) / 6) : (pres_w * scale);
  int win_h = pres_h * scale;

  Uint32 win_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
  if (fullscreen) {
    win_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
  }

  SDL_Window *window = SDL_CreateWindow(
    "Super Mario World 2: Yoshi's Island - Recompiled Native AOT",
    SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
    win_w, win_h,
    win_flags
  );
  if (!window) {
    char err[256];
    snprintf(err, sizeof(err), "SDL_CreateWindow Error: %s", SDL_GetError());
    Die(err);
    return 4;
  }

  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, linear_filter ? "1" : "0");
  SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");

  SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if (!renderer) renderer = SDL_CreateRenderer(window, -1, 0);
  if (!renderer) Die("SDL_CreateRenderer failed");

  SDL_RenderSetLogicalSize(renderer, pres_w, pres_h);

  SDL_Texture *texture = SDL_CreateTexture(
    renderer,
    SDL_PIXELFORMAT_BGRA32,
    SDL_TEXTUREACCESS_STREAMING,
    256, 224
  );
  if (texture) {
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
  }

  static uint8_t fb_pixels[256 * 4 * 256];
  PpuBeginDrawing(g_ppu, fb_pixels, 256 * 4, 0);

  int audio_rate = 44100;
#ifdef RECOMP_LAUNCHER
  if (g_settings.audio_freq >= 8000 && g_settings.audio_freq <= 96000)
    audio_rate = g_settings.audio_freq;
#endif

  SDL_AudioDeviceID audio_dev = 0;
  SDL_AudioSpec want, have;
  SDL_memset(&want, 0, sizeof(want));
  want.freq = audio_rate;
  want.format = AUDIO_S16;
  want.channels = 2;
  want.samples = 512;
  want.callback = AudioCallback;

  audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
  if (audio_dev > 0) {
    g_audio_channels = 2;
    RtlSetAudioOutputRate(have.freq);
    g_frames_per_block = (534 * have.freq + 32040 / 2) / 32040;
    g_audiobuffer = (uint8_t *)calloc(g_frames_per_block * g_audio_channels * sizeof(int16_t), 1);
    g_audiobuffer_cur = g_audiobuffer;
    g_audiobuffer_end = g_audiobuffer;
    SDL_PauseAudioDevice(audio_dev, 0);
    printf("[Audio] SDL2 audio output opened at %d Hz stereo (%d samples/cb)\n",
           have.freq, have.samples);
  } else {
    printf("[Audio] Audio device not available: %s\n", SDL_GetError());
  }

  refresh_controller_bindings();

  bool running = true;
  SDL_Event event;

  const uint64_t perf_freq = SDL_GetPerformanceFrequency();
  const double target_frame_cycles = (double)perf_freq / YiRomFrameRate(s_rom_profile);
  uint64_t next_frame_time = SDL_GetPerformanceCounter();

  printf("[Game] Entering %.3f Hz scanline execution loop...\n", YiRomFrameRate(s_rom_profile));

  while (running) {
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        running = false;
      } else if (event.type == SDL_KEYDOWN) {
        if (event.key.keysym.sym == SDLK_ESCAPE) {
          running = false;
        } else if (event.key.keysym.sym == SDLK_F11 ||
                   (event.key.keysym.sym == SDLK_RETURN && (event.key.keysym.mod & KMOD_ALT))) {
          static bool s_fs = false;
          s_fs = !s_fs;
          SDL_SetWindowFullscreen(window, s_fs ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        }
      } else if (event.type == SDL_CONTROLLERDEVICEADDED) {
        refresh_controller_bindings();
      } else if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
        SDL_JoystickID jid = event.cdevice.which;
        if (s_controller) {
          SDL_Joystick *joy = SDL_GameControllerGetJoystick(s_controller);
          if (joy && SDL_JoystickInstanceID(joy) == jid) {
            printf("[Input] Gamepad disconnected\n");
            SDL_GameControllerClose(s_controller);
            s_controller = NULL;
          }
        }
        refresh_controller_bindings();
      }
    }

    const uint8_t *key_states = SDL_GetKeyboardState(NULL);
    update_keyboard(key_states);

    /* Run one full hardware frame (262 scanlines, AOT dispatched + SuperFX + PPU) */
    RtlRunFrame(s_pad_state);
    GameDrawPpuFrame();

    /* Present active viewport */
    SDL_UpdateTexture(texture, NULL, fb_pixels, 256 * 4);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);

    static uint32_t s_frame_save_counter = 0;
    if (++s_frame_save_counter % 300 == 0) {
#ifdef RECOMP_LAUNCHER
      save_sram(g_active_sram_path);
#else
      save_sram("save.srm");
#endif
    }

    /* Pace frames for the selected ROM region. */
    next_frame_time += (uint64_t)target_frame_cycles;
    uint64_t now_perf = SDL_GetPerformanceCounter();
    if (now_perf > next_frame_time + (uint64_t)(target_frame_cycles * 3)) {
      next_frame_time = now_perf;
    } else {
      while (now_perf < next_frame_time) {
        double rem_ms = (double)(next_frame_time - now_perf) * 1000.0 / (double)perf_freq;
        if (rem_ms > 2.0) {
          SDL_Delay((Uint32)(rem_ms - 1.0));
        } else {
#ifdef _WIN32
          Sleep(0);
#else
          SDL_Delay(0);
#endif
        }
        now_perf = SDL_GetPerformanceCounter();
      }
    }
  }

#ifdef RECOMP_LAUNCHER
  save_sram(g_active_sram_path);
  printf("[SRAM] Saved game state to %s\n", g_active_sram_path);
#else
  save_sram("save.srm");
  printf("[SRAM] Saved game state to save.srm\n");
#endif

  if (s_controller) {
    SDL_GameControllerClose(s_controller);
    s_controller = NULL;
  }
  if (audio_dev > 0) {
    SDL_CloseAudioDevice(audio_dev);
    audio_dev = 0;
  }
  if (s_apu_mutex) {
    SDL_DestroyMutex(s_apu_mutex);
    s_apu_mutex = NULL;
  }
  if (g_audiobuffer) {
    free(g_audiobuffer);
    g_audiobuffer = NULL;
  }
  if (texture) SDL_DestroyTexture(texture);
  if (renderer) SDL_DestroyRenderer(renderer);
  if (window) SDL_DestroyWindow(window);
  SDL_Quit();

#ifdef _WIN32
  cleanup_timer_precision();
#endif
  return 0;
}

#ifdef _WIN32
/* GUI-subsystem entry (no console window). Forwards to the normal main. */
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
  (void)hInstance; (void)hPrevInstance; (void)lpCmdLine; (void)nCmdShow;
  return main(__argc, __argv);
}
#endif
