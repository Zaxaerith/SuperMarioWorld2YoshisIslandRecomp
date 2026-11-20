#include "yi_capture.h"
#include "yi_present_trace.h"
/*
 * Super Mario World 2: Yoshi's Island (SNESRecomp Native AOT Desktop Player)
 * Real-time SDL2 interactive host: PAL/NTSC gameplay, authentic PPU/APU, SuperFX GSU,
 * native recomp-ui launcher GUI, single-player controller mapping and SRAM saves.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "yi_video_effects.h"
#include "yi_save_indicator.h"
#include "yi_effect_diagnostics.h"
#include "yi_presentation.h"
#include <string.h>

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "game_rtl.h"
#include "yi_ws11.h"
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
static unsigned g_video_effect;
static SDL_Keycode g_effect_hotkey = SDLK_F7;
int YiEffectsLauncherMode(void) { return (int)g_video_effect; }
void YiEffectsLauncherSetMode(int mode) {
  if (mode < 0 || mode >= YI_EFFECT_COUNT) return;
  g_video_effect = (unsigned)mode;
  char value[16]; snprintf(value, sizeof(value), "%u", g_video_effect);
  launcher_ini_kv_write(g_config_path, "Mods", "VideoEffect", value);
}
const char *YiEffectsLauncherHotkey(void) { return SDL_GetKeyName(g_effect_hotkey); }
int YiEffectsLauncherSetHotkey(const char *name) {
  SDL_Keycode key = SDL_GetKeyFromName(name);
  if (!key || key == SDLK_ESCAPE || key == SDLK_RETURN || key == SDLK_F6 ||
      key == SDLK_F8 || key == SDLK_F9 || key == SDLK_F10 || key == SDLK_F11 ||
      key == SDLK_LCTRL || key == SDLK_RCTRL || key == SDLK_LALT || key == SDLK_RALT ||
      key == SDLK_LSHIFT || key == SDLK_RSHIFT || key == SDLK_LGUI || key == SDLK_RGUI)
    return 0;
  SDL_Scancode scan = SDL_GetScancodeFromKey(key);
  for (int button = 0; button < 12; ++button)
    if (keybinds_get_button(1, button) == scan) return 0;
  if (key > 0 && key < 128 && strchr("wasdjklizxcqe", (int)key)) return 0;
  if (key == SDLK_SPACE || key == SDLK_TAB || key == SDLK_RSHIFT) return 0;
  g_effect_hotkey = key;
  char value[24]; snprintf(value, sizeof(value), "%d", (int)key);
  launcher_ini_kv_write(g_config_path, "Mods", "EffectsKeycode", value);
  return 1;
}

static bool g_widescreen;
int YiWsLauncherEnabled(void) { return g_widescreen; }
void YiWsLauncherSetEnabled(int enabled) {
  g_widescreen = enabled != 0;
  launcher_ini_kv_write(g_config_path, "Mods", "Widescreen", g_widescreen ? "1" : "0");
}
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
      snprintf(err_msg, err_cap, "Expected USA or Europe (En,Fr,De), Rev 0, 1 or 2.");
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

static YiSaveIndicator g_save_indicator;

static void load_sram(const char *path) {
  if (!g_sram || g_sram_size <= 0) return;
  FILE *f = fopen(path, "rb");
  if (f) {
    size_t n = fread(g_sram, 1, g_sram_size, f);
    fclose(f);
    printf("[SRAM] Loaded %zu bytes from %s\n", n, path);
  }
  YiSaveIndicatorLoaded(&g_save_indicator,g_sram,(size_t)g_sram_size);
}

static void save_sram(const char *path) {
  if (!g_sram || g_sram_size <= 0) return;
  ensure_dir_for_file(path);
  FILE *f = fopen(path, "wb");
  bool success=false;
  if (f) {
    size_t written=fwrite(g_sram,1,g_sram_size,f);
    int closed=fclose(f);
    success=written==(size_t)g_sram_size && closed==0;
    if(!success)fprintf(stderr,"[SRAM] Error: Failed to finish writing %s\n",path);
  } else {
    fprintf(stderr,"[SRAM] Error: Failed to open %s for writing!\n",path);
  }
  YiSaveIndicatorWritten(&g_save_indicator,g_sram,(size_t)g_sram_size,success,SDL_GetTicks64());
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
  if (ini_read_int(cfg_path, "Graphics", "VSync", &val) && (val==0 || val==1))
    settings->vsync = val ? RECOMP_LAUNCHER_VSYNC_ON : RECOMP_LAUNCHER_VSYNC_OFF;
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
  snprintf(buf, sizeof(buf), "%d", settings->vsync == RECOMP_LAUNCHER_VSYNC_ON ? 1 : 0);
  launcher_ini_kv_write(cfg_path, "Graphics", "VSync", buf);
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

#ifdef RECOMP_LAUNCHER
static int vsync_settings_selftest(void) {
  const char *path="vsync-selftest.ini";
  FILE *f=fopen(path,"rb");if(f){fclose(f);return 2;}
  RecompLauncherCSettings settings={0};settings.vsync=RECOMP_LAUNCHER_VSYNC_OFF;
  load_launcher_settings(&settings,path);
  int ok=settings.vsync==RECOMP_LAUNCHER_VSYNC_OFF;
  settings.vsync=RECOMP_LAUNCHER_VSYNC_ON;save_launcher_settings(&settings,path);
  settings.vsync=RECOMP_LAUNCHER_VSYNC_OFF;load_launcher_settings(&settings,path);
  ok &= settings.vsync==RECOMP_LAUNCHER_VSYNC_ON;
  settings.vsync=RECOMP_LAUNCHER_VSYNC_OFF;save_launcher_settings(&settings,path);
  settings.vsync=RECOMP_LAUNCHER_VSYNC_ON;load_launcher_settings(&settings,path);
  ok &= settings.vsync==RECOMP_LAUNCHER_VSYNC_OFF;
  launcher_ini_kv_write(path,"Graphics","VSync","17");
  settings.vsync=RECOMP_LAUNCHER_VSYNC_OFF;load_launcher_settings(&settings,path);
  ok &= settings.vsync==RECOMP_LAUNCHER_VSYNC_OFF;
  remove(path);printf("VSync missing default, on/off persistence and invalid setting: %s\n",ok?"PASS":"FAIL");return ok?0:1;
}
#endif
static int run_launcher_flow(int argc, char **argv, const char *cli_rom_override, char *out_rom_path, size_t out_rom_cap, RecompLauncherCSettings *settings) {
  memset(settings, 0, sizeof(*settings));
  settings->window_scale = 3;
  settings->fullscreen = 0;
  settings->linear_filter = 1;
  settings->vsync = RECOMP_LAUNCHER_VSYNC_OFF;
  settings->aspect_index = 0; /* 4:3 CRT default */
  settings->enable_audio = 1;
  settings->audio_freq = 44100;
  settings->volume = 100;
  settings->player_src[0] = 1; /* Keyboard */
  settings->deadzone[0] = 10;

  snesrecomp_exe_dir_path("config.ini", g_config_path, sizeof(g_config_path));
  snesrecomp_exe_dir_path("keybinds.ini", g_keybinds_path, sizeof(g_keybinds_path));

  load_launcher_settings(settings, g_config_path);
  int effect_value = 0;
  if (ini_read_int(g_config_path,"Mods","VideoEffect",&effect_value) &&
      effect_value >= 0 && effect_value < YI_EFFECT_COUNT) g_video_effect = (unsigned)effect_value;
  /* Stored as SDL keycodes; validate using the same path as the launcher. */
  int hotkey_value = 0;
  if (ini_read_int(g_config_path,"Mods","EffectsKeycode",&hotkey_value))
    YiEffectsLauncherSetHotkey(SDL_GetKeyName((SDL_Keycode)hotkey_value));


  int ws_value = 0;
  if (ini_read_int(g_config_path, "Mods", "Widescreen", &ws_value)) g_widescreen = ws_value != 0;

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
  game.region = "USA / Europe (Rev 0-2)";
  game.known_sha256 = yi_known_sha256;
  game.num_known_sha256 = YI_ROM_COUNT;
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
  game.has_vsync = 1;
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

