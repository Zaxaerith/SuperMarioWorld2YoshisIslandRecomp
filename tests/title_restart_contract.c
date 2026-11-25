/* Synthetic machine only: no game ROM or CPU instructions execute. */
#include "yi_title_restart.h"
#include "yi_menu_game_input.h"
#include "cpu_state.h"
#include "snes/snes.h"
#include "snes/ppu.h"
#include "snes/cart.h"
struct SpcPlayer *g_spc_player;
bool g_new_ppu=true;
void RtlApuLock(void){} void RtlApuUnlock(void){}
void Die(const char *s){fprintf(stderr,"%s\n",s);exit(1);}
void debug_on_block_enter(uint32_t a,uint32_t b,uint32_t c,uint32_t d){(void)a;(void)b;(void)c;(void)d;}
void debug_on_wram_write_byte(uint32_t a,uint8_t b,uint8_t c){(void)a;(void)b;(void)c;}
void debug_on_wram_write_word(uint32_t a,uint16_t b,uint16_t c){(void)a;(void)b;(void)c;}
#define CHECK(x) do{if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(void){
 uint8_t *rom=calloc(1,YI_ROM_SIZE);CHECK(rom);
 rom[0x7fd5]=0x20;rom[0x7fd6]=0x15;rom[0x7fd7]=11;rom[0x7fd8]=6;
 rom[0x7fd9]=1;rom[0x7fdc]=rom[0x7fdd]=0xff;rom[0x7ffd]=0x80;
 RtlRegisterGame(&kGameInfo);CHECK(SnesInit(rom,YI_ROM_SIZE));free(rom);
 CHECK(g_sram_size==65536);
 uint8_t pixels[256*224*4]={0};g_ppu->renderBuffer=pixels;g_ppu->renderPitch=1024;
 Snes *machine=g_snes;Ppu *ppu=g_ppu;uint8_t *sram=g_sram;
 uint8_t *saved=malloc(g_sram_size);CHECK(saved);
 for(int i=0;i<g_sram_size;++i)saved[i]=(uint8_t)(i*17+3);
 const char *path="title-reset-fixture.srm";
 FILE *f=fopen(path,"wb");CHECK(f);CHECK(fwrite(saved,1,g_sram_size,f)==(size_t)g_sram_size);CHECK(!fclose(f));
 for(int profile=0;profile<YI_ROM_COUNT;++profile){
  GameConfigureRom((YiRomProfile)profile);
  memset(g_ram,0xaa,0x20000);memset(g_sram,0xbb,g_sram_size);
  g_cpu.A=0xabcd;g_cpu.master_cycles=123456789;g_cpu.coprocessor_master_cycles=987654;
  g_snes->inIrq=g_snes->inNmi=true;g_snes->hPos=345;g_snes->vPos=123;
  g_ppu->vram[17]=0xbeef;
  uint64_t generation=RtlStateGeneration();
  CHECK(YiTitleRestartReady(path,(YiRomProfile)profile));
  CHECK(g_snes==machine && g_ppu==ppu && g_sram==sram && g_ppu->renderBuffer==pixels && g_ppu->renderPitch==1024);
  CHECK(!memcmp(g_sram,saved,g_sram_size));
  CHECK(g_cpu.A==0 && g_cpu.master_cycles==0 && g_cpu.coprocessor_master_cycles==0 && g_cpu.ram==g_ram);
  CHECK(!g_snes->inIrq && !g_snes->inNmi && !g_snes->hPos && !g_snes->vPos && !g_ppu->vram[17]);
  CHECK(!GameGetResumePc() && RtlStateGeneration()==generation+1);
  CHECK(g_snes->pal==(bool)YiRomIsPal((YiRomProfile)profile));
  for(unsigned i=0;i<0x20000;++i)CHECK(g_ram[i]==0);
  unsigned shift=YiRomIsPal((YiRomProfile)profile)?4:0;
  g_ram[0x118]=15+shift;g_ram[0x21a]=0;g_sram[0x1fea]=g_sram[0x1ae]=g_sram[0x1b0]=g_sram[0xac]=0;
  CHECK(!YiMenuCanExitLevel(g_ram,g_sram,(YiRomProfile)profile));
  g_ram[0x222]=0x80;CHECK(!YiMenuCanExitLevel(g_ram,g_sram,(YiRomProfile)profile));
  g_ram[0x222]=0xe4;CHECK(YiMenuCanExitLevel(g_ram,g_sram,(YiRomProfile)profile));
  g_ram[0xd0f]=1;CHECK(!YiMenuCanExitLevel(g_ram,g_sram,(YiRomProfile)profile));g_ram[0xd0f]=0;
  g_sram[0x1ae]=1;CHECK(!YiMenuCanExitLevel(g_ram,g_sram,(YiRomProfile)profile));g_sram[0x1ae]=0;
  g_ram[0x118]=0x22+shift;CHECK(!YiMenuCanExitLevel(g_ram,g_sram,(YiRomProfile)profile));
  YiMenuGameInput input={.exit_ticks=6};
  for(unsigned i=0;i<6;++i)CHECK(YiMenuGameInputNext(&input,0xffff)==(i>=2&&i<4?12:0));
  CHECK(YiMenuGameInputNext(&input,0x123)==0x123);
  // A reset never synthesizes Start: intro, title and file selection wait
  // for the player, while actual user input passes through unchanged.
  input=(YiMenuGameInput){0};
  for(unsigned i=0;i<180;++i)CHECK(!YiMenuGameInputNext(&input,0));
  CHECK(YiMenuGameInputNext(&input,8)==8);
  CHECK(YiMenuGameInputNext(&input,0x123)==0x123);
 }
 // Short reads must leave the live game fully resumable.
 f=fopen(path,"wb");CHECK(f);fputc(1,f);CHECK(!fclose(f));
 g_ram[123]=77;g_cpu.A=0x4242;uint64_t generation=RtlStateGeneration();
 CHECK(!YiTitleRestartReady(path,YI_ROM_US_10));
 CHECK(g_ram[123]==77&&g_cpu.A==0x4242&&RtlStateGeneration()==generation);
 CHECK(!remove(path));free(saved);
 puts("PASS: four-profile in-process reset, saved SRAM reload, failed-read preservation, native Exit level eligibility, input sequencing and manual title input. No game execution.");return 0;
}
