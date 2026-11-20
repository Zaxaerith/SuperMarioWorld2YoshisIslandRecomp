#include "yi_presentation.h"
#include "yi_video_effects.h"
#include "yi_present_trace.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x);                          \
      return 1;                                                                \
    }                                                                          \
  } while (0)
static YiPresentation state;
static uint32_t native[256 * 224], wide[342 * 224];
int main(void) {
  for (unsigned i = 0; i < 256 * 224; ++i)
    native[i] = 0xff112233u;
  for (unsigned i = 0; i < 342 * 224; ++i)
    wide[i] = 0xff445566u;
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  SDL_Surface *surface =
      SDL_CreateRGBSurfaceWithFormat(0, 2400, 1350, 32, SDL_PIXELFORMAT_BGRA32);
  CHECK(surface);
  SDL_Renderer *renderer = SDL_CreateSoftwareRenderer(surface);
  CHECK(renderer);
  const bool sequence[] = {true, false, true, false};
  for (unsigned mode = 0; mode < YI_EFFECT_COUNT; ++mode)
    for (unsigned step = 0; step < 4; ++step) {
      bool is_wide = sequence[step];
      uint64_t ticks;
      CHECK(YiPresentationDraw(&state, renderer, (uint8_t *)native,
                               (uint8_t *)wide, is_wide, mode, 2400, 1800,
                               &ticks));
      CHECK(state.width == (is_wide ? 342 : 256) &&
            state.scale == (mode ? 3 : 1));
      CHECK(SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_BGRA32,
                                 surface->pixels, surface->pitch) == 0);
      uint32_t center = ((uint32_t *)((uint8_t *)surface->pixels +
                                      675 * surface->pitch))[1200];
      if (mode == YI_EFFECT_ORIGINAL || mode == YI_EFFECT_SHARP)
        CHECK((center & 0xffffff) == (is_wide ? 0x445566 : 0x112233));
      if (is_wide)
        CHECK((((uint32_t *)surface->pixels)[0] & 0xffffff) == 0);
    }
  CHECK(native[0] == 0xff112233 && wide[0] == 0xff445566);
  YiPresentationDestroy(&state);
  SDL_DestroyRenderer(renderer);
  SDL_FreeSurface(surface);
  /* A deliberately black backbuffer with a bright guest source must be
   * identified as an output-stage blackout, not a native black field. */
  surface=SDL_CreateRGBSurfaceWithFormat(0,120,80,32,SDL_PIXELFORMAT_BGRA32);
  CHECK(surface);renderer=SDL_CreateSoftwareRenderer(surface);CHECK(renderer);
  YiPresentTraceInit(false);
  YiPresentTraceRecord(renderer,(uint8_t*)native,256,224,256*4,true,YI_EFFECT_SHARP);
  CHECK(yi_present_trace.count==0);
  CHECK(SDL_RenderSetLogicalSize(renderer,8,6)==0);
  SDL_Rect trace_view_before,trace_view_after;float scale_x,scale_y,after_x,after_y;
  SDL_RenderGetViewport(renderer,&trace_view_before);SDL_RenderGetScale(renderer,&scale_x,&scale_y);
  YiPresentTraceInit(true);
  for(unsigned i=0;i<183;++i) {
    bool black=i==180;
    SDL_SetRenderDrawColor(renderer,black?0:68,black?0:85,black?0:102,255);
    CHECK(SDL_RenderClear(renderer)==0);
    YiPresentTraceRecord(renderer,(uint8_t*)native,256,224,256*4,true,YI_EFFECT_SHARP);
    YiPresentTracePresented(123);
  }
  CHECK(yi_present_trace.count==180 && yi_present_trace.frame==183);
  unsigned black_count=0;
  for(unsigned i=0;i<180;++i) {
    const YiPresentSample *sample=&yi_present_trace.samples[i];
    CHECK(sample->read_ok && !sample->source_black && sample->present_us==123);
    if(sample->rendered_black){++black_count;CHECK(sample->frame==181);}
  }
  CHECK(black_count==1);
  SDL_RenderGetViewport(renderer,&trace_view_after);SDL_RenderGetScale(renderer,&after_x,&after_y);
  CHECK(!memcmp(&trace_view_before,&trace_view_after,sizeof(trace_view_before)) &&
      scale_x==after_x && scale_y==after_y);
  Uint8 rr,gg,bb,aa;SDL_GetRenderDrawColor(renderer,&rr,&gg,&bb,&aa);
  CHECK(rr==68 && gg==85 && bb==102 && aa==255 && SDL_GetRenderTarget(renderer)==NULL);
  SDL_Rect viewport_before,viewport_after;SDL_RenderGetViewport(renderer,&viewport_before);
  CHECK(YiPresentTraceSave("presentation-trace-fixture"));
  SDL_RenderGetViewport(renderer,&viewport_after);
  CHECK(!memcmp(&viewport_before,&viewport_after,sizeof(viewport_before)));
  SDL_Surface *sheet=SDL_LoadBMP("presentation-trace-fixture-present-history.bmp");
  CHECK(sheet && sheet->w==1152 && sheet->h==810);SDL_FreeSurface(sheet);
  FILE *trace_file=fopen("presentation-trace-fixture-present-history.csv","rb");CHECK(trace_file);
  char trace_line[512];CHECK(fgets(trace_line,sizeof(trace_line),trace_file));
  CHECK(fgets(trace_line,sizeof(trace_line),trace_file));CHECK(!strncmp(trace_line,"4,",2));
  fclose(trace_file);
  YiPresentTraceInit(true);
  YiPresentTraceRecord(NULL,(uint8_t*)native,256,224,256*4,true,YI_EFFECT_SHARP);
  CHECK(!yi_present_trace.samples[0].read_ok && !yi_present_trace.samples[0].rendered_black);
  static uint8_t black_source[256*224*4];
  SDL_SetRenderDrawColor(renderer,0,0,0,255);CHECK(SDL_RenderClear(renderer)==0);
  YiPresentTraceRecord(renderer,black_source,256,224,256*4,true,YI_EFFECT_SHARP);
  CHECK(yi_present_trace.samples[1].source_black && yi_present_trace.samples[1].rendered_black &&
      yi_present_trace.samples[1].read_ok);
  YiPresentTraceDestroy();CHECK(!yi_present_trace.readback && !yi_present_trace.enabled);
  SDL_DestroyRenderer(renderer);SDL_FreeSurface(surface);
  puts("PASS actual combined presenter: all modes, native/wide/fallback "
       "transitions, texture sizing, aspect border and source preservation. No "
       "game/window/account.");
  return 0;
}