#ifdef RECOMP_LAUNCHER
static int effects_hotkey_selftest(void) {
  const char *path = "effects-selftest.ini";
  FILE *existing = fopen(path,"rb");
  if (existing) { fclose(existing); return 2; }
  if (SDL_Init(SDL_INIT_EVENTS) != 0) return 3;
  snprintf(g_config_path,sizeof(g_config_path),"%s",path);
  int ok = YiEffectsLauncherSetHotkey("F2") && g_effect_hotkey == SDLK_F2 &&
           !YiEffectsLauncherSetHotkey("F8") && g_effect_hotkey == SDLK_F2;
  g_video_effect = 0;
  const unsigned effect_cycle[]={4,1,2,3,0};
  for (unsigned i=1;i<=YI_EFFECT_COUNT;++i) {
    ok &= YiEffectKeyDown(&g_video_effect,g_effect_hotkey,SDLK_F2,false,false);
    ok &= g_video_effect == effect_cycle[i-1];
  }
  ok &= YiEffectKeyDown(&g_video_effect,g_effect_hotkey,SDLK_F2,true,false) && g_video_effect==0;
  ok &= !YiEffectKeyDown(&g_video_effect,g_effect_hotkey,SDLK_F2,false,true) && g_video_effect==0;
  int saved=0;
  ok &= ini_read_int(path,"Mods","EffectsKeycode",&saved) && saved == SDLK_F2;
  remove(path);
  SDL_Quit();
  printf("Effects hotkey rebind, persistence, repeat and cycle: %s\n",ok?"PASS":"FAIL");
  return ok?0:1;
}
#endif
static void capture_ws_pixels(const char *path, const void *pixels, int width, int height, int pitch) {
  SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom((void *)pixels, width, height, 32, pitch, SDL_PIXELFORMAT_BGRA32);
  if (surface) { SDL_SaveBMP(surface, path); SDL_FreeSurface(surface); }
}

