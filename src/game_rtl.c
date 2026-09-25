/* Yoshi's Island (SNESRecomp Native AOT) Frame Engine.
 * Advances beam, executes recompiled 65816 / SuperFX, handles NMI/IRQ and PPU scanlines.
 */
#include "game_rtl.h"
#include "common_rtl.h"
#include "cpu_state.h"
#include "snes/snes.h"
#include "snes/dma.h"
#include "snes/ppu.h"
#include "snes/superfx.h"
#include "snes/interp_bridge.h"
#include <stdio.h>
#include <stdlib.h>

#define MASTER_CYCLES_PER_LINE 1364ull
#define SCANLINES_PER_FRAME 262u
#define MASTER_CYCLES_PER_FRAME (SCANLINES_PER_FRAME * MASTER_CYCLES_PER_LINE)

static uint32_t resume_pc;
static uint64_t s_frame_base_clock;
static int parked;
uint64_t yi_nmi_count, yi_irq_count, yi_max_late;
uint64_t yi_total_slices;

static uint32_t vector(uint32_t adr) {
  return snes_read(g_snes, adr) | ((uint32_t)snes_read(g_snes, adr + 1) << 8);
}

static void interrupt_at(uint32_t adr) {
  DmaChannel saved_ch0;
  if (g_snes && g_snes->dma) {
    saved_ch0 = g_snes->dma->channel[0];
  }

  cpu_push_interrupt_frame_at(&g_cpu, resume_pc);
  interp_bridge_set_master_deadline(0);
  if (!interp_bridge_run_interrupt(&g_cpu, vector(adr))) {
    Die("interrupt did not reach RTI");
  }

  if (g_snes && g_snes->dma) {
    g_snes->dma->channel[0].bAdr = saved_ch0.bAdr;
    g_snes->dma->channel[0].mode = saved_ch0.mode;
    g_snes->dma->channel[0].aAdr = saved_ch0.aAdr;
    g_snes->dma->channel[0].aBank = saved_ch0.aBank;
    g_snes->dma->channel[0].size = saved_ch0.size;
  }
  parked = 0;
}

static void run_scanline(uint64_t line_deadline) {
  /* Step 1: Run CPU until it catches up to the scanline deadline or parks */
  for (unsigned slices = 0; g_cpu.master_cycles < line_deadline; ++slices) {
    if (slices > 100000) Die("frame slice limit exceeded");
    ++yi_total_slices;

    if (g_snes && g_snes->inIrq && !g_cpu._flag_I) {
      ++yi_irq_count;
      interrupt_at(g_cpu.emulation ? 0xfffe : 0xffee);
      continue;
    }

    if (parked) {
      uint64_t target = line_deadline;
      uint32_t irq = snes_master_clocks_until_irq(g_snes);
      if (irq && !g_cpu._flag_I && g_cpu.master_cycles + irq < target)
        target = g_cpu.master_cycles + irq;
      g_cpu.master_cycles = target;
      snes_refresh_exempt();
      if (g_snes) snes_sync_master_clock(g_snes, target);
      continue;
    }

    uint64_t slice = line_deadline;
    interp_bridge_set_master_deadline(slice);
    if (!interp_bridge_run_until_quiescent(&g_cpu, resume_pc)) {
      Die("mainline bridge bailed");
    }
    interp_bridge_set_master_deadline(0);

    resume_pc = interp_bridge_lle_resume_pc();
    int wai = interp_bridge_lle_took_wai();
    int quiescent = interp_bridge_lle_took_quiescent();
    parked = wai || quiescent;
  }

  /* Step 2: Advance beam & coprocessors (SuperFX) to scanline deadline and handle any IRQs */
  if (g_snes) {
    snes_sync_master_clock(g_snes, line_deadline);
    while (g_snes->inIrq && !g_cpu._flag_I) {
      ++yi_irq_count;
      interrupt_at(g_cpu.emulation ? 0xfffe : 0xffee);
      snes_sync_master_clock(g_snes, line_deadline);
    }
  }

  if (g_cpu.master_cycles > line_deadline && g_cpu.master_cycles - line_deadline > yi_max_late)
    yi_max_late = g_cpu.master_cycles - line_deadline;
}

void GameRunOneFrame(void) {
  static unsigned s_cur_frame = 0;
  s_cur_frame++;

  if (!resume_pc) {
    resume_pc = vector(0xfffc);
    s_frame_base_clock = 0;
    g_cpu.master_cycles = 0;
    if (g_snes) {
      g_snes->beamMasterLast = 0;
      g_snes->hPos = 0;
      g_snes->vPos = 0;
    }
    ppu_runLine(g_ppu, 0);
  }

  for (unsigned line = 0; line < SCANLINES_PER_FRAME; ++line) {
    if (line == 0) g_snes->inNmi = false;
    if (line == 225) {
      ppu_checkOverscan(g_ppu);
      ppu_handleVblank(g_ppu);
      g_snes->inNmi = true;
      if (g_snes->nmiEnabled) {
        ++yi_nmi_count;
        interrupt_at(g_cpu.emulation ? 0xfffa : 0xffea);
      }
    }

    if (line <= 224) {
      /* Advance beam to active display area (before HBlank HDMA at h=1024) */
      uint64_t active_deadline = s_frame_base_clock + (uint64_t)line * MASTER_CYCLES_PER_LINE + 1000;
      run_scanline(active_deadline);
      ppu_runLine(g_ppu, (int)line);
    }

    uint64_t line_deadline = s_frame_base_clock + (uint64_t)(line + 1) * MASTER_CYCLES_PER_LINE;
    run_scanline(line_deadline);
  }
  s_frame_base_clock += MASTER_CYCLES_PER_FRAME;
}

void GameDrawPpuFrame(void) {
  /* Lines rendered on shared timeline */
}

const RtlGameInfo kGameInfo = {
  .title = "Super Mario World 2: Yoshi's Island",
  .initialize = NULL,
  .run_frame = GameRunOneFrame,
  .draw_ppu_frame = GameDrawPpuFrame,
  .save_name_prefix = "save",
  .state_save_extra = NULL,
  .state_load_extra = NULL,
  .on_state_loaded = NULL
};

void GameSessionReset(void) {
  resume_pc = 0;
  s_frame_base_clock = 0;
  parked = 0;
  yi_nmi_count = yi_irq_count = yi_max_late = 0;
  yi_total_slices = 0;
  if (g_snes) {
    g_snes->beamMasterLast = 0;
    g_snes->hPos = 0;
    g_snes->vPos = 0;
  }
}

uint32_t GameGetResumePc(void) {
  return resume_pc;
}

