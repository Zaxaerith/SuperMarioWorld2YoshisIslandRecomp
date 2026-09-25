/*
 * Super Mario World 2: Yoshi's Island (SNESRecomp Native AOT Desktop Player)
 * Real-time SDL2 interactive host: 60 FPS gameplay, authentic PPU/APU, SuperFX GSU.
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

  SDL_UnlockMutex(s_apu_mutex);
}

void debug_on_block_enter(uint32_t a, uint32_t b, uint32_t c, uint32_t d) { (void)a; (void)b; (void)c; (void)d; }
void debug_on_wram_write_byte(uint32_t a, uint8_t b, uint8_t c) { (void)a; (void)b; (void)c; }
void debug_on_wram_write_word(uint32_t a, uint16_t b, uint16_t c) { (void)a; (void)b; (void)c; }
void NORETURN Die(const char *s) { fprintf(stderr, "FATAL: %s\n", s); exit(1); }

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
static SDL_GameController *s_controllers[2] = {NULL, NULL};

static uint32_t poll_controller(SDL_GameController *ctrl) {
  if (!ctrl) return 0;
  uint32_t p = 0;
  if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_DPAD_UP))    p |= BTN_UP;
  if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  p |= BTN_DOWN;
  if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  p |= BTN_LEFT;
  if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) p |= BTN_RIGHT;

  int16_t ax = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_LEFTX);
  int16_t ay = SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_LEFTY);
  if (ax < -12000) p |= BTN_LEFT;
  if (ax > 12000)  p |= BTN_RIGHT;
  if (ay < -12000) p |= BTN_UP;
  if (ay > 12000)  p |= BTN_DOWN;

  /* SNES B is bottom button: Xbox A */
  if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_A)) p |= BTN_B;
  /* SNES A is right button: Xbox B */
  if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_B)) p |= BTN_A;
  /* SNES Y is left button: Xbox X */
  if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_X)) p |= BTN_Y;
  /* SNES X is top button: Xbox Y */
  if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_Y)) p |= BTN_X;

  if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_LEFTSHOULDER))  p |= BTN_L;
  if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) p |= BTN_R;
  if (SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000)  p |= BTN_L;
  if (SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000) p |= BTN_R;

  if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_START)) p |= BTN_START;
  if (SDL_GameControllerGetButton(ctrl, SDL_CONTROLLER_BUTTON_BACK))  p |= BTN_SELECT;

  return p;
}

static void update_keyboard(const uint8_t *keys) {
  uint32_t p1 = 0;
  if (keys[SDL_SCANCODE_UP]    || keys[SDL_SCANCODE_W]) p1 |= BTN_UP;
  if (keys[SDL_SCANCODE_DOWN]  || keys[SDL_SCANCODE_S]) p1 |= BTN_DOWN;
  if (keys[SDL_SCANCODE_LEFT]  || keys[SDL_SCANCODE_A]) p1 |= BTN_LEFT;
  if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) p1 |= BTN_RIGHT;

  /* Yoshi's Island Controls:
   * J / Space : Jump / Flutter (SNES B)
   * K / U     : Tongue / Eat / Spit (SNES Y)
   * L / I     : Throw Egg / Aim (SNES A)
   * O         : Lock Aim (SNES X)
   * Q         : Shoulder L
   * E         : Shoulder R
   * Enter     : START
   * Tab       : SELECT
   */
  if (keys[SDL_SCANCODE_J] || keys[SDL_SCANCODE_SPACE]) p1 |= BTN_B;
  if (keys[SDL_SCANCODE_K] || keys[SDL_SCANCODE_Z])     p1 |= BTN_Y;
  if (keys[SDL_SCANCODE_L] || keys[SDL_SCANCODE_X])     p1 |= BTN_A;
  if (keys[SDL_SCANCODE_I] || keys[SDL_SCANCODE_C])     p1 |= BTN_X;
  if (keys[SDL_SCANCODE_Q])                             p1 |= BTN_L;
  if (keys[SDL_SCANCODE_E])                             p1 |= BTN_R;
  if (keys[SDL_SCANCODE_RETURN])                        p1 |= BTN_START;
  if (keys[SDL_SCANCODE_TAB] || keys[SDL_SCANCODE_RSHIFT]) p1 |= BTN_SELECT;

  p1 |= poll_controller(s_controllers[0]);
  s_pad_state = p1;
}