int main(int argc, char **argv) {
#ifdef RECOMP_LAUNCHER
  if(argc==2 && !strcmp(argv[1],"--effects-selftest")) return effects_hotkey_selftest();
  if(argc==2 && !strcmp(argv[1],"--vsync-selftest")) return vsync_settings_selftest();
#endif
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
             "ROM Error: %s\n\nPath: %s\n\nExpected: USA or Europe (En,Fr,De), Rev 0, 1 or 2",
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
  YiWs11Init(s_rom_profile);
  YiWs11SetEnabled(g_widescreen);
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
  if (g_widescreen) win_w = (win_h * 16 + 8) / 9;

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
  bool vsync_requested = false;
#ifdef RECOMP_LAUNCHER
  vsync_requested = g_settings.vsync == RECOMP_LAUNCHER_VSYNC_ON;
#endif
  SDL_SetHintWithPriority(SDL_HINT_RENDER_VSYNC, vsync_requested ? "1" : "0", SDL_HINT_OVERRIDE);

  SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED |
      (vsync_requested ? SDL_RENDERER_PRESENTVSYNC : 0));
  if (!renderer) renderer = SDL_CreateRenderer(window, -1, 0);
  if (!renderer) Die("SDL_CreateRenderer failed");
#if SDL_VERSION_ATLEAST(2,0,18)
  int vsync_result = SDL_RenderSetVSync(renderer, vsync_requested ? 1 : 0);
#else
  int vsync_result = 0;
#endif
  SDL_RendererInfo renderer_info={0};
  if(SDL_GetRendererInfo(renderer,&renderer_info)==0)
    printf("[Video] renderer=%s VSync requested=%s apply=%s\n",renderer_info.name,
      vsync_requested?"on":"off",vsync_result==0?"ok":SDL_GetError());

  SDL_RenderSetLogicalSize(renderer, pres_w, pres_h);
  const char *present_trace=SDL_getenv("YI_PRESENT_TRACE");
  YiPresentTraceInit(present_trace && !strcmp(present_trace,"1"));
  if(yi_present_trace.enabled)printf("[Video] Pre-Present readback diagnostic enabled; may reduce frame rate.\n");

  static YiPresentation presentation;
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

  bool capture_requested = false;
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
#ifdef RECOMP_LAUNCHER
        if (YiEffectKeyDown(&g_video_effect,g_effect_hotkey,event.key.keysym.sym,
            event.key.repeat != 0,(event.key.keysym.mod & (KMOD_CTRL|KMOD_ALT|KMOD_GUI)) != 0)) {
          if(!event.key.repeat) YiEffectsLauncherSetMode((int)g_video_effect);
          continue;
        }
