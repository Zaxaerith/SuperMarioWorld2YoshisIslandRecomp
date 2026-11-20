#ifndef YI_PRESENT_TRACE_H
#define YI_PRESENT_TRACE_H
#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Opt-in host-window backbuffer readback, immediately before Present. This is
 * not desktop capture or proof of what the monitor displayed after the swap.
 * Readback can stall the renderer: keep it out of the normal execution path. */
enum { YI_TRACE_COUNT=180, YI_TRACE_W=96, YI_TRACE_H=54 };
typedef struct YiPresentSample {
  uint64_t frame, ticks;
  unsigned width,height,wide,effect,source_luma,source_black;
  unsigned rendered_luma,rendered_black,read_ok,read_us,present_us;
  uint8_t pixels[YI_TRACE_W*YI_TRACE_H*3];
} YiPresentSample;
static struct {
  bool enabled;
  unsigned next,count;
  uint64_t frame;
  SDL_Surface *readback;
  YiPresentSample samples[YI_TRACE_COUNT];
} yi_present_trace;

static void YiPresentTraceDestroy(void) {
  SDL_FreeSurface(yi_present_trace.readback);
  memset(&yi_present_trace,0,sizeof(yi_present_trace));
}
static void YiPresentTraceInit(bool enabled) {
  YiPresentTraceDestroy();yi_present_trace.enabled=enabled;
}
static void YiPresentTraceRecord(SDL_Renderer *renderer,const uint8_t *source,
    unsigned sw,unsigned sh,unsigned pitch,bool wide,unsigned effect) {
  if(!yi_present_trace.enabled)return;
  YiPresentSample *s=&yi_present_trace.samples[yi_present_trace.next];
  memset(s,0,sizeof(*s));s->frame=++yi_present_trace.frame;
  s->ticks=SDL_GetTicks64();s->wide=wide;s->effect=effect;
  uint64_t total=0;
  if(source && sw && sh && pitch>=sw*4) {
    for(unsigned y=0;y<sh;++y)for(unsigned x=0;x<sw;++x) {
      const uint8_t *p=source+y*pitch+x*4;total+=p[0]+p[1]+p[2];
    }
    s->source_luma=(unsigned)(total/((uint64_t)sw*sh*3));
    s->source_black=total==0;
  }
  uint64_t start=SDL_GetPerformanceCounter();int w=0,h=0;
  if(renderer && SDL_GetRendererOutputSize(renderer,&w,&h)==0 && w>0 && h>0 &&
      (uint64_t)w*h<=64000000) {
    s->width=w;s->height=h;
    SDL_Surface *b=yi_present_trace.readback;
    if(!b || b->w!=w || b->h!=h) {
      SDL_FreeSurface(b);
      b=yi_present_trace.readback=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_BGRA32);
    }
    SDL_Rect output={0,0,w,h};
    if(b && SDL_FillRect(b,NULL,0)==0 &&
        SDL_RenderReadPixels(renderer,&output,SDL_PIXELFORMAT_BGRA32,b->pixels,b->pitch)==0) {
      total=0;s->read_ok=1;
      /* Read every RGB pixel so an exact black field is distinguished from a
       * dark scene, letterboxing, or failed/unavailable readback. */
      for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
        const uint8_t *p=(const uint8_t*)b->pixels+y*b->pitch+x*4;
        total+=p[0]+p[1]+p[2];
      }
      s->rendered_luma=(unsigned)(total/((uint64_t)w*h*3));
      s->rendered_black=total==0;
      for(unsigned y=0;y<YI_TRACE_H;++y)for(unsigned x=0;x<YI_TRACE_W;++x)
        memcpy(s->pixels+(y*YI_TRACE_W+x)*3,(const uint8_t*)b->pixels+
            (y*(uint64_t)h/YI_TRACE_H)*b->pitch+(x*(uint64_t)w/YI_TRACE_W)*4,3);
    }
  }
  s->read_us=(unsigned)((SDL_GetPerformanceCounter()-start)*1000000/SDL_GetPerformanceFrequency());
  yi_present_trace.next=(yi_present_trace.next+1)%YI_TRACE_COUNT;
  if(yi_present_trace.count<YI_TRACE_COUNT)++yi_present_trace.count;
}
static void YiPresentTracePresented(unsigned us) {
  if(yi_present_trace.enabled && yi_present_trace.count)
    yi_present_trace.samples[(yi_present_trace.next+YI_TRACE_COUNT-1)%YI_TRACE_COUNT].present_us=us;
}
static void YiPresentTraceWord(uint8_t *p,unsigned value) {
  for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(value>>(i*8));
}
static bool YiPresentTraceSave(const char *prefix) {
  if(!yi_present_trace.enabled || !yi_present_trace.count)return false;
  char path[512];snprintf(path,sizeof(path),"%s-present-history.csv",prefix);
  FILE *f=fopen(path,"wb");if(!f)return false;
  fputs("trace_frame,ticks_ms,width,height,wide,effect,source_luma,source_black,rendered_luma,rendered_black,read_ok,read_us,present_us\n",f);
  unsigned first=(yi_present_trace.next+YI_TRACE_COUNT-yi_present_trace.count)%YI_TRACE_COUNT;
  for(unsigned i=0;i<yi_present_trace.count;++i) {
    const YiPresentSample *s=&yi_present_trace.samples[(first+i)%YI_TRACE_COUNT];
    fprintf(f,"%llu,%llu,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",
        (unsigned long long)s->frame,(unsigned long long)s->ticks,s->width,s->height,
        s->wide,s->effect,s->source_luma,s->source_black,s->rendered_luma,
        s->rendered_black,s->read_ok,s->read_us,s->present_us);
  }
  bool ok=!ferror(f);if(fclose(f))ok=false;
  snprintf(path,sizeof(path),"%s-present-history.bmp",prefix);f=fopen(path,"wb");if(!f)return false;
  unsigned w=YI_TRACE_W*12,h=YI_TRACE_H*((yi_present_trace.count+11)/12),pitch=w*3;
  uint8_t header[54]={0};header[0]='B';header[1]='M';
  YiPresentTraceWord(header+2,54+pitch*h);YiPresentTraceWord(header+10,54);
  YiPresentTraceWord(header+14,40);YiPresentTraceWord(header+18,w);
  YiPresentTraceWord(header+22,h);header[26]=1;header[28]=24;
  fwrite(header,1,54,f);uint8_t row[YI_TRACE_W*12*3];
  for(unsigned y=h;y>0;--y) {
    unsigned sy=y-1;memset(row,0,sizeof(row));
    for(unsigned c=0;c<12;++c) {
      unsigned i=(sy/YI_TRACE_H)*12+c;if(i>=yi_present_trace.count)continue;
      const YiPresentSample *s=&yi_present_trace.samples[(first+i)%YI_TRACE_COUNT];
      memcpy(row+c*YI_TRACE_W*3,s->pixels+(sy%YI_TRACE_H)*YI_TRACE_W*3,YI_TRACE_W*3);
    }
    fwrite(row,1,sizeof(row),f);
  }
  if(ferror(f))ok=false;if(fclose(f))ok=false;return ok;
}
#endif