static uint8_t *try_load_rom(const char *path, size_t *out_size) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz <= 0 || sz > 16 * 1024 * 1024) {
    fclose(f);
    return NULL;
  }
  uint8_t *buf = (uint8_t *)malloc(sz);
  if (!buf) { fclose(f); return NULL; }
  if (fread(buf, 1, sz, f) != (size_t)sz) {
    free(buf);
    fclose(f);
    return NULL;
  }
  fclose(f);
  *out_size = (size_t)sz;
  return buf;
}

static uint8_t *find_and_load_rom(int argc, char **argv, size_t *out_size) {
  if (argc > 1) {
    uint8_t *buf = try_load_rom(argv[1], out_size);
    if (buf) {
      printf("[ROM] Loaded user-specified ROM: %s (%zu bytes)\n", argv[1], *out_size);
      return buf;
    }
  }
  const char *candidates[] = {
    "Super Mario World 2 - Yoshi's Island (USA).sfc",
    "../Super Mario World 2 - Yoshi's Island (USA).sfc",
    "yoshisisland-disassembly-master/yi.sfc",
    "../yoshisisland-disassembly-master/yi.sfc"
  };
  for (size_t i = 0; i < sizeof(candidates)/sizeof(candidates[0]); i++) {
    uint8_t *buf = try_load_rom(candidates[i], out_size);
    if (buf) {
      printf("[ROM] Found and loaded: %s (%zu bytes)\n", candidates[i], *out_size);
      return buf;
    }
  }
  return NULL;
}

extern uint8_t *g_sram;
extern int g_sram_size;

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
  FILE *f = fopen(path, "wb");
  if (f) {
    fwrite(g_sram, 1, g_sram_size, f);
    fclose(f);
  }
}

