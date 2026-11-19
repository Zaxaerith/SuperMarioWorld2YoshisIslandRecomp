#include "audio_trace.h"
#include "snes/dsp.h"
#include "snes/dsp_shadow.h"
DspShadow *dsp_shadow_create(void) { return NULL; }
void dsp_shadow_free(DspShadow *s) { (void)s; }
void dsp_shadow_process(DspShadow *s, Dsp *d, int l, int r, int *ol, int *or) {
  (void)s;
  (void)d;
  *ol = l;
  *or = r;
}
void dsp_shadow_verify_echo(const int16_t *l, const int16_t *r, const int8_t *c, int i, int a,
                            int b) {
  (void)l;
  (void)r;
  (void)c;
  (void)i;
  (void)a;
  (void)b;
}
void dsp_shadow_verify_brr(const uint8_t *a, uint16_t p, int o, int oo, const int16_t *b) {
  (void)a;
  (void)p;
  (void)o;
  (void)oo;
  (void)b;
}
void audio_trace_on_sample(int16_t l, int16_t r, int dropped, uint32_t n) {
  (void)l;
  (void)r;
  (void)dropped;
  (void)n;
}
void audio_trace_on_reg_write(uint8_t a, uint8_t v) {
  (void)a;
  (void)v;
}
void audio_trace_on_consume(uint64_t p, uint32_t n, uint32_t a) {
  (void)p;
  (void)n;
  (void)a;
}