#endif
        if (event.key.keysym.sym == SDLK_ESCAPE) {
          running = false;
        } else if (event.key.keysym.sym == SDLK_F9 && !event.key.repeat) {
          capture_requested = true;
        } else if (event.key.keysym.sym == SDLK_F8 && !event.key.repeat) {
          YiWs11Toggle();
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
    uint64_t emulation_start=SDL_GetPerformanceCounter();
    RtlRunFrame(s_pad_state);
    GameDrawPpuFrame();
    uint64_t emulation_end=SDL_GetPerformanceCounter();

    /* Present active viewport */
    bool wide = YiWs11PresentationWide();
    const uint8_t* source=wide ? YiWs11Pixels() : fb_pixels;
    unsigned source_width=wide ? YI_WS_WIDTH : 256;
    uint64_t filter_ticks=0;
    if(!YiPresentationDraw(&presentation,renderer,fb_pixels,YiWs11Pixels(),wide,g_video_effect,pres_w,pres_h,&filter_ticks))
      Die("Presentation failed");
    YiPresentTraceRecord(renderer,source,source_width,224,source_width*4,wide,g_video_effect);
    YiWs11FrameTiming((unsigned)((emulation_end-emulation_start)*1000000/perf_freq),
        (unsigned)((SDL_GetPerformanceCounter()-emulation_end)*1000000/perf_freq));
    if (capture_requested) {
      char native_path[128],wide_path[128],terrain_path[128],present_path[128],info_path[128];
      unsigned long long capture_id=(unsigned long long)SDL_GetPerformanceCounter();
      snprintf(native_path,sizeof(native_path),"ws-%llu-native.bmp",capture_id);
      snprintf(wide_path,sizeof(wide_path),"ws-%llu-wide.bmp",capture_id);
      snprintf(terrain_path,sizeof(terrain_path),"ws-%llu-terrain.bin",capture_id);
      YiWs11CaptureTerrain(terrain_path);
      char trace_prefix[64];snprintf(trace_prefix,sizeof(trace_prefix),"ws-%llu",capture_id);
      YiPresentTraceSave(trace_prefix);
      capture_ws_pixels(native_path, fb_pixels, 256, 224, 256 * 4);
      capture_ws_pixels(wide_path, YiWs11Pixels(), YI_WS_WIDTH, 224, YI_WS_WIDTH * 4);
      snprintf(present_path,sizeof(present_path),"ws-%llu-present.bmp",capture_id);
      SDL_Surface* capture=YiCaptureRenderer(renderer);
      if(capture) {
        SDL_SaveBMP(capture,present_path);
        SDL_FreeSurface(capture);
      }
      snprintf(info_path,sizeof(info_path),"ws-%llu-info.txt",capture_id);
      FILE* info=fopen(info_path,"wb");
      if(info){fprintf(info,"capture=%llu\npresentation_wide=%d\nscene=%s\nregion_hz=%.6f\nvsync_requested=%d\nrenderer=%s\npresent_trace=%d\neffect=%s\nnative=256x224\nwide=342x224\nRaw images use SNES pixel geometry; present.bmp records the final aspect-correct output. If presentation_wide=0 the wide buffer is diagnostic, not a valid current wide view.\n",capture_id,wide,YiWs11SceneName(),YiRomFrameRate(s_rom_profile),vsync_requested,renderer_info.name?renderer_info.name:"unknown",yi_present_trace.enabled,YiEffectName(g_video_effect));fclose(info);}
      printf("[WS11] F9 capture id=%llu: wide=%d room=%s\n",capture_id,wide,YiWs11SceneName());
      YiEffectsCapture(renderer,source,source_width,224,source_width*4,capture_id,(int)g_video_effect);
      capture_requested = false;
    }
    YiSaveIndicatorDraw(&g_save_indicator,renderer,SDL_GetTicks64());
    uint64_t present_start=SDL_GetPerformanceCounter();
    SDL_RenderPresent(renderer);
    YiPresentTracePresented((unsigned)((SDL_GetPerformanceCounter()-present_start)*1000000/perf_freq));
    YiEffectsRecord(g_video_effect,source_width,224,filter_ticks,vsync_requested);

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
  YiPresentTraceDestroy();
  YiPresentationDestroy(&presentation);
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