int main(int argc, char **argv) {

#ifdef _WIN32
  init_timer_precision();
#endif
  printf("=================================================================\n");
  printf("  Super Mario World 2: Yoshi's Island (SNESRecomp Native AOT)   \n");
  printf("=================================================================\n");
  printf("Controls:\n");
  printf("  W / A / S / D or Arrow Keys : D-Pad (Walk, Aim Up, Crouch)\n");
  printf("  J or Space                  : Jump / Flutter Jump (SNES B)\n");
  printf("  K or Z                      : Eat / Tongue / Spit (SNES Y)\n");
  printf("  L or X                      : Throw Egg / Aim (SNES A)\n");
  printf("  I or C                      : Lock Aim Cursor (SNES X)\n");
  printf("  Q / E                       : L / R Shoulder\n");
  printf("  Enter                       : START (Pause / Map)\n");
  printf("  Tab                         : SELECT (Item Menu)\n");
  printf("  F11                         : Toggle Fullscreen\n");
  printf("  Esc                         : Quit\n");
  printf("Gamepad:\n");
  printf("  Plug-and-play USB/Bluetooth Gamepads (Xbox/PlayStation) Supported!\n");
  printf("=================================================================\n\n");

  size_t rom_size = 0;
  uint8_t *rom_data = find_and_load_rom(argc, argv, &rom_size);
  if (!rom_data) {
    fprintf(stderr, "Error: Could not locate 'Super Mario World 2 - Yoshi's Island (USA).sfc'!\n");
    return 1;
  }

  RtlRegisterGame(&kGameInfo);
  if (!SnesInit(rom_data, (int)rom_size)) {
    fprintf(stderr, "Error: Failed to initialize SNES core!\n");
    free(rom_data);
    return 2;
  }
  free(rom_data);

  if (!g_snes || !g_snes->cart || !g_snes->cart->superfx) {
    fprintf(stderr, "Warning: SuperFX coprocessor was not initialized!\n");
  } else {
    printf("[Hardware] SuperFX (GSU-2) coprocessor active.\n");
  }

  load_sram("save.srm");


  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
    fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
    return 3;
  }

  s_apu_mutex = SDL_CreateMutex();
  if (!s_apu_mutex) Die("SDL_CreateMutex failed");

  const int scale = 3;
  const int win_w = 256 * scale;
  const int win_h = 224 * scale;

  SDL_Window *window = SDL_CreateWindow(
    "Super Mario World 2: Yoshi's Island - Recompiled Native AOT",
    SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
    win_w, win_h,
    SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE
  );
  if (!window) {
    fprintf(stderr, "SDL_CreateWindow Error: %s\n", SDL_GetError());
    SDL_Quit();
    return 4;
  }

  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
  SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");

  SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if (!renderer) renderer = SDL_CreateRenderer(window, -1, 0);
  if (!renderer) Die("SDL_CreateRenderer failed");

  SDL_RenderSetLogicalSize(renderer, 256, 224);

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

  SDL_AudioDeviceID audio_dev = 0;
  SDL_AudioSpec want, have;
  SDL_memset(&want, 0, sizeof(want));
  want.freq = 44100;
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

  bool running = true;
  SDL_Event event;

  const uint64_t perf_freq = SDL_GetPerformanceFrequency();
  const double target_frame_cycles = (double)perf_freq / 60.09881389744051;
  uint64_t next_frame_time = SDL_GetPerformanceCounter();

  printf("[Game] Entering authentic 60 FPS scanline execution loop...\n");

  while (running) {
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_QUIT) {
        running = false;
      } else if (event.type == SDL_KEYDOWN) {
        if (event.key.keysym.sym == SDLK_ESCAPE) {
          running = false;
        } else if (event.key.keysym.sym == SDLK_F11) {
          static bool s_fullscreen = false;
          s_fullscreen = !s_fullscreen;
          SDL_SetWindowFullscreen(window, s_fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        }
      } else if (event.type == SDL_CONTROLLERDEVICEADDED) {
        int which = event.cdevice.which;
        if (!s_controllers[0]) {
          s_controllers[0] = SDL_GameControllerOpen(which);
          if (s_controllers[0]) {
            printf("[Input] Gamepad 1 connected: %s\n", SDL_GameControllerName(s_controllers[0]));
          }
        }
      } else if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
        SDL_JoystickID jid = event.cdevice.which;
        if (s_controllers[0]) {
          SDL_Joystick *joy = SDL_GameControllerGetJoystick(s_controllers[0]);
          if (joy && SDL_JoystickInstanceID(joy) == jid) {
            printf("[Input] Gamepad 1 disconnected\n");
            SDL_GameControllerClose(s_controllers[0]);
            s_controllers[0] = NULL;
          }
        }
      }
    }

    const uint8_t *key_states = SDL_GetKeyboardState(NULL);
    update_keyboard(key_states);

    /* Run one full hardware frame (262 scanlines, AOT dispatched + SuperFX + PPU) */
    RtlRunFrame(s_pad_state);
    GameDrawPpuFrame();

    /* Present active 256x224 viewport */
    SDL_UpdateTexture(texture, NULL, fb_pixels, 256 * 4);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);

    static uint32_t s_frame_save_counter = 0;
    if (++s_frame_save_counter % 300 == 0) {
      save_sram("save.srm");
    }

    /* Regulate 60 FPS pacing */
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

  save_sram("save.srm");
  printf("[SRAM] Saved game state to save.srm\n");

  if (audio_dev > 0) SDL_CloseAudioDevice(audio_dev);
  if (texture) SDL_DestroyTexture(texture);
  if (renderer) SDL_DestroyRenderer(renderer);
  if (window) SDL_DestroyWindow(window);
  SDL_Quit();

#ifdef _WIN32
  cleanup_timer_precision();
#endif
  return 0;
}