/* Synthetic BRR data and DSP register sequences. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
#define CHECK(v)                                                                                   \
  do {                                                                                             \
    ++checks;                                                                                      \
    if (!(v)) {                                                                                    \
      fprintf(stderr, "FAIL line%d: %s\n", __LINE__, #v);                                          \
      return 0;                                                                                    \
    }                                                                                              \
  } while (0)
static uint8_t ram[65536], other_ram[65536];
static Dsp *setup(unsigned ch, unsigned pitch, int adsr) {
  memset(ram, 0, sizeof(ram));
  ram[0x100] = 0;
  ram[0x101] = 0x10;
  ram[0x102] = 0;
  ram[0x103] = 0x10;
  ram[0x1000] = 0xc3;
  memset(ram + 0x1001, 0x77, 8);
  Dsp *d = dsp_init(ram);
  dsp_reset(d);
  dsp_write(d, 0x6c, 0x20);
  dsp_write(d, 0xc, 127);
  dsp_write(d, 0x1c, 127);
  dsp_write(d, 0x5d, 1);
  dsp_write(d, ch * 16, 127);
  dsp_write(d, ch * 16 + 1, 127);
  dsp_write(d, ch * 16 + 2, pitch & 255);
  dsp_write(d, ch * 16 + 3, pitch >> 8);
  dsp_write(d, ch * 16 + 5, adsr ? 0x8f : 0);
  dsp_write(d, ch * 16 + 6, 0xe0);
  dsp_write(d, ch * 16 + 7, 127);
  return d;
}
static int start(Dsp *d, unsigned ch) {
  dsp_write(d, 0x4c, 1u << ch);
  for (unsigned i = 0; i < 2 && d->channel[ch].keyOn; ++i)
    dsp_cycle(d);
  CHECK(!d->channel[ch].keyOn);
  CHECK(d->channel[ch].keyOnDelay == 5);
  CHECK(d->channel[ch].gain == 0 && d->channel[ch].sampleOut == 0);
  CHECK(d->channel[ch].pitchCounter == 0x3000);
  CHECK(d->channel[ch].decodeOffset == 0x1009);
  for (unsigned i = 0; i < 5; ++i) {
    dsp_cycle(d);
    CHECK(d->channel[ch].gain == 0 && d->channel[ch].sampleOut == 0);
    CHECK(d->channel[ch].pitchCounter == 0x3000);
  }
  CHECK(d->channel[ch].keyOnReady);
  dsp_cycle(d);
  CHECK(!d->channel[ch].keyOnReady);
  CHECK(d->channel[ch].pitchCounter == 0x3000);
  CHECK(d->channel[ch].gain > 0);
  CHECK(d->channel[ch].sampleOut > 0);
  return 1;
}
static int test_voices(void) {
  const unsigned pitches[] = {0, 1, 0x100, 0x800, 0x1000, 0x2fff, 0x3000, 0x3fff};
  for (unsigned ch = 0; ch < 8; ++ch)
    for (unsigned k = 0; k < 8; ++k)
      for (int adsr = 0; adsr < 2; ++adsr) {
        Dsp *d = setup(ch, pitches[k], adsr);
        CHECK(start(d, ch));
        for (unsigned t = 0; t < 11; ++t)
          dsp_cycle(d);
        CHECK(start(d, ch));
        dsp_reset(d);
        CHECK(d->channel[ch].keyOnDelay == 0 && !d->channel[ch].keyOnReady);
        dsp_free(d);
      }
  return 1;
}
static int test_controls(void) {
  Dsp *d = setup(0, 0x1000, 0);
  dsp_write(d, 0x4c, 1);
  dsp_cycle(d);
  dsp_cycle(d);
  CHECK(d->channel[0].keyOnDelay > 0);
  dsp_write(d, 0x4c, 1);
  for (unsigned i = 0; i < 2 && d->channel[0].keyOn; ++i)
    dsp_cycle(d);
  CHECK(d->channel[0].keyOnDelay == 5);
  dsp_write(d, 0x5c, 1);
  for (unsigned i = 0; i < 8; ++i)
    dsp_cycle(d);
  CHECK(d->channel[0].gain == 0 && d->channel[0].sampleOut == 0 && d->channel[0].adsrState == 4);
  dsp_write(d, 0x5c, 0);
  dsp_write(d, 0x4c, 1);
  dsp_cycle(d);
  dsp_cycle(d);
  dsp_write(d, 0x6c, 0x80);
  dsp_cycle(d);
  CHECK(d->channel[0].keyOnDelay == 0 && !d->channel[0].keyOnReady);
  CHECK(d->channel[0].gain == 0);
  dsp_free(d);
  return 1;
}
static int test_restore(void) {
  Dsp *a = setup(0, 0x3fff, 0);
  dsp_write(a, 0x4c, 1);
  dsp_cycle(a);
  dsp_cycle(a);
  memcpy(other_ram, ram, sizeof(ram));
  Dsp *b = dsp_init(other_ram);
  dsp_reset(b);
  memcpy((uint8_t *)b + offsetof(Dsp, ram), (uint8_t *)a + offsetof(Dsp, ram),
         sizeof(Dsp) - offsetof(Dsp, ram));
  for (unsigned i = 0; i < 200; ++i) {
    dsp_cycle(a);
    dsp_cycle(b);
    CHECK(!memcmp((uint8_t *)a + offsetof(Dsp, ram), (uint8_t *)b + offsetof(Dsp, ram),
                  sizeof(Dsp) - offsetof(Dsp, ram)));
  }
  dsp_free(a);
  dsp_free(b);
  return 1;
}
/* A new note must not depend on the phase or decoded samples of its predecessor. */
static int test_previous_note_independence(void) {
  Dsp *a = setup(0, 0x3fff, 0);
  Dsp *b = setup(0, 0x3fff, 0);
  a->channel[0].pitchCounter = 0;
  b->channel[0].pitchCounter = 0xffff;
  memset(b->channel[0].decodeBuffer, 0x55, sizeof(b->channel[0].decodeBuffer));
  dsp_write(a, 0x4c, 1);
  dsp_write(b, 0x4c, 1);
  for (unsigned i = 0; i < 64; ++i) {
    dsp_cycle(a);
    dsp_cycle(b);
    if (!a->channel[0].keyOn) {
      CHECK(a->channel[0].sampleOut == b->channel[0].sampleOut);
      CHECK(a->channel[0].pitchCounter == b->channel[0].pitchCounter);
    }
  }
  dsp_free(a);
  dsp_free(b);
  return 1;
}

int main(void) {
  if (!test_voices() || !test_controls() || !test_restore() || !test_previous_note_independence())
    return 1;
  printf("DSP key-on: %u assertions passed\n", checks);
  return 0;
}
